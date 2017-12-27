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

#include "Python.h"
#include "structmember.h"

#define MODULE_NAME dsm

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

#define MODULE_INIT_ERROR NULL

#define DEFINE_EXCEPTION(name, base) \
    do { \
        if (!(PyExc_##name = PyErr_NewException(STRINGIFY(MODULE_NAME) "." #name, PyExc_##base, NULL))) return MODULE_INIT_ERROR; \
        Py_INCREF(PyExc_##name); \
        PyModule_AddObject(PyModule_dsm, #name, PyExc_##name); \
    } while (0)

#define DECLARE_PYTYPEOBJECT_EX(ct, pyt) \
    static PyTypeObject ct##Type = { \
        PyVarObject_HEAD_INIT(NULL, 0) \
        "abysmal." STRINGIFY(MODULE_NAME) "." #pyt, \
    }

#define DECLARE_PYTYPEOBJECT(t) DECLARE_PYTYPEOBJECT_EX(t, t)

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


/********** Configuration **********/

#define STACK_SIZE 32U
#define ARENA_SIZE 256U
#define DEFAULT_INSTRUCTION_LIMIT 10000
#define TRACE_EXECUTION 0


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
#define CHECK_WITH_MESSAGE(x, exc, msg)                do { if (!(x)) { PyErr_SetString(exc, msg); goto cleanup; } } while (0)
#define CHECK_WITH_FORMATTED_MESSAGE(x, exc, fmt, ...) do { if (!(x)) { PyErr_Format(exc, fmt, __VA_ARGS__); goto cleanup; } } while (0)


/********** DSMValue **********/

typedef struct tag_DSMValue {
    mpd_t mpd;                      // mpd_t stored in-place
    mpd_uint_t mpdDefaultBuffer[4]; // default static mpd data buffer
    PyObject* str;                  // lazily-created PyUnicode representation
    int marked;                     // used during GC mark-and-sweep
} DSMValue;

#define MPD(v) (&(v)->mpd)
#define MPD_DEFAULT_BUFFER_SIZE 4

#define INTERN_POS_DIGIT(digit) \
static DSMValue _pos_##digit##_value = { { MPD_STATIC | MPD_CONST_DATA,           0, 1, 1, MPD_DEFAULT_BUFFER_SIZE, _pos_##digit##_value.mpdDefaultBuffer }, { digit, 0, 0, 0 }, NULL, 1 }; \

#define INTERN_NEG_DIGIT(digit) \
static DSMValue _neg_##digit##_value = { { MPD_STATIC | MPD_CONST_DATA | MPD_NEG, 0, 1, 1, MPD_DEFAULT_BUFFER_SIZE, _neg_##digit##_value.mpdDefaultBuffer }, { digit, 0, 0, 0 }, NULL, 1 };

INTERN_POS_DIGIT(0)
INTERN_POS_DIGIT(1) INTERN_NEG_DIGIT(1)
INTERN_POS_DIGIT(2) INTERN_NEG_DIGIT(2)
INTERN_POS_DIGIT(3) INTERN_NEG_DIGIT(3)
INTERN_POS_DIGIT(4) INTERN_NEG_DIGIT(4)
INTERN_POS_DIGIT(5) INTERN_NEG_DIGIT(5)
INTERN_POS_DIGIT(6) INTERN_NEG_DIGIT(6)
INTERN_POS_DIGIT(7) INTERN_NEG_DIGIT(7)
INTERN_POS_DIGIT(8) INTERN_NEG_DIGIT(8)
INTERN_POS_DIGIT(9) INTERN_NEG_DIGIT(9)

#define POS_INTERNED_DIGIT(digit) &_pos_##digit##_value
#define NEG_INTERNED_DIGIT(digit) &_neg_##digit##_value

// Assumes that the DSMValue is zero-initialized.
static void DSMValue_init(DSMValue* v) {
    assert(!MPD(v)->exp);
    assert(!MPD(v)->digits);
    assert(!MPD(v)->len);
    assert(!v->str);
    assert(!v->marked);
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

    int success = 0;
    uint32_t mpdStatus = 0;
    mpd_qset_string(MPD(v), s, ctx, &mpdStatus);
    CHECK_ALLOCATION(!(mpdStatus & MPD_Malloc_error));
    CHECK_WITH_FORMATTED_MESSAGE(
        !(mpdStatus & MPD_Errors) && !mpd_isspecial(MPD(v)),
        exc, "invalid %s value \"%s\"", friendlySource, s);
    success = 1;

cleanup:
    return success;
}

static int DSMValue_setFromLongLong(DSMValue* v, PY_LONG_LONG ll, const char* friendlySource, mpd_context_t* ctx, PyObject* exc) {
    assert(mpd_isstatic(MPD(v)));
    assert(!v->str);

    int success = 0;
    uint32_t mpdStatus = 0;
    mpd_qset_i64(MPD(v), ll, ctx, &mpdStatus);
    CHECK_ALLOCATION(!(mpdStatus & MPD_Malloc_error));
    CHECK_WITH_FORMATTED_MESSAGE(
        !(mpdStatus & MPD_Errors) && !mpd_isspecial(MPD(v)),
        exc, "invalid %s value %lld", friendlySource, ll);
    success = 1;

cleanup:
    return success;
}

static PyObject* DSMValue_asPyUnicode(DSMValue* v) {
    if (!v->str) {
        char* formatted = NULL;
        mpd_ssize_t formattedLength = mpd_to_sci_size(&formatted, MPD(v), 0);
        if (formattedLength < 0) {
            PyErr_NoMemory();
        } else {
            v->str = PyUnicode_FromStringAndSize(formatted, formattedLength);
            mpd_free(formatted);
        }
    }
    Py_XINCREF(v->str);
    return v->str;
}


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

    size_t variableCount;
    PyObject* variableNameToSlotDict;

    size_t constantCount;
    DSMValue* constants;

    size_t instructionCount;
    DSMInstruction* instructions;
} DSMProgram;
DECLARE_PYTYPEOBJECT_EX(DSMProgram, Program);

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
DECLARE_PYTYPEOBJECT_EX(DSMMachine, Machine);

// Forward declarations.
static void DSMMachine_dealloc(DSMMachine* machine);
static DSMValue* DSMMachine_allocateArenaValue(DSMMachine* machine);
static DSMValue* DSMMachine_createValueFromPythonObject(DSMMachine* machine, PyObject* obj, const char* friendlySource, mpd_context_t* ctx, PyObject* exc);
static PyObject* DSMMachine_reset(DSMMachine* machine, PyObject* args, PyObject* kwargs);
static PyObject* DSMMachine_subscript(DSMMachine* machine, PyObject* key);
static int DSMMachine_ass_subscript(DSMMachine* machine, PyObject* key, PyObject* value);
static Py_ssize_t DSMMachine_len(DSMMachine* machine);
static PyObject* DSMMachine_run_(DSMMachine* machine, int coverage);
static PyObject* DSMMachine_run(DSMMachine* machine, PyObject* dummy_args);
static PyObject* DSMMachine_runWithCoverage(DSMMachine* machine, PyObject* dummy_args);
#if TRACE_EXECUTION
static void DSMMachine_dump(DSMMachine* machine);
#endif

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
    program->variableCount = (size_t)count;
    Py_XDECREF(variableNamesList);
    return success;
}

static int DSMProgram_parseConstants(DSMProgram* program, PyObject* sectionStr) {
    assert(!program->constantCount);
    assert(!program->constants);

    int success = 0;
    Py_ssize_t count = 0;
    PyObject* constantsList = NULL;
    mpd_context_t ctx; mpd_defaultcontext(&ctx);

    CHECK(0 == PyUnicode_READY(sectionStr));

    // Note that a totally empty section is allowed.
    if (PyUnicode_GET_LENGTH(sectionStr)) {

        // Split section on pipes.
        constantsList = PyUnicode_Split(sectionStr, PyUnicode_pipe, -1);
        CHECK(constantsList);

        // Allocate constants storage.
        Py_ssize_t allegedCount = PyList_Size(constantsList);
        CHECK_ALLOCATION(program->constants = (DSMValue*)PyMem_Calloc(allegedCount, sizeof(DSMValue)));

        // Parse constant values.
        for (count = 0; count < allegedCount; count += 1) {
            PyObject* constantStr = PyList_GetItem(constantsList, count);
            CHECK(0 == PyUnicode_READY(constantStr));
            Py_ssize_t length = PyUnicode_GET_LENGTH(constantStr);
            CHECK_WITH_MESSAGE(length, PyExc_InvalidProgramError, "invalid constant value \"\"");
            const char* constantChars = PyUnicode_AsUTF8(constantStr);
            CHECK(constantChars);
            DSMValue_init(&program->constants[count]); // PyMem_Calloc() guarantees constants are zero-initialized
            program->constants[count].marked = 1; // constant lifetimes are controlled by the program lifetime
            CHECK(DSMValue_setFromString(&program->constants[count], constantChars, "constant", &ctx, PyExc_InvalidProgramError));
            program->constants[count].str = constantStr; Py_INCREF(constantStr);
        }
    }

    success = 1;

cleanup:
    Py_XDECREF(constantsList);
    program->constantCount = (size_t)count;
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

    Py_ssize_t i;

    // Count instructions (each uppercase character starts an instruction).
    Py_ssize_t allegedCount = 0;
    for (i = 0; i < inputLength; i += 1) {
        char c = input[i];
        if (c >= 'A' && c <= 'Z') {
            allegedCount += 1;
        }
    }

    // Allocate space for instructions.
    if (allegedCount) {
        CHECK_ALLOCATION(program->instructions = (DSMInstruction*)PyMem_Calloc(allegedCount, sizeof(DSMInstruction)));
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
                    param = (param * 10) + (c - '0');
                    CHECK_WITH_MESSAGE(param <= 0xFFFF, PyExc_InvalidProgramError, "instruction parameter is too large");
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
    program->instructionCount = (size_t)count;
    return success;
}

static PyObject* DSMProgram_reduce(DSMProgram* program) {
    // Pickle support is implemented by simply serializing to the program string.
    return Py_BuildValue("O(O)", Py_TYPE(program), program->dsmal);
}

static PyObject* DSMProgram_createMachine(DSMProgram* program, PyObject* args, PyObject* kwargs) {
    if (args && PyTuple_GET_SIZE(args)) {
        PyErr_SetString(PyExc_TypeError, "machine() does not accept positional parameters");
        return NULL;
    }

    // variables: [ current0, current1, ..., currentN, baseline0, baseline1, ..., baselineN ]
    size_t variableCount = program->variableCount;
    DSMMachine* machine = (DSMMachine*)DSMMachineType.tp_alloc(&DSMMachineType, (Py_ssize_t)((variableCount * 2) - 1));
    if (!machine) {
        return NULL;
    }

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
        machine->variables[i] = POS_INTERNED_DIGIT(0);
    }

    // Override variables with passed-in values.
    if (kwargs) {
        PyObject* key = NULL;
        PyObject* value = NULL;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwargs, &pos, &key, &value)) {
            if (0 != DSMMachine_ass_subscript(machine, key, value)) {
                Py_DECREF(machine);
                return NULL;
            }
        }
    }

    // Save the current variable values as the baseline.
    memcpy(machine->variables + variableCount, machine->variables, variableCount * sizeof(DSMValue*));
    return (PyObject*)machine;
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

