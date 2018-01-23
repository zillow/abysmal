/*
 * Copyright (C) 2016 Zillow
 *
 * Decimal Stack-Machine (DSM).
 *
 * The DSM is a virtual machine that operates on decimal values. Each DSM instance
 * includes a set of read/write variable registers, a set of read-only constant
 * registers, and a program that uses a single operand stack to read and write
 * the registers and perform computations.
 *
 * The client creates a DSM by specifying a program in Decimal Stack Machine Assembly
 * Language (DSMAL); the program is a string containng three sections separated by
 * semicolons:
 *
 * Variables
 *
 * A list of variable names, separated by | characters. Each variable name
 * defines a read-write register which the program's instructions can address by
 * its zero-based index in the list.
 *
 * Constants
 *
 * A list of constant values, separated by | characters. Each constant defines a
 * read-only register which the program's instructions can address by its zero-based
 * index in the list.
 *
 * Instructions
 *
 * A list of instructions which will be run starting from the first instruction.
 * All instructions begin with an uppercase letter, followed by a lowercase letter.
 * Some instructions also include an integer parameter, which appears directly
 * after the two-letter instruction code. There are no delimiters between
 * instructions. The full instruction set is:
 *
 *   Xx       exit the program successfully
 *   Ju#      jump unconditionally to instruction #
 *   Jn#      pop a; if a != 0, jump to instruction #
 *   Jz#      pop a; if a == 0, jump to instruction #
 *   Lc#      push constants[#]
 *   Lv#      push variables[#]
 *   Lr       push random value
 *   Lz       push 0
 *   Lo       push 1
 *   St#      pop a; set variables[#] = a
 *   Cp       peek a; push a
 *   Pp       pop
 *   Nt       pop a; push 0 if a != 0, 1 otherwise
 *   Ng       pop a; push -a
 *   Ab       pop a; push abs(a)
 *   Cl       pop a; push ceil(a)
 *   Fl       pop a; push floor(a)
 *   Rd       pop a; push round(a)
 *   Eq       pop b; pop a; push 1 if a == b, 0 otherwise
 *   Ne       pop b; pop a; push 1 if a != b, 0 otherwise
 *   Gt       pop b; pop a; push 1 if a > b, 0 otherwise
 *   Ge       pop b; pop a; push 1 if a >= b, 0 otherwise
 *   Ad       pop b; pop a; push a + b
 *   Sb       pop b; pop a; push a - b
 *   Ml       pop b; pop a; push a * b
 *   Dv       pop b; pop a; push a / b
 *   Pw       pop b; pop a; push pow(a, b)
 *   Mn       pop b; pop a; push min(a, b)
 *   Mx       pop b; pop a; push max(a, b)
 *
 * Shortest valid (though useless) DSMAL program:
 *
 *   ";;Xx"
 *
 *   This program has no variables or constants, and simply exits immediately
 *
 * Compute the area of a circle from its radius:
 *
 *   "radius|area;3.14;Lv0CpMlLc0MlSt1Xx"
 *
 *   Instruction                           Stack                   Variables
 *
 *                                                                 2, 0
 *   Lv0  (load variable 0, aka radius)    2,                      2, 0
 *   Cp   (copy top value)                 2, 2                    2, 0
 *   Ml   (multiply top 2 values)          4                       2, 0
 *   Lc0  (load constant 0)                4, 3.14                 2, 0
 *   Ml   (multiply top 2 values)          12.56                   2, 0
 *   St1  (set variable 1, aka area)                               2, 12.56
 *   Xx   (exit)                                                   2, 12.56
 *
 * Running a program is done as follows:
 *
 *    program = Program(dsmal)
 *    machine = program.machine(foo=1)     # baseline variable values are initialized using keyword args
 *    machine.random_number_iterator = ... # sets the iterator used to generate random numbers
 *    machine.instruction_limit = 5000     # modifies default runtime limit
 *    machine['bar'] = '10.01'             # modifies variable value but not baseline
 *    machine.run()
 *    return machine['baz']
 *
 * The machine instance can be restored to its baseline variable values, with
 * additional modifications applied as follows:
 *
 *    machine.reset(bar='99.99')  # reset to baseline, then set bar
 *
 * Variable values are converted to strings when set.
 */


/********** Python **********/

#include "Python.h"
#include "structmember.h"

#define DECLARE_PYTYPEOBJECT(ct, pyt) static PyTypeObject ct##Type = { PyVarObject_HEAD_INIT(NULL, 0) "abysmal.dsm." #pyt, }


/********** libmpdec **********/

#include "mpdecimal.h"

/* MPD_VERSION_HEX was not introduced until 2.4.1 */
#ifndef MPD_VERSION_HEX
#define MPD_VERSION_HEX ((MPD_MAJOR_VERSION << 24) | (MPD_MINOR_VERSION << 16) | (MPD_MICRO_VERSION <<  8))
#endif

#if MPD_VERSION_HEX < 0x02040000
#define VERSION_ERROR(INSTALLED_VERSION) "libmpdec version " INSTALLED_VERSION " is installed"
#pragma message VERSION_ERROR(MPD_VERSION)
#error "libmpdec version 2.4.0 or higher is required"
#endif

#define MPD_Errors_and_overflows (MPD_Errors | MPD_Overflow | MPD_Underflow)

#define mpd_dsmcontext(ctx) mpd_ieee_context((ctx), 128)


/********** DSM constants **********/

#define STACK_SIZE 32U
#define ARENA_SIZE 256U
#define DEFAULT_INSTRUCTION_LIMIT 10000


/********** Global variables **********/

static PyObject* PyModule_dsm = NULL;
static PyObject* PyExc_InvalidProgramError = NULL;
static PyObject* PyExc_ExecutionError = NULL;
static PyObject* PyExc_InstructionLimitExceededError = NULL;
static PyObject* PyUnicode_semicolon = NULL;
static PyObject* PyUnicode_pipe = NULL;


/********** Opcodes **********/

#define OP_EXIT                  0U
#define OP_JUMP_UNCONDITIONAL    1U
#define OP_JUMP_IF_NONZERO       2U
#define OP_JUMP_IF_ZERO          3U
#define OP_LOAD_CONSTANT         4U
#define OP_LOAD_VARIABLE         5U
#define OP_LOAD_RANDOM           6U
#define OP_LOAD_ZERO             7U
#define OP_LOAD_ONE              8U
#define OP_SET_VARIABLE          9U
#define OP_COPY                  10U
#define OP_POP                   11U
#define OP_NOT                   12U
#define OP_NEGATE                13U
#define OP_ABSOLUTE              14U
#define OP_CEILING               15U
#define OP_FLOOR                 16U
#define OP_ROUND                 17U
#define OP_EQUAL                 18U
#define OP_NOT_EQUAL             19U
#define OP_GREATER_THAN          20U
#define OP_GREATER_THAN_OR_EQUAL 21U
#define OP_ADD                   22U
#define OP_SUBTRACT              23U
#define OP_MULTIPLY              24U
#define OP_DIVIDE                25U
#define OP_POWER                 26U
#define OP_MIN                   27U
#define OP_MAX                   28U

typedef struct tag_OpcodeInfo {
    const char* name;
    unsigned char hasParam;
    unsigned char operands;
} OpcodeInfo;

static const OpcodeInfo OPCODE_INFO[29] = {
    { "Xx", 0, 0 }, // OP_EXIT
    { "Ju", 1, 0 }, // OP_JUMP_UNCONDITIONAL
    { "Jn", 1, 1 }, // OP_JUMP_IF_NONZERO
    { "Jz", 1, 1 }, // OP_JUMP_IF_ZERO
    { "Lc", 1, 0 }, // OP_LOAD_CONSTANT
    { "Lv", 1, 0 }, // OP_LOAD_VARIABLE
    { "Lr", 0, 0 }, // OP_LOAD_RANDOM
    { "Lz", 0, 0 }, // OP_LOAD_ZERO
    { "Lo", 0, 0 }, // OP_LOAD_ONE
    { "St", 1, 1 }, // OP_SET_VARIABLE
    { "Cp", 0, 1 }, // OP_COPY
    { "Pp", 0, 1 }, // OP_POP
    { "Nt", 0, 1 }, // OP_NOT
    { "Ng", 0, 1 }, // OP_NEGATE
    { "Ab", 0, 1 }, // OP_ABSOLUTE
    { "Cl", 0, 1 }, // OP_CEILING
    { "Fl", 0, 1 }, // OP_FLOOR
    { "Rd", 0, 1 }, // OP_ROUND
    { "Eq", 0, 2 }, // OP_EQUAL
    { "Ne", 0, 2 }, // OP_NOT_EQUAL
    { "Gt", 0, 2 }, // OP_GREATER_THAN
    { "Ge", 0, 2 }, // OP_GREATER_THAN_OR_EQUAL
    { "Ad", 0, 2 }, // OP_ADD
    { "Sb", 0, 2 }, // OP_SUBTRACT
    { "Ml", 0, 2 }, // OP_MULTIPLY
    { "Dv", 0, 2 }, // OP_DIVIDE
    { "Pw", 0, 2 }, // OP_POWER
    { "Mn", 0, 2 }, // OP_MIN
    { "Mx", 0, 2 }  // OP_MAX
};

// Allows us to use a switch statement on the 2-letter instruction name.
#define OPCODE_NAME_AS_INT(C, c) ((((unsigned int)(C)) << 8) | (unsigned int)(c))


/********** Utilities **********/

#define CHECK(x)                                       do { if (!(x)) { goto cleanup; } } while (0)
#define CHECK_ALLOCATION(x)                            do { if (!(x)) { PyErr_NoMemory(); goto cleanup; } } while (0)
#define CHECK_WITH_OBJECT(x, exc, obj)                 do { if (!(x)) { PyErr_SetObject(exc, obj); goto cleanup; } } while (0)
#define CHECK_WITH_MESSAGE(x, exc, msg)                do { if (!(x)) { PyErr_SetString(exc, msg); goto cleanup; } } while (0)
#define CHECK_WITH_FORMATTED_MESSAGE(x, exc, fmt, ...) do { if (!(x)) { PyErr_Format(exc, fmt, __VA_ARGS__); goto cleanup; } } while (0)


/********** DSMValue **********/