static DSMValue* DSMMachine_allocateArenaValue(DSMMachine* machine) {
    // The first ARENA_SIZE allocations are responsible for initializing the
    // arena slots on demand. The free list is not referenced and remains NULL
    // until all the slots have been allocated the first time.
    if (machine->arenaInitialized < ARENA_SIZE) {
        DSMArenaValue* hv = &machine->arena[machine->arenaInitialized];
        DSMValue_init(&hv->value); // DSMProgram_createMachine() guarantees the arena value is zero-initialized
        machine->arenaInitialized += 1;
        return &hv->value;
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

#if TRACE_EXECUTION
        printf("GARBAGE COLLECTION\n\n");
        DSMMachine_dump(machine);
#endif

        // Mark.
        for (i = 0; i < machine->stackUsed; i += 1) machine->stack[i]->marked = 1;
        for (i = 0; i < machine->program->variableCount * 2; i += 1) machine->variables[i]->marked = 1;

        // Sweep.
        for (i = 0; i < ARENA_SIZE; i += 1) {
            DSMArenaValue* hv = &machine->arena[i];
            if (hv->value.marked) {
                // Arena slot contains a value currently referenced by the stack and/or variables.
                hv->value.marked = 0;
            } else {
                // Arena slot is no longer in use and can be added to the free list.
                hv->nextFree = machine->nextFreeArenaValue;
                machine->nextFreeArenaValue = hv;
            }
        }

        // If no slots freed up, we can't allocate a new value.
        if (!machine->nextFreeArenaValue) {
            PyErr_SetString(PyExc_ExecutionError, "ran out of space");
            return NULL;
        }
    }

    // Pop the head off the free list and sanitize its contents before returning.
    DSMArenaValue* hv = machine->nextFreeArenaValue;
    machine->nextFreeArenaValue = hv->nextFree;
    if (hv->value.str) {
        Py_DECREF(hv->value.str);
        hv->value.str = NULL;
    }
    return &hv->value;
}