typedef struct tag_DSMValue {
    mpd_t mpd;                      // mpd_t stored in-place
    mpd_uint_t mpdDefaultBuffer[4]; // default static mpd data buffer
    PyObject* str;                  // lazily-created PyUnicode representation
    unsigned char marked;           // used during GC mark-and-sweep
    unsigned char i32Valid;
    unsigned char mpdValid;
    int32_t i32;
} DSMValue;

#define MPD(v) (&(v)->mpd)
#define MPD_DEFAULT_BUFFER_SIZE 4

#define DSMValue_IS_ZERO(v) ((v)->i32Valid ? !(v)->i32 : mpd_iszero(MPD(v)))
#define DSMValid_IS_NEGATIVE(v) ((v)->i32Valid ? ((v)->i32 < 0) : mpd_isnegative(MPD(v)))
#define DSMValue_IS_OBVIOUSLY_ONE(v) ((v)->i32Valid ? ((v)->i32 == 1) : (MPD(v)->len == 1 && MPD(v)->exp == 0 && MPD(v)->data[0] == 1 && !mpd_isnegative(MPD(v))))
#define DSMValue_IS_OBVIOUSLY_TWO(v) ((v)->i32Valid ? ((v)->i32 == 2) : (MPD(v)->len == 1 && MPD(v)->exp == 0 && MPD(v)->data[0] == 2 && !mpd_isnegative(MPD(v))))
#define DSMValue_ARE_OBVIOUSLY_EQUAL(va, vb) ((va == vb) || (va->i32Valid && vb->i32Valid && va->i32 == vb->i32))

#define MAX_INTERNED_DIGIT 9
#define INTERNED_DIGIT(digit) (&INTERNED_DIGITS[9 + (digit)])
#define DECLARE_POS_INTERNED_DIGIT(digit) { { MPD_STATIC | MPD_CONST_DATA,           0, 1, 1, MPD_DEFAULT_BUFFER_SIZE, (INTERNED_DIGIT(digit))->mpdDefaultBuffer  }, { digit, 0, 0, 0 }, NULL, 1, 1, 1, digit }
#define DECLARE_NEG_INTERNED_DIGIT(digit) { { MPD_STATIC | MPD_CONST_DATA | MPD_NEG, 0, 1, 1, MPD_DEFAULT_BUFFER_SIZE, (INTERNED_DIGIT(-digit))->mpdDefaultBuffer }, { digit, 0, 0, 0 }, NULL, 1, 1, 1, -digit }

static DSMValue INTERNED_DIGITS[MAX_INTERNED_DIGIT + 1 + MAX_INTERNED_DIGIT] = {
    DECLARE_NEG_INTERNED_DIGIT(9),
    DECLARE_NEG_INTERNED_DIGIT(8),
    DECLARE_NEG_INTERNED_DIGIT(7),
    DECLARE_NEG_INTERNED_DIGIT(6),
    DECLARE_NEG_INTERNED_DIGIT(5),
    DECLARE_NEG_INTERNED_DIGIT(4),
    DECLARE_NEG_INTERNED_DIGIT(3),
    DECLARE_NEG_INTERNED_DIGIT(2),
    DECLARE_NEG_INTERNED_DIGIT(1),
    DECLARE_POS_INTERNED_DIGIT(0),
    DECLARE_POS_INTERNED_DIGIT(1),
    DECLARE_POS_INTERNED_DIGIT(2),
    DECLARE_POS_INTERNED_DIGIT(3),
    DECLARE_POS_INTERNED_DIGIT(4),
    DECLARE_POS_INTERNED_DIGIT(5),
    DECLARE_POS_INTERNED_DIGIT(6),
    DECLARE_POS_INTERNED_DIGIT(7),
    DECLARE_POS_INTERNED_DIGIT(8),
    DECLARE_POS_INTERNED_DIGIT(9)
};

// Assumes that the DSMValue is zero-initialized.
static void DSMValue_init(DSMValue* v) {
    assert(!MPD(v)->exp);
    assert(!MPD(v)->digits);
    assert(!MPD(v)->len);
    assert(!v->str);
    assert(!v->marked);
    assert(!v->i32Valid);
    assert(!v->mpdValid);
    assert(!v->i32);

    MPD(v)->flags = MPD_STATIC | MPD_STATIC_DATA;
    MPD(v)->alloc = MPD_DEFAULT_BUFFER_SIZE;
    MPD(v)->data = v->mpdDefaultBuffer;
}

// Assumes that the DSMValue was initialized.
static void DSMValue_uninit(DSMValue* v) {
    assert(mpd_isstatic(MPD(v)));
    mpd_del(MPD(v));
    Py_XDECREF(v->str);
}

static int DSMValue_setFromString(DSMValue* v, const char* s, const char* friendlySource, mpd_context_t* ctx, PyObject* exc) {
    assert(mpd_isstatic(MPD(v)));
    assert(!v->str);

    uint32_t mpdStatus = 0;
    v->i32Valid = 0;
    v->mpdValid = 0;
    v->i32 = 0;

    // Set decimal from string (remove trailing zeroes).
    mpd_qset_string(MPD(v), s, ctx, &mpdStatus);
    CHECK_ALLOCATION(!(mpdStatus & MPD_Malloc_error));
    mpd_qreduce(MPD(v), MPD(v), ctx, &mpdStatus);
    CHECK_WITH_FORMATTED_MESSAGE(
        !(mpdStatus & MPD_Errors_and_overflows) && !mpd_isspecial(MPD(v)),
        exc, "invalid %s value \"%s\"", friendlySource, s);
    v->mpdValid = 1;

    // See if we can store the value as an integer.
    if (mpd_isinteger(MPD(v))) {
        mpdStatus = 0;
        v->i32 = mpd_qget_i32(MPD(v), &mpdStatus);
        v->i32Valid = !(mpdStatus & MPD_Invalid_operation);
    }

    return 1;

cleanup:
    return 0;
}

static int DSMValue_setFromLongLong(DSMValue* v, PY_LONG_LONG ll, const char* friendlySource, mpd_context_t* ctx, PyObject* exc) {
    assert(mpd_isstatic(MPD(v)));
    assert(!v->str);

    // If possible, store in i32 and don't promote it to decimal.
    v->mpdValid = 0;
    v->i32Valid = (ll >= INT32_MIN && ll <= INT32_MAX);
    if (v->i32Valid) {
        v->i32Valid = 1;
        v->i32 = (int32_t)ll;
        return 1;
    }

    // If value doesn't fit in i32, promote it to decimal.
    uint32_t mpdStatus = 0;
    v->i32Valid = 0;
    mpd_qset_i64(MPD(v), ll, ctx, &mpdStatus);
    CHECK_ALLOCATION(!(mpdStatus & MPD_Malloc_error));
    CHECK_WITH_FORMATTED_MESSAGE(
        !(mpdStatus & MPD_Errors_and_overflows),
        exc, "invalid %s value %lld", friendlySource, ll);
    assert(!mpd_isspecial(MPD(v)));
    v->mpdValid = 1;
    return 1;

cleanup:
    return 0;
}

// Returns either: the specified value (modified in-place) or an interned digit.
// Assumes that:
//  * the mpdValue field has been set
//  * the mpdValue field is not special (NaN, Inf, -Inf)
//  * the mpdValue field has been finalized (probably implicitly)
//  * the mpdValue field was finalized using the same context passed to us here
static DSMValue* DSMValue_simplify(DSMValue* v, mpd_context_t* ctx) {
    assert(v->mpdValid);
    if (mpd_iszero(MPD(v))) {
        return INTERNED_DIGIT(0);
    }

    // Remove trailing zeroes. Adapted from implementation of mpd_qreduce(),
    // with unneeded error checks removed.
    mpd_ssize_t shift = mpd_trail_zeros(MPD(v));
    mpd_ssize_t maxexp = (ctx->clamp) ? mpd_etop(ctx) : ctx->emax;
    assert(MPD(v)->exp <= maxexp);
    mpd_ssize_t maxshift = maxexp - MPD(v)->exp;
    shift = (shift > maxshift) ? maxshift : shift;
    mpd_qshiftr_inplace(MPD(v), shift);
    MPD(v)->exp += shift;

    // Update the i32Valid/i32 fields.
    if (mpd_isinteger(MPD(v))) {
        uint32_t mpdStatus = 0;
        v->i32 = mpd_qget_i32(MPD(v), &mpdStatus);
        v->i32Valid = !(mpdStatus & MPD_Invalid_operation);
        if (v->i32Valid && v->i32 >= -MAX_INTERNED_DIGIT && v->i32 <= MAX_INTERNED_DIGIT) {
            return INTERNED_DIGIT(v->i32);
        }
    } else {
        v->i32Valid = 0;
    }
    return v;
}

static PyObject* DSMValue_asPyUnicode(DSMValue* v) {
    if (!v->str) {
        if (DSMValue_IS_ZERO(v)) {
            v->str = INTERNED_DIGIT(0)->str;
            Py_INCREF(v->str);
        } else if (v->i32Valid) {
            int64_t i64 = v->i32;
            int negative = i64 < 0;
            char buffer[11]; // longest possible signed 32-bit int is "-2147483647"
            size_t bufferLength = sizeof(buffer);
            char* start = &buffer[bufferLength];
            if (negative) i64 = -i64;
            do {
                int64_t div = i64 / 10;
                int64_t mod = i64 - (div * 10);
                start -= 1;
                *start = (char)((char)mod + '0');
                i64 = div;
            } while (i64);
            if (negative) {
                start -= 1;
                *start = '-';
            }
            v->str = PyUnicode_FromStringAndSize(start, (Py_ssize_t)(bufferLength - (size_t)(start - buffer)));
        } else {
            assert(v->mpdValid);
            mpd_context_t ctx; mpd_dsmcontext(&ctx);
            uint32_t mpdStatus = 0;
            char* formatted = mpd_to_sci(MPD(v), 0);
            if (!formatted) {
                if (mpdStatus & MPD_Malloc_error) {
                    PyErr_NoMemory();
                } else {
                    PyErr_SetString(PyExc_ValueError, "could not format decimal value");
                }
            } else {
                v->str = PyUnicode_FromString(formatted);
                mpd_free(formatted);
            }
        }
    }
    Py_XINCREF(v->str);
    return v->str;
}

//static PyObject* DSMValue_asPyUnicode_(DSMValue* v) {

#define WRITE_CHAR(c) \
    do { \
        start -= 1; \
        *start = (char)(c); \
    } while (0)

#define CHARS_WRITTEN() (bufferLength - (size_t)(start - buffer))

//    if (!v->str) {
//        if (DSMValue_IS_ZERO(v)) {
//            // Represent all forms of zero as "0".
//            v->str = INTERNED_DIGIT(0)->str;
//            Py_INCREF(v->str);
//        } else if (v->i32Valid) {
//            int64_t i64 = v->i32;
//            int negative = i64 < 0;
//            char buffer[11]; // longest possible signed 32-bit int is "-2147483647"
//            size_t bufferLength = sizeof(buffer);
//            char* start = &buffer[bufferLength];
//            if (negative) i64 = -i64;
//            do {
//                int64_t div = i64 / 10;
//                int64_t mod = i64 - (div * 10);
//                WRITE_CHAR(mod + '0');
//                i64 = div;
//            } while (i64);
//            if (negative) WRITE_CHAR('-');
//            v->str = PyUnicode_FromStringAndSize(start, (Py_ssize_t)CHARS_WRITTEN());
//        } else {
//            assert(v->mpdValid);
//            mpd_ssize_t exp = MPD(v)->exp;
//            size_t digits = (size_t)MPD(v)->digits;
//            size_t bufferLength =
//                1 + /* minus sign */
//                1 + /* decimal point */
//                digits +
//                (size_t)(exp >= 0 ? exp : -exp); /* leading or trailing zeroes */
//            char* buffer = (char*)PyMem_Malloc(bufferLength);
//            if (buffer) {
//                char* start = &buffer[bufferLength];
//                // Write any trailing integer zeroes stored in the exponent.
//                for (; exp > 0; exp -= 1) WRITE_CHAR('0');
//                // Invariant: exp <= 0
//                mpd_uint_t* nextWord = MPD(v)->data;
//                size_t coeffDigitsAvailable = 0;
//                mpd_uint_t coeff = 0;
//                for (; digits; digits -= 1, coeffDigitsAvailable -= 1) {
//                    // If we've exhausted the current coefficient word, load the next one.
//                    if (!coeffDigitsAvailable) {
//                        assert(!coeff);
//                        coeff = *nextWord;
//                        nextWord += 1;
//                        coeffDigitsAvailable = MPD_RDIGITS;
//                    }
//                    mpd_uint_t div = coeff / 10;
//                    mpd_uint_t mod = coeff - (div * 10);
//                    if (mod || exp >= 0 || CHARS_WRITTEN()) WRITE_CHAR((char)mod + '0');
//                    coeff = div;
//                    exp += 1;
//                    // If we just wrote the last of > 0 fractional digits, write a decimal point.
//                    if (!exp && CHARS_WRITTEN()) WRITE_CHAR('.');
//                }
//                // Write leading fractional zeroes if necessary.
//                if (exp < 0) {
//                    for (; exp < 0; exp += 1) WRITE_CHAR('0');
//                    WRITE_CHAR('.');
//                }
//                // Invariant: exp == 0
//                // Write leading zero if necessary.
//                if (*start == '.') WRITE_CHAR('0');
//                // Write minus sign prefix if necessary.
//                if (mpd_isnegative(MPD(v))) WRITE_CHAR('-');
//                v->str = PyUnicode_FromStringAndSize(start, (Py_ssize_t)CHARS_WRITTEN());
//                PyMem_Free(buffer);
//            }
//        }
//    }
//    Py_XINCREF(v->str);
//    return v->str;
//}


/********** DSMArenaValue **********/

typedef struct tag_DSMArenaValue {
    DSMValue value;
    struct tag_DSMArenaValue* nextFree;
} DSMArenaValue;


/********** DSMInstruction **********/

typedef struct tag_DSMInstruction {
    unsigned char opcode;
    uint16_t param;
} DSMInstruction;


/********** DSMProgram **********/

typedef struct tag_DSMProgram {
    PyObject_VAR_HEAD

    PyObject* dsmal;

    uint16_t variableCount;
    PyObject* variableNameToSlotDict;

    uint16_t constantCount;
    DSMValue* constants; // NULL if constantCount == 0

    uint16_t instructionCount; // >= 1
    DSMInstruction* instructions;
} DSMProgram;
DECLARE_PYTYPEOBJECT(DSMProgram, Program);

// Forward declarations.
static PyObject* DSMProgram_new(PyTypeObject* type, PyObject* args, PyObject* kwargs);
static void DSMProgram_dealloc(DSMProgram* program);
static int DSMProgram_parseVariableNames(DSMProgram* program, PyObject* sectionStr);
static int DSMProgram_parseConstants(DSMProgram* program, PyObject* sectionStr);
static int DSMProgram_parseInstructions(DSMProgram* program, PyObject* sectionStr);
static PyObject* DSMProgram_reduce(DSMProgram* program);
static PyObject* DSMProgram_createMachine(DSMProgram* program, PyObject* args, PyObject* kwargs);

static PyMethodDef DSMProgram_methods[] = {
    { "__reduce__", (PyCFunction)DSMProgram_reduce, METH_NOARGS, NULL },
    { "machine", (PyCFunction)DSMProgram_createMachine, METH_VARARGS | METH_KEYWORDS, "Returns a new DSM machine created from the compiled program, with its baseline variables values set using the passed-in keyword arguments." },
    { NULL }
};

static PyMemberDef DSMProgram_members[] = {
    { "dsmal", T_OBJECT, offsetof(DSMProgram, dsmal), READONLY },
    { NULL }
};

static int DSMProgram_initType(void) {
    DSMProgramType.tp_doc = "DSM program objects";
    DSMProgramType.tp_basicsize = sizeof(DSMProgram);
    DSMProgramType.tp_flags = Py_TPFLAGS_DEFAULT;
    DSMProgramType.tp_new = DSMProgram_new;
    DSMProgramType.tp_dealloc = (destructor)DSMProgram_dealloc;
    DSMProgramType.tp_methods = DSMProgram_methods;
    DSMProgramType.tp_members = DSMProgram_members;
    return PyType_Ready(&DSMProgramType) >= 0;
}


/********** DSMMachine **********/

typedef struct tag_DSMMachine {
    PyObject_VAR_HEAD

    DSMProgram* program;
    PyObject* randomNumberIterator;
    unsigned long instructionLimit;

    size_t arenaInitialized;
    DSMArenaValue* nextFreeArenaValue;
    DSMArenaValue arena[ARENA_SIZE];

    size_t stackUsed;
    DSMValue* stack[STACK_SIZE];

    // Actual allocated length of the array is 2 x variableCount (see DSMProgram_createMachine()).
    // Slots [0 : variableCount - 1] are the current variable values.
    // Slots [variableCount : variableCount * 2 - 1] are the baseline variable values.
    DSMValue* variables[1];
} DSMMachine;
DECLARE_PYTYPEOBJECT(DSMMachine, Machine);

// Forward declarations.
static void DSMMachine_dealloc(DSMMachine* machine);
static DSMValue* DSMMachine_allocateArenaValue(DSMMachine* machine, DSMValue* gcRoot1, DSMValue* gcRoot2);
static DSMValue* DSMMachine_createValueFromPythonObject(DSMMachine* machine, PyObject* obj, const char* friendlySource, mpd_context_t* ctx, PyObject* exc);
static PyObject* DSMMachine_reset(DSMMachine* machine, PyObject* args, PyObject* kwargs);
static PyObject* DSMMachine_subscript(DSMMachine* machine, PyObject* key);
static int DSMMachine_ass_subscript(DSMMachine* machine, PyObject* key, PyObject* value);
static Py_ssize_t DSMMachine_len(DSMMachine* machine);
static PyObject* DSMMachine_run_(DSMMachine* machine, int coverage);
static PyObject* DSMMachine_run(DSMMachine* machine, PyObject* dummy_args);
static PyObject* DSMMachine_runWithCoverage(DSMMachine* machine, PyObject* dummy_args);

static PyMemberDef DSMMachine_members[] = {
    { "program", T_OBJECT, offsetof(DSMMachine, program), READONLY },
    { "instruction_limit", T_ULONG, offsetof(DSMMachine, instructionLimit), 0 },
    { "random_number_iterator", T_OBJECT, offsetof(DSMMachine, randomNumberIterator), 0 },
    { NULL }
};

static PyMethodDef DSMMachine_methods[] = {
    { "reset", (PyCFunction)DSMMachine_reset, METH_VARARGS | METH_KEYWORDS, "Resets the machine variables to their baseline values.\n\nReturns the machine to allow method chaining." },
    { "run", (PyCFunction)DSMMachine_run, METH_NOARGS, "Runs the machine.\n\nReturns the number of instructions that were executed before the program terminated." },
    { "run_with_coverage", (PyCFunction)DSMMachine_runWithCoverage, METH_NOARGS, "Runs the machine.\n\nReturns a coverage tuple." },
    { NULL }
};

static PyMappingMethods DSMMachine_mapping_methods = {
    (lenfunc)DSMMachine_len,                 // mp_length
    (binaryfunc)DSMMachine_subscript,        // mp_subscript
    (objobjargproc)DSMMachine_ass_subscript  // mp_ass_subscript
};

static int DSMMachine_initType(void) {
    DSMMachineType.tp_doc = "DSMMachine objects";
    DSMMachineType.tp_basicsize = sizeof(DSMMachine);
    DSMMachineType.tp_itemsize = sizeof(DSMValue*);
    DSMMachineType.tp_flags = Py_TPFLAGS_DEFAULT;
    // There is no DSMMachine_new(). Machines are only creatable by DSMProgram_createMachine().
    // The tp_new slot in DSMMachineType is NULL, which enforces this restriction.
    DSMMachineType.tp_dealloc = (destructor)DSMMachine_dealloc;
    DSMMachineType.tp_members = DSMMachine_members;
    DSMMachineType.tp_methods = DSMMachine_methods;
    DSMMachineType.tp_as_mapping = &DSMMachine_mapping_methods;
    return PyType_Ready(&DSMMachineType) >= 0;
}


/********** DSMProgram implementation **********/