static DSMValue* DSMMachine_createValueFromPythonObject(DSMMachine* machine, PyObject* obj, const char* friendlySource, mpd_context_t* ctx, PyObject* exc) {

    static DSMValue* const INTERNED_DIGITS[] = {
        NEG_INTERNED_DIGIT(9),
        NEG_INTERNED_DIGIT(8),
        NEG_INTERNED_DIGIT(7),
        NEG_INTERNED_DIGIT(6),
        NEG_INTERNED_DIGIT(5),
        NEG_INTERNED_DIGIT(4),
        NEG_INTERNED_DIGIT(3),
        NEG_INTERNED_DIGIT(2),
        NEG_INTERNED_DIGIT(1),
        POS_INTERNED_DIGIT(0),
        POS_INTERNED_DIGIT(1),
        POS_INTERNED_DIGIT(2),
        POS_INTERNED_DIGIT(3),
        POS_INTERNED_DIGIT(4),
        POS_INTERNED_DIGIT(5),
        POS_INTERNED_DIGIT(6),
        POS_INTERNED_DIGIT(7),
        POS_INTERNED_DIGIT(8),
        POS_INTERNED_DIGIT(9)
    };

    // Handle True and False specially.
    if (obj == Py_False) return POS_INTERNED_DIGIT(0);
    if (obj == Py_True) return POS_INTERNED_DIGIT(1);

    // Try parsing the value as a long.
    if (PyLong_Check(obj)) {
        int overflow;
        PY_LONG_LONG ll = PyLong_AsLongLongAndOverflow(obj, &overflow);
        if (!overflow) {
            if (ll == -1LL && PyErr_Occurred()) {
                PyErr_Clear(); // fall through, try parsing from string representation
            } else if (ll >= -9 && ll <= 9) {
                return INTERNED_DIGITS[ll + 9];
            } else {
                DSMValue* v = DSMMachine_allocateArenaValue(machine);
                if (!v || !DSMValue_setFromLongLong(v, ll, friendlySource, ctx, exc)) return NULL;
                return v;
            }
        }
    }

    // Parse the value from its string representation.
    DSMValue* v = DSMMachine_allocateArenaValue(machine);
    if (!v) return NULL;
    PyObject* str = PyObject_Str(obj);
    if (!str) return NULL;
    const char* buffer = PyUnicode_AsUTF8(str);
    int success = !!buffer && DSMValue_setFromString(v, buffer, friendlySource, ctx, exc);
    Py_DECREF(str);
    return success ? v : NULL;
}