static PyObject* DSMProgram_new(PyTypeObject* type, PyObject* args, PyObject* kwargs) {

    static char* kwlist[] = {
        "dsmal",
        NULL
    };

    PyObject* result = NULL;

    // References that we are responsible for freeing.
    PyObject* sectionsList = NULL;
    DSMProgram* program = NULL;

    // Get `dsmal` parameter.
    PyObject* dsmal = NULL;
    CHECK(PyArg_ParseTupleAndKeywords(args, kwargs, "O!", kwlist, &PyUnicode_Type, &dsmal));

    // Split program on semicolons.
    sectionsList = PyUnicode_Split(dsmal, PyUnicode_semicolon, -1);
    CHECK(sectionsList);
    CHECK_WITH_MESSAGE(
        PyList_Size(sectionsList) == 3,
        PyExc_InvalidProgramError, "program must have variables, constants, and instructions sections");

    // Allocate the DSMProgram object.
    program = (DSMProgram*)type->tp_alloc(type, 0);
    CHECK(program);

    Py_INCREF(dsmal);
    program->dsmal = dsmal;

    // Note: tp_alloc() zero-initialized the memory for us.
    assert(!program->variableCount);
    assert(!program->variableNameToSlotDict);
    assert(!program->constantCount);
    assert(!program->constants);
    assert(!program->instructionCount);
    assert(!program->instructions);

    // Parse variables.
    PyObject* variablesSectionStr = PyList_GetItem(sectionsList, 0);
    CHECK(variablesSectionStr && DSMProgram_parseVariableNames(program, variablesSectionStr));

    // Parse constants.
    PyObject* constantsSectionStr = PyList_GetItem(sectionsList, 1);
    CHECK(constantsSectionStr && DSMProgram_parseConstants(program, constantsSectionStr));

    // Parse instructions.
    PyObject* instructionsSectionStr = PyList_GetItem(sectionsList, 2);
    CHECK(instructionsSectionStr && DSMProgram_parseInstructions(program, instructionsSectionStr));

    // Success! Transfer ownership of program object to result.
    result = (PyObject*)program;
    program = NULL;

cleanup:
    Py_XDECREF(sectionsList);
    Py_XDECREF(program);
    return result;
}

static void DSMProgram_dealloc(DSMProgram* program) {
    Py_XDECREF(program->dsmal);
    Py_XDECREF(program->variableNameToSlotDict);
    if (program->constants) {
        size_t i;
        for (i = 0; i < program->constantCount; i += 1) {
            DSMValue_uninit(&program->constants[i]);
        }
        PyMem_Free(program->constants);
    }
    if (program->instructions) {
        PyMem_Free(program->instructions);
    }
    Py_TYPE(program)->tp_free(program);
}

static int DSMProgram_parseVariableNames(DSMProgram* program, PyObject* sectionStr) {
    assert(!program->variableCount);
    assert(!program->variableNameToSlotDict);

    int success = 0;
    Py_ssize_t count = 0;
    PyObject* variableNamesList = NULL;

    CHECK(0 == PyUnicode_READY(sectionStr));

    // Allocate dictionary.
    program->variableNameToSlotDict = PyDict_New();
    CHECK(program->variableNameToSlotDict);

    // Note that a totally empty section is allowed.
    if (PyUnicode_GET_LENGTH(sectionStr)) {

        // Split section on pipes.
        variableNamesList = PyUnicode_Split(sectionStr, PyUnicode_pipe, -1);
        CHECK(variableNamesList);

        Py_ssize_t allegedCount = PyList_Size(variableNamesList);
        CHECK_WITH_MESSAGE(
            allegedCount <= UINT16_MAX,
            PyExc_InvalidProgramError, "too many variables");

        for (count = 0; count < allegedCount; count += 1) {
            PyObject* variableNameStr = PyList_GetItem(variableNamesList, count);
            CHECK(0 == PyUnicode_READY(variableNameStr));
            CHECK_WITH_MESSAGE(
                PyUnicode_GET_LENGTH(variableNameStr) > 0,
                PyExc_InvalidProgramError, "invalid variable name \"\"");
            CHECK_WITH_FORMATTED_MESSAGE(
                !PyDict_Contains(program->variableNameToSlotDict, variableNameStr),
                PyExc_InvalidProgramError, "duplicate variable name \"%U\"", variableNameStr);
            int addedToDict = 0;
            PyObject* slotNumber = PyLong_FromLongLong(count);
            if (slotNumber) {
                addedToDict = !PyDict_SetItem(program->variableNameToSlotDict, variableNameStr, slotNumber);
                Py_DECREF(slotNumber);
            }
            CHECK(addedToDict);
        }
    }

    assert(PyDict_Size(program->variableNameToSlotDict) == count);
    success = 1;

cleanup:
    program->variableCount = (uint16_t)count;
    Py_XDECREF(variableNamesList);
    return success;
}

static int DSMProgram_parseConstants(DSMProgram* program, PyObject* sectionStr) {
    assert(!program->constantCount);
    assert(!program->constants);

    int success = 0;
    Py_ssize_t count = 0;
    PyObject* constantsList = NULL;
    mpd_context_t ctx; mpd_dsmcontext(&ctx);

    CHECK(0 == PyUnicode_READY(sectionStr));

    // Note that a totally empty section is allowed.
    if (PyUnicode_GET_LENGTH(sectionStr)) {

        // Split section on pipes.
        constantsList = PyUnicode_Split(sectionStr, PyUnicode_pipe, -1);
        CHECK(constantsList);

        // Allocate constants storage.
        Py_ssize_t allegedCount = PyList_Size(constantsList);
        CHECK_WITH_MESSAGE(
            allegedCount <= UINT16_MAX,
            PyExc_InvalidProgramError, "too many constants");
        program->constants = (DSMValue*)PyMem_Malloc((uint16_t)allegedCount * sizeof(DSMValue));
        CHECK_ALLOCATION(program->constants);
        memset(program->constants, 0, (uint16_t)allegedCount * sizeof(DSMValue));

        // Parse constant values.
        for (count = 0; count < allegedCount; count += 1) {
            PyObject* constantStr = PyList_GetItem(constantsList, count);
            CHECK(0 == PyUnicode_READY(constantStr));
            Py_ssize_t length = PyUnicode_GET_LENGTH(constantStr);
            CHECK_WITH_MESSAGE(length, PyExc_InvalidProgramError, "invalid constant value \"\"");
            const char* constantChars = PyUnicode_AsUTF8(constantStr);
            CHECK(constantChars);
            DSMValue_init(&program->constants[count]); // constants memory is zero-initialized
            program->constants[count].marked = 1; // constant lifetimes are controlled by the program lifetime
            CHECK(DSMValue_setFromString(&program->constants[count], constantChars, "constant", &ctx, PyExc_InvalidProgramError));
        }
    }

    success = 1;

cleanup:
    Py_XDECREF(constantsList);
    program->constantCount = (uint16_t)count;
    return success;
}

static int DSMProgram_parseInstructions(DSMProgram* program, PyObject* sectionStr) {
    assert(!program->instructionCount);
    assert(!program->instructions);

    int success = 0;
    Py_ssize_t count = 0;

    Py_ssize_t inputLength = 0;
    const char* input = PyUnicode_AsUTF8AndSize(sectionStr, &inputLength);
    CHECK(input);
    CHECK_WITH_MESSAGE(
        inputLength,
        PyExc_InvalidProgramError, "program must contain at least one instruction");

    Py_ssize_t i;

    // Count instructions (each uppercase character starts an instruction).
    Py_ssize_t allegedCount = 0;
    for (i = 0; i < inputLength; i += 1) {
        char c = input[i];
        if (c >= 'A' && c <= 'Z') {
            allegedCount += 1;
        }
    }
    CHECK_WITH_MESSAGE(
        allegedCount <= UINT16_MAX,
        PyExc_InvalidProgramError, "too many instructions");

    // Allocate space for instructions.
    if (allegedCount) {
        program->instructions = (DSMInstruction*)PyMem_Malloc((uint16_t)allegedCount * sizeof(DSMInstruction));
        CHECK_ALLOCATION(program->instructions);
        memset(program->instructions, 0, (uint16_t)allegedCount * sizeof(DSMInstruction));
    }

    // Parse instructions.
    char name[3]; name[2] = '\0';
    for (i = 0; i < inputLength;) {
        name[0] = input[i];
        if (!(name[0] >= 'A' && name[0] <= 'Z')) {
            name[1] = '\0';
            PyErr_Format(PyExc_InvalidProgramError, "invalid instruction \"%s\"", name);
            goto cleanup;
        }
        i += 1;

        name[1] = (i < inputLength) ? input[i] : '\0';
        if (!(name[1] >= 'a' && name[1] <= 'z')) {
            name[1] = '\0';
            PyErr_Format(PyExc_InvalidProgramError, "invalid instruction \"%s\"", name);
            goto cleanup;
        }
        i += 1;

        unsigned char opcode;
        switch (OPCODE_NAME_AS_INT(name[0], name[1])) {
            case OPCODE_NAME_AS_INT('X', 'x'): opcode = OP_EXIT; break;
            case OPCODE_NAME_AS_INT('J', 'u'): opcode = OP_JUMP_UNCONDITIONAL; break;
            case OPCODE_NAME_AS_INT('J', 'n'): opcode = OP_JUMP_IF_NONZERO; break;
            case OPCODE_NAME_AS_INT('J', 'z'): opcode = OP_JUMP_IF_ZERO; break;
            case OPCODE_NAME_AS_INT('L', 'c'): opcode = OP_LOAD_CONSTANT; break;
            case OPCODE_NAME_AS_INT('L', 'v'): opcode = OP_LOAD_VARIABLE; break;
            case OPCODE_NAME_AS_INT('L', 'r'): opcode = OP_LOAD_RANDOM; break;
            case OPCODE_NAME_AS_INT('L', 'z'): opcode = OP_LOAD_ZERO; break;
            case OPCODE_NAME_AS_INT('L', 'o'): opcode = OP_LOAD_ONE; break;
            case OPCODE_NAME_AS_INT('S', 't'): opcode = OP_SET_VARIABLE; break;
            case OPCODE_NAME_AS_INT('C', 'p'): opcode = OP_COPY; break;
            case OPCODE_NAME_AS_INT('P', 'p'): opcode = OP_POP; break;
            case OPCODE_NAME_AS_INT('N', 't'): opcode = OP_NOT; break;
            case OPCODE_NAME_AS_INT('N', 'g'): opcode = OP_NEGATE; break;
            case OPCODE_NAME_AS_INT('A', 'b'): opcode = OP_ABSOLUTE; break;
            case OPCODE_NAME_AS_INT('C', 'l'): opcode = OP_CEILING; break;
            case OPCODE_NAME_AS_INT('F', 'l'): opcode = OP_FLOOR; break;
            case OPCODE_NAME_AS_INT('R', 'd'): opcode = OP_ROUND; break;
            case OPCODE_NAME_AS_INT('E', 'q'): opcode = OP_EQUAL; break;
            case OPCODE_NAME_AS_INT('N', 'e'): opcode = OP_NOT_EQUAL; break;
            case OPCODE_NAME_AS_INT('G', 't'): opcode = OP_GREATER_THAN; break;
            case OPCODE_NAME_AS_INT('G', 'e'): opcode = OP_GREATER_THAN_OR_EQUAL; break;
            case OPCODE_NAME_AS_INT('A', 'd'): opcode = OP_ADD; break;
            case OPCODE_NAME_AS_INT('S', 'b'): opcode = OP_SUBTRACT; break;
            case OPCODE_NAME_AS_INT('M', 'l'): opcode = OP_MULTIPLY; break;
            case OPCODE_NAME_AS_INT('D', 'v'): opcode = OP_DIVIDE; break;
            case OPCODE_NAME_AS_INT('P', 'w'): opcode = OP_POWER; break;
            case OPCODE_NAME_AS_INT('M', 'n'): opcode = OP_MIN; break;
            case OPCODE_NAME_AS_INT('M', 'x'): opcode = OP_MAX; break;
            default: CHECK_WITH_FORMATTED_MESSAGE(0, PyExc_InvalidProgramError, "invalid instruction \"%s\"", name);
        }
        uint32_t param = 0;
        if (OPCODE_INFO[opcode].hasParam) {
            for (; i < inputLength; i += 1) {
                char c = input[i];
                if (c >= '0' && c <= '9') {
                    param = (param * 10) + (uint32_t)(c - '0');
                    CHECK_WITH_MESSAGE(
                        param <= UINT16_MAX,
                        PyExc_InvalidProgramError, "instruction parameter is too large");
                } else {
                    break;
                }
            }
            CHECK_WITH_FORMATTED_MESSAGE(
                opcode != OP_LOAD_CONSTANT || param < program->constantCount,
                PyExc_InvalidProgramError, "reference to nonexistent constant slot %u", (unsigned int)param);
            CHECK_WITH_FORMATTED_MESSAGE(
                (opcode != OP_LOAD_VARIABLE && opcode != OP_SET_VARIABLE) || param < program->variableCount,
                PyExc_InvalidProgramError, "reference to nonexistent variable slot %u", (unsigned int)param);
        }
        program->instructions[count].opcode = opcode;
        program->instructions[count].param = (uint16_t)param;
        count += 1;
    }

    assert(count == allegedCount);
    success = 1;

cleanup:
    program->instructionCount = (uint16_t)count;
    return success;
}