static PyObject* DSMMachine_reset(DSMMachine* machine, PyObject* args, PyObject* kwargs) {
    if (args && PyTuple_GET_SIZE(args)) {
        PyErr_SetString(PyExc_TypeError, "reset() does not accept positional parameters");
        return NULL;
    }

    // Reset variables to baseline.
    memcpy(machine->variables, machine->variables + machine->program->variableCount, machine->program->variableCount * sizeof(DSMValue*));

    // Override baseline values with passed-in values.
    if (kwargs) {
        PyObject* key = NULL;
        PyObject* value = NULL;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwargs, &pos, &key, &value)) {
            if (0 != DSMMachine_ass_subscript(machine, key, value)) {
                return NULL;
            }
        }
    }

    Py_INCREF(machine);
    return (PyObject*)machine;
}

static PyObject* DSMMachine_subscript(DSMMachine* machine, PyObject* key) {
    PyObject* slotNumber = PyDict_GetItem(machine->program->variableNameToSlotDict, key);
    if (!slotNumber) {
        PyErr_SetObject(PyExc_KeyError, key);
        return NULL;
    }

    size_t idx = PyLong_AsSize_t(slotNumber);
    if (idx >= machine->program->variableCount) {
        PyErr_SetString(PyExc_IndexError, "index is out of range");
        return NULL;
    }

    return DSMValue_asPyUnicode(machine->variables[idx]);
}

static int DSMMachine_ass_subscript(DSMMachine* machine, PyObject* key, PyObject* value) {
    PyObject* slotNumber = PyDict_GetItem(machine->program->variableNameToSlotDict, key);
    if (!slotNumber) {
        PyErr_SetObject(PyExc_KeyError, key);
        return -1;
    }

    size_t idx = PyLong_AsSize_t(slotNumber);
    if (idx >= machine->program->variableCount) {
        PyErr_SetString(PyExc_IndexError, "index is out of range");
        return -1;
    }

    mpd_context_t ctx; mpd_defaultcontext(&ctx);
    DSMValue* v = DSMMachine_createValueFromPythonObject(machine, value, "variable", &ctx, PyExc_ValueError);
    if (!v) {
        return -1;
    }

    machine->variables[idx] = v;
    return 0;
}

static Py_ssize_t DSMMachine_len(DSMMachine* machine) {
    return machine->program->variableCount;
}

static inline DSMValue* DSMMachine_popStack(DSMMachine* machine) {
    assert(machine->stackUsed);
    machine->stackUsed -= 1;
    return machine->stack[machine->stackUsed];
}

static inline DSMValue* DSMMachine_peekStack(DSMMachine* machine) {
    assert(machine->stackUsed);
    return machine->stack[machine->stackUsed - 1];
}

static PyObject* DSMMachine_run_(DSMMachine* machine, int coverage) {
    size_t instructionLimit = (size_t)machine->instructionLimit;
    size_t instructionCount = machine->program->instructionCount;

    PyObject* result = NULL;
    PyObject* randomNumberIterator = NULL; // lazily initialized

    mpd_context_t ctx; mpd_defaultcontext(&ctx);
    uint32_t mpdStatus = 0;

    unsigned long instructionsExecuted = 0;
    size_t pc = 0;
    const DSMInstruction* instruction = NULL;

    unsigned char* coverageStats = NULL;
    if (coverage && instructionCount) {
        CHECK_ALLOCATION(coverageStats = (unsigned char*)PyMem_Calloc(instructionCount, sizeof(unsigned char)));
    }

    assert(machine->stackUsed == 0);

#define PUSH(v) do { \
    DSMValue* _tmp = (v); \
    assert(_tmp); \
    CHECK_WITH_MESSAGE(machine->stackUsed != STACK_SIZE, PyExc_ExecutionError, "ran out of stack"); \
    machine->stack[machine->stackUsed] = _tmp; \
    machine->stackUsed += 1; \
} while (0)

#define POP() DSMMachine_popStack(machine)

#define PEEK() DSMMachine_peekStack(machine)

    /* BEGIN EXECUTION */

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

    if (coverageStats) {
        coverageStats[pc] = 1;
    }
    instructionsExecuted += 1;
    instruction = &machine->program->instructions[pc];

#if TRACE_EXECUTION
    DSMMachine_dump(machine);
    if (OPCODE_INFO[instruction->opcode].hasParam) {
        printf("\nPC = %zu, OPCODE = %s%u\n\n", pc, OPCODE_INFO[instruction->opcode].name, (unsigned int)instruction->param);
    } else {
        printf("\nPC = %zu, OPCODE = %s\n\n", pc, OPCODE_INFO[instruction->opcode].name);
    }