static PyObject* DSMProgram_reduce(DSMProgram* program) {
    // Pickle support is implemented by simply serializing to the program string.
    return Py_BuildValue("O(O)", Py_TYPE(program), program->dsmal);
}

static PyObject* DSMProgram_createMachine(DSMProgram* program, PyObject* args, PyObject* kwargs) {
    DSMMachine* machine = NULL;

    CHECK_WITH_MESSAGE(
        !args || !PyTuple_GET_SIZE(args),
        PyExc_TypeError, "machine() does not accept positional parameters");

    // variables: [ current0, current1, ..., currentN, baseline0, baseline1, ..., baselineN ]
    uint16_t variableCount = program->variableCount;
    machine = (DSMMachine*)DSMMachineType.tp_alloc(&DSMMachineType, (((Py_ssize_t)variableCount * 2) - 1));
    CHECK(machine);

    machine->program = program; Py_INCREF(program);
    machine->instructionLimit = DEFAULT_INSTRUCTION_LIMIT;

    // Note: tp_alloc() zero-initialized the memory for us.
    assert(!machine->randomNumberIterator);
    assert(!machine->arenaInitialized);
    assert(!machine->nextFreeArenaValue);
    assert(!machine->stackUsed);

    // Variable values default to zero.
    // We must initialize the baseline variables as well, even though they will get
    // overwritten by the memcpy() at the end of this function; this is because the
    // DSMMachine_ass_subscript() calls below may trigger a GC, which expects that
    // all current and baseline variable slots contain valid pointers.
    size_t i;
    for (i = 0; i < variableCount * 2; i += 1) {
        machine->variables[i] = INTERNED_DIGIT(0);
    }

    // Override variables with passed-in values.
    if (kwargs) {
        PyObject* key = NULL;
        PyObject* value = NULL;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwargs, &pos, &key, &value)) {
            CHECK(!DSMMachine_ass_subscript(machine, key, value));
        }
    }

    // Save the current variable values as the baseline.
    memcpy(machine->variables + variableCount, machine->variables, variableCount * sizeof(DSMValue*));

    return (PyObject*)machine;

cleanup:
    Py_XDECREF(machine);
    return NULL;
}


/********** DSMMachine implementation **********/

static void DSMMachine_dealloc(DSMMachine* machine) {
    size_t i;
    for (i = 0; i < machine->arenaInitialized; i += 1) {
        DSMValue_uninit(&machine->arena[i].value);
    }
    Py_DECREF(machine->program);
    Py_XDECREF(machine->randomNumberIterator);
    Py_TYPE(machine)->tp_free(machine);
}

#ifdef ABYSMAL_TRACE
static void DSMValue_println(DSMValue* v) {
    if (v->i32Valid) {
        printf("%i | ", v->i32);
    } else {
        printf("~ | ");
    }
    if (v->mpdValid) {
        mpd_print(MPD(v));
    } else {
        printf("~\n");
    }
}

static void DSMMachine_printStack(DSMMachine* machine) {
    printf("STACK\n");
    if (!machine->stackUsed) {
        printf("  empty\n");
    } else {
        size_t i;
        for (i = 0; i < machine->stackUsed; i += 1) {
            printf("  %zu: ", i); DSMValue_println(machine->stack[i]);
        }
    }
    printf("\n");
}

static void DSMMachine_printVariables(DSMMachine* machine) {
    printf("VARIABLES\n");
    if (!machine->program->variableCount) {
        printf("  empty\n");
    } else {
        size_t i;
        for (i = 0; i < machine->program->variableCount; i += 1) {
            printf("  %zu: ", i); DSMValue_println(machine->variables[i]);
        }
    }
    printf("\n");
}

static void DSMMachine_printBaseline(DSMMachine* machine) {
    printf("BASELINE\n");
    if (!machine->program->variableCount) {
        printf("  empty\n");
    } else {
        size_t i;
        for (i = 0; i < machine->program->variableCount; i += 1) {
            printf("  %zu: ", i); DSMValue_println(machine->variables[machine->program->variableCount + i]);
        }
    }
    printf("\n");
}

static void DSMMachine_printProgram(DSMMachine* machine) {
    printf("%s\n", PyUnicode_AsUTF8(machine->program->dsmal));
}
#endif

static DSMValue* DSMMachine_allocateArenaValue(DSMMachine* machine, DSMValue* gcRoot1, DSMValue* gcRoot2) {
    // The first ARENA_SIZE allocations are responsible for initializing the
    // arena slots on demand. The free list is not referenced and remains NULL
    // until all the slots have been allocated the first time.
    if (machine->arenaInitialized < ARENA_SIZE) {
        DSMArenaValue* av = &machine->arena[machine->arenaInitialized];
        DSMValue_init(&av->value); // DSMProgram_createMachine() guarantees the arena value is zero-initialized
        machine->arenaInitialized += 1;
        return &av->value;
    }

    // After all arena slots have been allocated the first time, we have to use
    // the free list to find a slot to allocate to the new value. If the free
    // list is empty, we have to do a garbage collection. We use a simple
    // mark-and-sweep collector that marks all values referenced by the stack
    // and current/baseline variables and then sweeps the arena looking for
    // unmarked items. Note that constant and interned values will also be
    // marked by the GC, but since those values are already permanently marked,
    // the marking is effectively a no-op and is not externally observable.
    if (!machine->nextFreeArenaValue) {
        size_t i;

#ifdef ABYSMAL_TRACE
        printf("... collecting garbage ...\n");
        DSMMachine_printVariables(machine);
        DSMMachine_printStack(machine);
#endif

        // Mark.
        if (gcRoot1) gcRoot1->marked = 1;
        if (gcRoot2) gcRoot2->marked = 1;
        for (i = 0; i < machine->stackUsed; i += 1) machine->stack[i]->marked = 1;
        for (i = 0; i < machine->program->variableCount * 2; i += 1) machine->variables[i]->marked = 1;

        // Sweep.
        for (i = 0; i < ARENA_SIZE; i += 1) {
            DSMArenaValue* av = &machine->arena[i];
            if (av->value.marked) {
                // Arena slot contains a value currently referenced by the stack and/or variables.
                av->value.marked = 0;
            } else {
                // Arena slot is no longer in use and can be added to the free list.
                av->nextFree = machine->nextFreeArenaValue;
                machine->nextFreeArenaValue = av;
            }
        }

        // If no slots freed up, we can't allocate a new value.
        if (!machine->nextFreeArenaValue) {
            PyErr_SetString(PyExc_ExecutionError, "ran out of space");
            return NULL;
        }
    }

    // Pop the head off the free list and sanitize its contents before returning.
    DSMArenaValue* av = machine->nextFreeArenaValue;
    machine->nextFreeArenaValue = av->nextFree;
    if (av->value.str) {
        Py_DECREF(av->value.str);
        av->value.str = NULL;
        av->value.i32Valid = 0;
        av->value.mpdValid = 0;
        av->value.i32 = 0;
    }
    return &av->value;
}

static DSMValue* DSMMachine_createValueFromPythonObject(DSMMachine* machine, PyObject* obj, const char* friendlySource, mpd_context_t* ctx, PyObject* exc) {

    // Handle True and False specially.
    if (obj == Py_False) return INTERNED_DIGIT(0);
    if (obj == Py_True) return INTERNED_DIGIT(1);

    // Try parsing the value as a long.
    if (PyLong_Check(obj)) {
        int overflow;
        PY_LONG_LONG ll = PyLong_AsLongLongAndOverflow(obj, &overflow);
        if (!overflow) {
            if (ll == -1LL && PyErr_Occurred()) {
                PyErr_Clear(); // fall through, try parsing from string representation
            } else if (ll >= -MAX_INTERNED_DIGIT && ll <= MAX_INTERNED_DIGIT) {
                return INTERNED_DIGIT(ll);
            } else {
                DSMValue* v = DSMMachine_allocateArenaValue(machine, NULL, NULL); CHECK(v);
                CHECK(DSMValue_setFromLongLong(v, ll, friendlySource, ctx, exc));
                return v;
            }
        }
    }

    // Parse the value from its string representation.
    DSMValue* v = DSMMachine_allocateArenaValue(machine, NULL, NULL); CHECK(v);
    PyObject* str = PyObject_Str(obj); CHECK(str);
    const char* buffer = PyUnicode_AsUTF8(str);
    int success = !!buffer && DSMValue_setFromString(v, buffer, friendlySource, ctx, exc);
    Py_DECREF(str);
    CHECK(success);
    return v;

cleanup:
    return NULL;
}