#endif

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
            pc = mpd_iszero(MPD(POP())) ? pc + 1 : instruction->param;
            goto execute;
        }

        case OP_JUMP_IF_ZERO: {
            pc = mpd_iszero(MPD(POP())) ? instruction->param : pc + 1;
            goto execute;
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
            DSMValue* v;
            if (!randomNumberIterator) {
                v = POS_INTERNED_DIGIT(0);
            } else {
                v = NULL;
                PyObject* random = PyIter_Next(randomNumberIterator);
                CHECK_WITH_MESSAGE(random, PyExc_ExecutionError, "random_number_iterator ran out of values");
                v = DSMMachine_createValueFromPythonObject(machine, random, "random number", &ctx, PyExc_ExecutionError);
                Py_DECREF(random);
                CHECK(v);
            }
            PUSH(v);
            goto advance;
        }

        case OP_LOAD_ZERO: {
            PUSH(POS_INTERNED_DIGIT(0));
            goto advance;
        }

        case OP_LOAD_ONE: {
            PUSH(POS_INTERNED_DIGIT(1));
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
            PUSH(mpd_iszero(MPD(POP())) ? POS_INTERNED_DIGIT(1) : POS_INTERNED_DIGIT(0));
            goto advance;
        }

        case OP_NEGATE:
        case OP_ABSOLUTE:
        case OP_CEILING:
        case OP_FLOOR:
        case OP_ROUND: {
            DSMValue* vout = DSMMachine_allocateArenaValue(machine); CHECK(vout);
            DSMValue* vin = POP();
            mpdStatus = 0;
            switch (instruction->opcode) {
                case OP_NEGATE: mpd_qminus(MPD(vout), MPD(vin), &ctx, &mpdStatus); break;
                case OP_ABSOLUTE: mpd_qabs(MPD(vout), MPD(vin), &ctx, &mpdStatus); break;
                case OP_CEILING: mpd_qceil(MPD(vout), MPD(vin), &ctx, &mpdStatus); break;
                case OP_FLOOR: mpd_qfloor(MPD(vout), MPD(vin), &ctx, &mpdStatus); break;
                case OP_ROUND: mpd_qround_to_int(MPD(vout), MPD(vin), &ctx, &mpdStatus); break;
            }
            CHECK(!(mpdStatus & MPD_Errors));
            PUSH(vout);
            goto advance;
        }

        case OP_EQUAL:
        case OP_NOT_EQUAL:
        case OP_GREATER_THAN:
        case OP_GREATER_THAN_OR_EQUAL: {
            DSMValue* v2 = POP();
            DSMValue* v1 = POP();
            mpdStatus = 0;
            int cmp = mpd_qcmp(MPD(v1), MPD(v2), &mpdStatus);
            CHECK(!(mpdStatus & MPD_Errors));
            switch (instruction->opcode) {
                case OP_EQUAL: cmp = !cmp; break;
                case OP_GREATER_THAN: cmp = (cmp > 0); break;
                case OP_GREATER_THAN_OR_EQUAL: cmp = (cmp >= 0); break;
            }
            PUSH(cmp ? POS_INTERNED_DIGIT(1) : POS_INTERNED_DIGIT(0));
            goto advance;
        }

        case OP_ADD:
        case OP_SUBTRACT:
        case OP_MULTIPLY:
        case OP_DIVIDE:
        case OP_POWER: {
            DSMValue* vout = DSMMachine_allocateArenaValue(machine); CHECK(vout);
            DSMValue* vin2 = POP();
            DSMValue* vin1 = POP();
            mpdStatus = 0;
            switch (instruction->opcode) {
                case OP_ADD: mpd_qadd(MPD(vout), MPD(vin1), MPD(vin2), &ctx, &mpdStatus); break;
                case OP_SUBTRACT: mpd_qsub(MPD(vout), MPD(vin1), MPD(vin2), &ctx, &mpdStatus); break;
                case OP_MULTIPLY: mpd_qmul(MPD(vout), MPD(vin1), MPD(vin2), &ctx, &mpdStatus); break;
                case OP_DIVIDE: mpd_qdiv(MPD(vout), MPD(vin1), MPD(vin2), &ctx, &mpdStatus); break;
                case OP_POWER: mpd_qpow(MPD(vout), MPD(vin1), MPD(vin2), &ctx, &mpdStatus); break;
            }
            CHECK(!(mpdStatus & MPD_Errors));
            PUSH(vout);
            goto advance;
        }

        case OP_MIN:
        case OP_MAX: {
            DSMValue* v2 = POP();
            DSMValue* v1 = POP();
            mpdStatus = 0;
            int cmp = mpd_qcmp(MPD(v1), MPD(v2), &mpdStatus);
            CHECK(!(mpdStatus & MPD_Errors));
            PUSH((instruction->opcode == OP_MIN) ? ((cmp < 0) ? v1 : v2) : ((cmp > 0) ? v1 : v2));
            goto advance;
        }

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
        result = PyLong_FromLongLong(instructionsExecuted);
    }