static PyObject* DSMMachine_reset(DSMMachine* machine, PyObject* args, PyObject* kwargs) {
    CHECK_WITH_MESSAGE(
        !args || !PyTuple_GET_SIZE(args),
        PyExc_TypeError, "reset() does not accept positional parameters");

    // Reset variables to baseline.
    memcpy(machine->variables, machine->variables + machine->program->variableCount, machine->program->variableCount * sizeof(DSMValue*));

    // Override baseline values with passed-in values.
    if (kwargs) {
        PyObject* key = NULL;
        PyObject* value = NULL;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwargs, &pos, &key, &value)) {
            CHECK(!DSMMachine_ass_subscript(machine, key, value));
        }
    }

    Py_INCREF(machine);
    return (PyObject*)machine;

cleanup:
    return NULL;
}

static PyObject* DSMMachine_subscript(DSMMachine* machine, PyObject* key) {
    PyObject* slotNumber = PyDict_GetItem(machine->program->variableNameToSlotDict, key);
    CHECK_WITH_OBJECT(slotNumber, PyExc_KeyError, key);

    size_t idx = PyLong_AsSize_t(slotNumber);
    CHECK_WITH_MESSAGE(
        idx < machine->program->variableCount,
        PyExc_IndexError, "index is out of range");

    return DSMValue_asPyUnicode(machine->variables[idx]);

cleanup:
    return NULL;
}

static int DSMMachine_ass_subscript(DSMMachine* machine, PyObject* key, PyObject* value) {
    PyObject* slotNumber = PyDict_GetItem(machine->program->variableNameToSlotDict, key);
    CHECK_WITH_OBJECT(slotNumber, PyExc_KeyError, key);

    size_t idx = PyLong_AsSize_t(slotNumber);
    CHECK_WITH_MESSAGE(
        idx < machine->program->variableCount,
        PyExc_IndexError, "index is out of range");

    mpd_context_t ctx; mpd_dsmcontext(&ctx);
    DSMValue* v = DSMMachine_createValueFromPythonObject(machine, value, "variable", &ctx, PyExc_ValueError);
    CHECK(v);

    machine->variables[idx] = v;
    return 0;

cleanup:
    return -1;
}

static Py_ssize_t DSMMachine_len(DSMMachine* machine) {
    return machine->program->variableCount;
}

static inline DSMValue* DSMMachine_popStack(DSMMachine* machine) {
    machine->stackUsed -= 1;
    DSMValue* v = machine->stack[machine->stackUsed];
    assert(v->i32Valid || v->mpdValid);
    return v;
}

static inline DSMValue* DSMMachine_peekStack(DSMMachine* machine) {
    DSMValue* v = machine->stack[machine->stackUsed - 1];
    assert(v->i32Valid || v->mpdValid);
    return v;
}

static PyObject* DSMMachine_run_(DSMMachine* machine, int coverage) {
    size_t instructionLimit = (size_t)machine->instructionLimit;
    uint16_t instructionCount = machine->program->instructionCount;

    PyObject* result = NULL;
    PyObject* randomNumberIterator = NULL; // lazily initialized

    mpd_context_t ctx; mpd_dsmcontext(&ctx);
    uint32_t mpdStatus = 0;

    unsigned long instructionsExecuted = 0;
    size_t pc = 0;
    const DSMInstruction* instruction = NULL;

    unsigned char* coverageStats = NULL;
    if (coverage) {
        coverageStats = (unsigned char*)PyMem_Malloc(instructionCount);
        CHECK_ALLOCATION(coverageStats);
        memset(coverageStats, 0, instructionCount);
    }

    assert(machine->stackUsed == 0);

#define PUSH(v) \
    do {                  \
        DSMValue* _tmp = (v); \
        assert(_tmp); \
        assert(_tmp->i32Valid || _tmp->mpdValid); \
        CHECK_WITH_MESSAGE(machine->stackUsed != STACK_SIZE, PyExc_ExecutionError, "ran out of stack"); \
        machine->stack[machine->stackUsed] = _tmp; \
        machine->stackUsed += 1; \
    } while (0)

// POP() and PEEK() cannot be pure macros because they are used as expressions and have side effects
#define POP() DSMMachine_popStack(machine)
#define PEEK() DSMMachine_peekStack(machine)

#define ALLOC() DSMMachine_allocateArenaValue(machine, NULL, NULL)
#define ALLOC_1(gcRoot1) DSMMachine_allocateArenaValue(machine, gcRoot1, NULL)
#define ALLOC_2(gcRoot1, gcRoot2) DSMMachine_allocateArenaValue(machine, gcRoot1, gcRoot2)

// Initialize a DSMValue's mpd from its i32 (if it's not already initialized)
#define ENSURE_MPD_VALID(v) \
    do { \
        DSMValue* _tmp = (v); \
        assert(_tmp); \
        if (!_tmp->mpdValid) { \
            assert(_tmp->i32Valid); \
            mpdStatus = 0; \
            mpd_qset_i32(MPD(_tmp), _tmp->i32, &ctx, &mpdStatus); \
            CHECK_ALLOCATION(!(mpdStatus & MPD_Malloc_error)); \
            CHECK_WITH_FORMATTED_MESSAGE( \
                !(mpdStatus & MPD_Errors_and_overflows), \
                PyExc_ExecutionError, "could not convert %i from integer to decimal", (int)_tmp->i32); \
            assert(!mpd_isspecial(MPD(_tmp))); \
        } \
    } while (0)

    /* BEGIN EXECUTION */

#ifdef ABYSMAL_TRACE
    printf("\n==================================================\n");
    DSMMachine_printProgram(machine);
    printf("--------------------------------------------------\n");
    DSMMachine_printBaseline(machine);
#endif

    goto execute;

advance:

    pc += 1;

execute:

    CHECK_WITH_FORMATTED_MESSAGE(
        pc < instructionCount,
        PyExc_ExecutionError, "current execution location %zu is out-of-bounds", pc);
    CHECK_WITH_FORMATTED_MESSAGE(
        instructionsExecuted != instructionLimit,
        PyExc_InstructionLimitExceededError, "execution forcibly terminated after %zu instructions", instructionsExecuted);

    instruction = &machine->program->instructions[pc];

#ifdef ABYSMAL_TRACE
    printf("--------------------------------------------------\n");
    DSMMachine_printVariables(machine);
    DSMMachine_printStack(machine);
    printf("TICK %zu: execute %s", instructionsExecuted, OPCODE_INFO[instruction->opcode].name);
    if (OPCODE_INFO[instruction->opcode].hasParam) printf("%u", (unsigned int)instruction->param);
    printf(" at location %zu\n", pc);
#ifdef ABYSMAL_TRACE_INTERACTIVE
    getchar();
#endif
#endif

    if (coverageStats) {
        coverageStats[pc] = 1;
    }
    instructionsExecuted += 1;

    CHECK_WITH_FORMATTED_MESSAGE(
        machine->stackUsed >= OPCODE_INFO[instruction->opcode].operands,
        PyExc_ExecutionError,
        "instruction \"%s\" requires %zu operand(s), but the stack only has %zu",
        OPCODE_INFO[instruction->opcode].name, OPCODE_INFO[instruction->opcode].operands, machine->stackUsed);

    switch (instruction->opcode) {
        case OP_EXIT: {
            goto exit_successfully;
        }

        case OP_JUMP_UNCONDITIONAL: {
            pc = instruction->param;
            goto execute;
        }

        case OP_JUMP_IF_NONZERO: {
            DSMValue* v = POP();
            if (!DSMValue_IS_ZERO(v)) {
                pc = instruction->param;
                goto execute;
            }
            goto advance;
        }

        case OP_JUMP_IF_ZERO: {
            DSMValue* v = POP();
            if (DSMValue_IS_ZERO(v)) {
                pc = instruction->param;
                goto execute;
            }
            goto advance;
        }

        case OP_LOAD_CONSTANT: {
            CHECK_WITH_FORMATTED_MESSAGE(
                instruction->param < machine->program->constantCount,
                PyExc_ExecutionError,
                "execution halted on reference to nonexistent constant slot %u at instruction %zu",
                (unsigned int)instruction->param, pc);
            PUSH(&machine->program->constants[instruction->param]);
            goto advance;
        }

        case OP_LOAD_VARIABLE: {
            CHECK_WITH_FORMATTED_MESSAGE(
                instruction->param < machine->program->variableCount,
                PyExc_ExecutionError,
                "execution halted on reference to nonexistent variable slot %u at instruction %zu",
                (unsigned int)instruction->param, pc);
            PUSH(machine->variables[instruction->param]);
            goto advance;
        }

        case OP_LOAD_RANDOM: {
            if (!randomNumberIterator) {
                randomNumberIterator = machine->randomNumberIterator;
                if (!randomNumberIterator) {
                    randomNumberIterator = PyObject_GetAttrString(PyModule_dsm, "random_number_iterator");
                    if (!randomNumberIterator) {
                        PyErr_Clear(); // ignore missing attribute
                    }
                }
                if (randomNumberIterator) {
                    Py_INCREF(randomNumberIterator); // decref during cleanup
                    CHECK_WITH_MESSAGE(
                        PyIter_Check(randomNumberIterator),
                        PyExc_ExecutionError, "random_number_iterator is not an iterator");
                }
            }
            if (!randomNumberIterator) {
                PUSH(INTERNED_DIGIT(0));
            } else {
                PyObject* random = PyIter_Next(randomNumberIterator);
                if (!random) {
                    // Distinguish between StopIteration and other errors.
                    CHECK(!PyErr_Occurred());
                    CHECK_WITH_MESSAGE(0, PyExc_ExecutionError, "random_number_iterator ran out of values");
                }
                DSMValue* v = DSMMachine_createValueFromPythonObject(machine, random, "random number", &ctx, PyExc_ExecutionError);
                Py_DECREF(random);
                CHECK(v);
                PUSH(v);
            }
            goto advance;
        }

        case OP_LOAD_ZERO: {
            PUSH(INTERNED_DIGIT(0));
            goto advance;
        }

        case OP_LOAD_ONE: {
            PUSH(INTERNED_DIGIT(1));
            goto advance;
        }

        case OP_SET_VARIABLE: {
            CHECK_WITH_FORMATTED_MESSAGE(
                instruction->param < machine->program->variableCount,
                PyExc_ExecutionError,
                "execution halted on reference to nonexistent variable slot %u at instruction %zu",
                (unsigned int)instruction->param, pc);
            machine->variables[instruction->param] = POP();
            goto advance;
        }

        case OP_COPY: {
            PUSH(PEEK());
            goto advance;
        }

        case OP_POP: {
            POP();
            goto advance;
        }

        case OP_NOT: {
            DSMValue* v = POP();
            PUSH(DSMValue_IS_ZERO(v) ? INTERNED_DIGIT(1) : INTERNED_DIGIT(0));
            goto advance;
        }

    negate:
        case OP_NEGATE: {
            DSMValue* va = POP();
            if (va->i32Valid && va->i32 >= -MAX_INTERNED_DIGIT && va->i32 <= MAX_INTERNED_DIGIT) {
                // Negated value can be represented by an interned digit.
                PUSH(INTERNED_DIGIT(-va->i32));
                goto advance;
            }
            DSMValue* vr = ALLOC_1(va); CHECK(vr);
            if (va->i32Valid && va->i32 != INT32_MIN) {
                // Value is an integer that can be safely negated (remember, -INT32_MIN > INT32_MAX).
                vr->i32Valid = 1;
                vr->i32 = -va->i32;
                PUSH(vr);
                goto advance;
            }
            // Value is a decimal.
            ENSURE_MPD_VALID(va);
            mpdStatus = 0;
            mpd_qminus(MPD(vr), MPD(va), &ctx, &mpdStatus);
            CHECK(!(mpdStatus & MPD_Errors_and_overflows));
            assert(!mpd_isspecial(MPD(vr)));
            vr->mpdValid = 1;
            vr = DSMValue_simplify(vr, &ctx);
            PUSH(vr);
            goto advance;
        }

        case OP_ABSOLUTE: {
            DSMValue* v = PEEK();
            if (v->i32Valid) {
                if (v->i32 >= 0) {
                    // Value is already its own absolute value.
                    goto advance;
                }
            } else if (mpd_ispositive(MPD(v))) {
                // Value is already its own absolute value.
                goto advance;
            }
            goto negate;
        }

        case OP_CEILING:
        case OP_FLOOR:
        case OP_ROUND: {
            if (PEEK()->i32Valid) {
                // Value is already its own ceiling/floor/rounded value.
            } else {
                DSMValue* vr = ALLOC(); CHECK(vr);
                DSMValue* va = POP();
                mpdStatus = 0;
                switch (instruction->opcode) {
                    case OP_CEILING: mpd_qceil(MPD(vr), MPD(va), &ctx, &mpdStatus); break;
                    case OP_FLOOR: mpd_qfloor(MPD(vr), MPD(va), &ctx, &mpdStatus); break;
                    case OP_ROUND: mpd_qround_to_int(MPD(vr), MPD(va), &ctx, &mpdStatus); break;
                }
                CHECK(!(mpdStatus & MPD_Errors_and_overflows));
                assert(!mpd_isspecial(MPD(vr)));
                vr->mpdValid = 1;
                vr = DSMValue_simplify(vr, &ctx);
                PUSH(vr);
            }
            goto advance;
        }

        case OP_EQUAL:
        case OP_NOT_EQUAL:
        case OP_GREATER_THAN:
        case OP_GREATER_THAN_OR_EQUAL: {
            DSMValue* vb = POP();
            DSMValue* va = POP();
            int cmp;
            if (va->i32Valid && vb->i32Valid) {
                switch (instruction->opcode) {
                    case OP_EQUAL: cmp = va->i32 == vb->i32; break;
                    case OP_NOT_EQUAL: cmp = va->i32 != vb->i32; break;
                    case OP_GREATER_THAN: cmp = va->i32 > vb->i32; break;
                    case OP_GREATER_THAN_OR_EQUAL: cmp = va->i32 >= vb->i32; break;
                }
            } else {
                ENSURE_MPD_VALID(va);
                ENSURE_MPD_VALID(vb);
                mpdStatus = 0;
                cmp = mpd_qcmp(MPD(va), MPD(vb), &mpdStatus);
                CHECK(!(mpdStatus & MPD_Errors_and_overflows));
                switch (instruction->opcode) {
                    case OP_EQUAL: cmp = !cmp; break;
                    case OP_GREATER_THAN: cmp = (cmp > 0); break;
                    case OP_GREATER_THAN_OR_EQUAL: cmp = (cmp >= 0); break;
                }
            }
            PUSH(cmp ? INTERNED_DIGIT(1) : INTERNED_DIGIT(0));
            goto advance;
        }

        case OP_ADD: {
            DSMValue* vb = POP();
            if (DSMValue_IS_ZERO(vb)) {
                // a + 0 = a
                goto advance;
            }
            DSMValue* va = POP();
            if (DSMValue_IS_ZERO(va)) {
                // 0 + b = b
                PUSH(vb);
                goto advance;
            }
            DSMValue* vr = ALLOC_2(va, vb); CHECK(vr);
            if (va->i32Valid && vb->i32Valid) {
                int64_t i64out = (int64_t)va->i32 + (int64_t)vb->i32;
                vr->i32Valid = i64out >= INT32_MIN && i64out <= INT32_MAX;
                vr->i32 = (int32_t)i64out;
                mpdStatus = 0;
                mpd_qset_i64(MPD(vr), i64out, &ctx, &mpdStatus);
                CHECK_ALLOCATION(!(mpdStatus & MPD_Malloc_error));
                CHECK_WITH_FORMATTED_MESSAGE(
                    !(mpdStatus & MPD_Errors_and_overflows),
                    PyExc_ExecutionError, "could not convert %ll from integer to decimal", (PY_LONG_LONG)i64out);
                assert(!mpd_isspecial(MPD(vr)));
                vr->mpdValid = 1;
            } else {
                ENSURE_MPD_VALID(va);
                ENSURE_MPD_VALID(vb);
                mpdStatus = 0;
                mpd_qadd(MPD(vr), MPD(va), MPD(vb), &ctx, &mpdStatus);
                CHECK(!(mpdStatus & MPD_Errors_and_overflows));
                assert(!mpd_isspecial(MPD(vr)));
                vr->mpdValid = 1;
                vr = DSMValue_simplify(vr, &ctx);
            }
            PUSH(vr);
            goto advance;
        }

        case OP_SUBTRACT: {
            DSMValue* vb = POP();
            if (DSMValue_IS_ZERO(vb)) {
                // a - 0 = a
                goto advance;
            }
            DSMValue* va = POP();
            if (DSMValue_ARE_OBVIOUSLY_EQUAL(va, vb)) {
                // a - a = 0
                PUSH(INTERNED_DIGIT(0));
                goto advance;
            }
            if (DSMValue_IS_ZERO(va)) {
                // 0 - b = -b
                PUSH(vb);
                goto negate;
            }
            DSMValue* vr = ALLOC_2(va, vb); CHECK(vr);
            if (va->i32Valid && vb->i32Valid) {
                int64_t i64out = (int64_t)va->i32 - (int64_t)vb->i32;
                vr->i32Valid = i64out >= INT32_MIN && i64out <= INT32_MAX;
                vr->i32 = (int32_t)i64out;
                mpdStatus = 0;
                mpd_qset_i64(MPD(vr), i64out, &ctx, &mpdStatus);
                CHECK_ALLOCATION(!(mpdStatus & MPD_Malloc_error));
                CHECK_WITH_FORMATTED_MESSAGE(
                    !(mpdStatus & MPD_Errors_and_overflows),
                    PyExc_ExecutionError, "could not convert %ll from integer to decimal", (PY_LONG_LONG)i64out);
                assert(!mpd_isspecial(MPD(vr)));
                vr->mpdValid = 1;
            } else {
                ENSURE_MPD_VALID(va);
                ENSURE_MPD_VALID(vb);
                mpdStatus = 0;
                mpd_qsub(MPD(vr), MPD(va), MPD(vb), &ctx, &mpdStatus);
                CHECK(!(mpdStatus & MPD_Errors_and_overflows));
                assert(!mpd_isspecial(MPD(vr)));
                vr->mpdValid = 1;
                vr = DSMValue_simplify(vr, &ctx);
            }
            PUSH(vr);
            goto advance;
        }

    multiply:
        case OP_MULTIPLY: {
            DSMValue* vb = POP();
            if (DSMValue_IS_ZERO(vb)) {
                // a * 0 = 0
                POP();
                PUSH(INTERNED_DIGIT(0));
                goto advance;
            }
            if (DSMValue_IS_OBVIOUSLY_ONE(vb)) {
                // a * 1 = a
                goto advance;
            }
            DSMValue* va = POP();
            if (DSMValue_IS_ZERO(va)) {
                // 0 * b = 0
                PUSH(INTERNED_DIGIT(0));
                goto advance;
            }
            if (DSMValue_IS_OBVIOUSLY_ONE(va)) {
                // 1 * b = b
                PUSH(vb);
                goto advance;
            }
            DSMValue* vr = ALLOC_2(va, vb); CHECK(vr);
            if (va->i32Valid && vb->i32Valid) {
                int64_t i64out = (int64_t)va->i32 * (int64_t)vb->i32;
                vr->i32Valid = i64out >= INT32_MIN && i64out <= INT32_MAX;
                vr->i32 = (int32_t)i64out;
                mpdStatus = 0;
                mpd_qset_i64(MPD(vr), i64out, &ctx, &mpdStatus);
                CHECK_ALLOCATION(!(mpdStatus & MPD_Malloc_error));
                CHECK_WITH_FORMATTED_MESSAGE(
                    !(mpdStatus & MPD_Errors_and_overflows),
                    PyExc_ExecutionError, "could not convert %ll from integer to decimal", (PY_LONG_LONG)i64out);
                assert(!mpd_isspecial(MPD(vr)));
                vr->mpdValid = 1;
            } else {
                ENSURE_MPD_VALID(va);
                ENSURE_MPD_VALID(vb);
                mpdStatus = 0;
                mpd_qmul(MPD(vr), MPD(va), MPD(vb), &ctx, &mpdStatus);
                CHECK(!(mpdStatus & MPD_Errors_and_overflows));
                assert(!mpd_isspecial(MPD(vr)));
                vr->mpdValid = 1;
                vr = DSMValue_simplify(vr, &ctx);
            }
            PUSH(vr);
            goto advance;
        }

        case OP_DIVIDE: {
            DSMValue* vb = POP();
            if (DSMValue_IS_ZERO(vb)) {
                // a / 0 = ERROR
                mpdStatus = MPD_Division_by_zero;
                CHECK(0);
            }
            if (DSMValue_IS_OBVIOUSLY_ONE(vb)) {
                // a / 1 = a
                goto advance;
            }
            DSMValue* va = POP();
            if (DSMValue_IS_ZERO(va)) {
                // 0 / b = 0
                PUSH(INTERNED_DIGIT(0));
                goto advance;
            }
            if (DSMValue_ARE_OBVIOUSLY_EQUAL(va, vb)) {
                // a / a = 1
                PUSH(INTERNED_DIGIT(1));
                goto advance;
            }
            DSMValue* vr = ALLOC_2(va, vb); CHECK(vr);
            ENSURE_MPD_VALID(va);
            ENSURE_MPD_VALID(vb);
            mpdStatus = 0;
            mpd_qdiv(MPD(vr), MPD(va), MPD(vb), &ctx, &mpdStatus);
            CHECK(!(mpdStatus & MPD_Errors_and_overflows));
            assert(!mpd_isspecial(MPD(vr)));
            vr->mpdValid = 1;
            vr = DSMValue_simplify(vr, &ctx);
            PUSH(vr);
            goto advance;
        }

        case OP_POWER: {
            DSMValue* vb = POP();
            if (DSMValue_IS_OBVIOUSLY_ONE(vb)) {
                // a ^ 1 = a
                goto advance;
            }
            if (DSMValue_IS_OBVIOUSLY_TWO(vb)) {
                // a ^ 2 = a * a
                PUSH(PEEK());
                goto multiply;
            }
            DSMValue* va = POP();
            if (DSMValue_IS_ZERO(vb)) {
                // 0 ^ 0 = 0
                // a ^ 0 = 1
                PUSH(INTERNED_DIGIT(DSMValue_IS_ZERO(va) ? 0 : 1));
                goto advance;
            }
            if (DSMValue_IS_ZERO(va) && DSMValid_IS_NEGATIVE(vb)) {
                mpdStatus = MPD_Invalid_operation;
                CHECK(0);
            }
            if (DSMValue_IS_OBVIOUSLY_ONE(va)) {
                // 1 ^ b = 1
                PUSH(INTERNED_DIGIT(1));
                goto advance;
            }
            DSMValue* vr = ALLOC_2(va, vb); CHECK(vr);
            ENSURE_MPD_VALID(va);
            ENSURE_MPD_VALID(vb);
            mpdStatus = 0;
            mpd_qpow(MPD(vr), MPD(va), MPD(vb), &ctx, &mpdStatus);
            CHECK(!(mpdStatus & MPD_Errors_and_overflows));
            assert(!mpd_isspecial(MPD(vr)));
            vr->mpdValid = 1;
            vr = DSMValue_simplify(vr, &ctx);
            PUSH(vr);
            goto advance;
        }

        case OP_MIN:
        case OP_MAX: {
            DSMValue* vb = POP();
            DSMValue* va = POP();
            int cmp;
            if (va->i32Valid && vb->i32Valid) {
                cmp = (va->i32 == vb->i32) ? 0 : (va->i32 < vb->i32) ? -1 : 1;
            } else {
                ENSURE_MPD_VALID(va);
                ENSURE_MPD_VALID(vb);
                mpdStatus = 0;
                cmp = mpd_qcmp(MPD(va), MPD(vb), &mpdStatus);
                CHECK(!(mpdStatus & MPD_Errors_and_overflows));
            }
            PUSH((instruction->opcode == OP_MIN) ? ((cmp < 0) ? va : vb) : ((cmp > 0) ? va : vb));
            goto advance;
        }

        // All opcodes have associated case statements, so the default case
        // cannot get hit unless there is a bug somewhere.
        default: {
            CHECK_WITH_FORMATTED_MESSAGE(
                0,
                PyExc_ExecutionError,
                "execution halted on invalid opcode %u at instruction %zu",
                (unsigned int)instruction->opcode, pc);
        }
    }

exit_successfully:

    if (coverage) {
        result = PyTuple_New((Py_ssize_t)instructionCount);
        CHECK(result);
        size_t i;
        for (i = 0; i < instructionCount; i += 1) {
            PyObject* covered = coverageStats[i] ? Py_True : Py_False;
            Py_INCREF(covered); // ref will be stolen by PyTuple_SET_ITEM()
            PyTuple_SET_ITEM(result, (Py_ssize_t)i, covered);
        }
    } else {
        result = PyLong_FromLongLong((long long int)instructionsExecuted);
    }

cleanup:

    if (mpdStatus & MPD_Malloc_error) {
        PyErr_NoMemory();
    } else if (mpdStatus & MPD_Errors_and_overflows) {
        if (mpdStatus & MPD_Overflow) {
            PyErr_Format(PyExc_ExecutionError, "result of %s at instruction %zu was too large", OPCODE_INFO[instruction->opcode].name, pc);
        } else if (mpdStatus & MPD_Underflow) {
            PyErr_Format(PyExc_ExecutionError, "result of %s at instruction %zu was too small", OPCODE_INFO[instruction->opcode].name, pc);
        } else if (mpdStatus & MPD_Errors) {
            PyErr_Format(PyExc_ExecutionError, "illegal %s at instruction %zu", OPCODE_INFO[instruction->opcode].name, pc);
        }

        // Add `instruction` and `opcode` attributes to the raised exception object.
        PyObject* exceptionType;
        PyObject* exceptionValue;
        PyObject* traceback;
        PyErr_Fetch(&exceptionType, &exceptionValue, &traceback);
        PyErr_NormalizeException(&exceptionType, &exceptionValue, &traceback);
        if (traceback) {
            PyException_SetTraceback(exceptionValue, traceback);
        }
        PyObject_SetAttrString(exceptionValue, "instruction", PyLong_FromSize_t(pc));
        PyObject_SetAttrString(exceptionValue, "opcode", PyUnicode_FromString(OPCODE_INFO[instruction->opcode].name));
        PyErr_Restore(exceptionType, exceptionValue, traceback);
    }

    Py_XDECREF(randomNumberIterator);
    PyMem_Free(coverageStats);

#ifdef ABYSMAL_TRACE
    printf("--------------------------------------------------\n");
    DSMMachine_printVariables(machine);
    DSMMachine_printStack(machine);
    printf("PROGRAM HALTED\n");
    printf("==================================================\n");
#endif

    machine->stackUsed = 0;
    return result;
}