cleanup:
    if (mpdStatus & MPD_Malloc_error) {
        PyErr_NoMemory();
    } else if (mpdStatus & MPD_Errors) {
        if (mpdStatus & (MPD_Division_by_zero | MPD_Division_impossible | MPD_Division_undefined | MPD_Invalid_operation)) {
            PyErr_Format(PyExc_ExecutionError, "illegal %s at instruction %zu", OPCODE_INFO[instruction->opcode].name, pc);
        } else {
            PyErr_Format(PyExc_ExecutionError, "computation error (status = %u) in %s operation at instruction %zu", (unsigned int)mpdStatus, OPCODE_INFO[instruction->opcode].name, pc);
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

    if (coverageStats) {
        PyMem_Free(coverageStats);
    }
#if TRACE_EXECUTION
    printf("HALT\n\n");
    DSMMachine_dump(machine);
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

#if TRACE_EXECUTION
static void DSMMachine_dump(DSMMachine* machine) {
    size_t i;
    printf("STACK =\n");
    for (i = 0; i < machine->stackUsed; i += 1) {
        printf("  %zu: ", i); mpd_print(MPD(machine->stack[i]));
    }
    printf("VARIABLES =\n");
    for (i = 0; i < machine->program->variableCount; i += 1) {
        printf("  %zu: ", i); mpd_print(MPD(machine->variables[i]));
    }
    printf("BASELINE =\n");
    for (i = 0; i < machine->program->variableCount; i += 1) {
        printf("  %zu: ", i); mpd_print(MPD(machine->variables[machine->program->variableCount + i]));
    }
}
#endif


/********** module initialization **********/

static PyMethodDef dsm_methods[] = {
    { NULL, NULL, 0, NULL }
};

PyMODINIT_FUNC CONCAT(PyInit_, MODULE_NAME)(void) {
    static struct PyModuleDef moduledef = { PyModuleDef_HEAD_INIT, STRINGIFY(MODULE_NAME), "Decimal stack machine", -1, dsm_methods, };
    PyModule_dsm = PyModule_Create(&moduledef);
    if (!PyModule_dsm) return MODULE_INIT_ERROR;

    // Allocate global string constants.
    if (!(PyUnicode_semicolon = PyUnicode_InternFromString(";")) ||
        !(PyUnicode_pipe = PyUnicode_InternFromString("|"))) {
        return MODULE_INIT_ERROR;
    }

    // Initialize exception types.
    DEFINE_EXCEPTION(InvalidProgramError, ValueError);
    DEFINE_EXCEPTION(ExecutionError, ValueError);
    DEFINE_EXCEPTION(InstructionLimitExceededError, ExecutionError);

    // Initialize extension object types.
    if (!DSMMachine_initType() || !DSMProgram_initType()) {
        return MODULE_INIT_ERROR;
    }

    // Add top-level module attributes.
    Py_INCREF(&DSMProgramType); // PyModule_AddObject() steals a ref
    PyModule_AddObject(PyModule_dsm, "Program", (PyObject*)&DSMProgramType);

    return PyModule_dsm;
}