static PyObject* DSMMachine_run(DSMMachine* machine, PyObject* dummy_args) {
    (void)dummy_args; // unused
    return DSMMachine_run_(machine, 0/*coverage*/);
}

static PyObject* DSMMachine_runWithCoverage(DSMMachine* machine, PyObject* dummy_args) {
    (void)dummy_args; // unused
    return DSMMachine_run_(machine, 1/*coverage*/);
}


/********** module initialization **********/

static PyMethodDef dsm_methods[] = {
    { NULL, NULL, 0, NULL }
};

PyMODINIT_FUNC PyInit_dsm(void) {
    // Initialize module definition.
    static struct PyModuleDef moduledef = { PyModuleDef_HEAD_INIT, "dsm", "Decimal stack machine", -1, dsm_methods, };
    PyModule_dsm = PyModule_Create(&moduledef);
    CHECK(PyModule_dsm);

    // Allocate global string constants.
    CHECK(PyUnicode_semicolon = PyUnicode_InternFromString(";"));
    CHECK(PyUnicode_pipe = PyUnicode_InternFromString("|"));

    // Initialize interned digits.
#define INIT_INTERNED_DIGIT(digit) \
    do { \
        CHECK(INTERNED_DIGIT(digit)->str = PyUnicode_InternFromString(#digit)); \
    } while (0)
    INIT_INTERNED_DIGIT(-9);
    INIT_INTERNED_DIGIT(-8);
    INIT_INTERNED_DIGIT(-7);
    INIT_INTERNED_DIGIT(-6);
    INIT_INTERNED_DIGIT(-5);
    INIT_INTERNED_DIGIT(-4);
    INIT_INTERNED_DIGIT(-3);
    INIT_INTERNED_DIGIT(-2);
    INIT_INTERNED_DIGIT(-1);
    INIT_INTERNED_DIGIT(0);
    INIT_INTERNED_DIGIT(1);
    INIT_INTERNED_DIGIT(2);
    INIT_INTERNED_DIGIT(3);
    INIT_INTERNED_DIGIT(4);
    INIT_INTERNED_DIGIT(5);
    INIT_INTERNED_DIGIT(6);
    INIT_INTERNED_DIGIT(7);
    INIT_INTERNED_DIGIT(8);
    INIT_INTERNED_DIGIT(9);

    // Initialize exception types.
#define INIT_EXCEPTION(name, base) \
    do { \
        CHECK(PyExc_##name = PyErr_NewException("dsm." #name, PyExc_##base, NULL)); \
        Py_INCREF(PyExc_##name); \
        PyModule_AddObject(PyModule_dsm, #name, PyExc_##name); \
    } while (0)
    INIT_EXCEPTION(InvalidProgramError, ValueError);
    INIT_EXCEPTION(ExecutionError, ValueError);
    INIT_EXCEPTION(InstructionLimitExceededError, ExecutionError);

    // Initialize extension object types.
    CHECK(DSMMachine_initType());
    CHECK(DSMProgram_initType());

    // Add top-level module attributes.
    Py_INCREF(&DSMProgramType); // PyModule_AddObject() steals a ref
    PyModule_AddObject(PyModule_dsm, "Program", (PyObject*)&DSMProgramType);

    return PyModule_dsm;

cleanup:
    return NULL;
}
