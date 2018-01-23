from decimal import Decimal
import itertools
import pickle
import unittest

from abysmal import dsm # pylint:disable=no-name-in-module


class Test_dsm_Program(unittest.TestCase):

    def test_invalid_program_type(self):
        with self.assertRaises(TypeError):
            dsm.Program(None)

    def test_missing_program_section(self):
        for program in ['', 'foo', ';123']:
            with self.assertRaises(dsm.InvalidProgramError) as raised:
                dsm.Program(program)
            self.assertEqual(str(raised.exception), 'program must have variables, constants, and instructions sections')

    def test_program_no_instructions(self):
        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';;')
        self.assertEqual(str(raised.exception), 'program must contain at least one instruction')

    def test_too_many_variables(self):
        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program('|'.join('v{0}'.format(i) for i in range(0, 2 ** 16)) + ';;Xx')
        self.assertEqual(str(raised.exception), 'too many variables')

    def test_invalid_variable_name(self):
        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program('|bar;;Xx')
        self.assertEqual(str(raised.exception), 'invalid variable name ""')

    def test_duplicate_variable_name(self):
        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program('foo|foo;;Xx')
        self.assertEqual(str(raised.exception), 'duplicate variable name "foo"')

    def test_too_many_constants(self):
        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';' + '|'.join('0.{0}'.format(i) for i in range(0, 2 ** 16)) + ';Xx')
        self.assertEqual(str(raised.exception), 'too many constants')

    def test_invalid_constant_slot(self):
        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';;Lc0Xx')
        self.assertEqual(str(raised.exception), 'reference to nonexistent constant slot 0')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';100|200|300;Lc3Xx')
        self.assertEqual(str(raised.exception), 'reference to nonexistent constant slot 3')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';100|200|300;Lc123Xx')
        self.assertEqual(str(raised.exception), 'reference to nonexistent constant slot 123')

    def test_invalid_constant_value(self):
        for v in ['NaN', 'Inf', 'Infinity', '-Inf', '-Infinity']:
            with self.assertRaises(dsm.InvalidProgramError) as raised:
                dsm.Program(';{0};Xx'.format(v))

            self.assertEqual(str(raised.exception), 'invalid constant value "{0}"'.format(v))

        for program in [';|123;Xx', ';123|;Xx']:
            with self.assertRaises(dsm.InvalidProgramError) as raised:
                dsm.Program(program)
            self.assertEqual(str(raised.exception), 'invalid constant value ""')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';bogus;Xx')
        self.assertEqual(str(raised.exception), 'invalid constant value "bogus"')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';123|3.14159|bogus;Xx')
        self.assertEqual(str(raised.exception), 'invalid constant value "bogus"')

    def test_constants(self):
        self.assertEqual(dsm.Program(';0|-0|1|-1|0.3|3.13159|-100000000.0000000001;Xx').machine().run(), 1)

    def test_too_many_instructions(self):
        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';;' + 'Xx' * (2 ** 16))
        self.assertEqual(str(raised.exception), 'too many instructions')

    def test_minimal_program(self):
        self.assertEqual(dsm.Program(';;Xx').machine().run(), 1)

    def test_stack_overflow(self):
        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';;' + ('Lz' * 8000) + 'Xx').machine().run()
        self.assertEqual(str(raised.exception), 'ran out of stack')

    def test_heap_overflow(self):
        with self.assertRaises(dsm.ExecutionError) as raised:
            variable_names = '|'.join(('v' + str(i)) for i in range(1000))
            instructions = ''.join(
                'Lv{0}Lv{1}AdSt{2}'.format(i, i + 1, i + 2) # look, Fibonacci!
                for i in range(998)
            )
            dsm.Program(variable_names + ';;LoSt0LoSt1' + instructions + 'Xx').machine().run()
        self.assertEqual(str(raised.exception), 'ran out of space')

    def test_infinite_loop(self):
        with self.assertRaises(dsm.InstructionLimitExceededError) as raised:
            machine = dsm.Program(';;Ju0').machine()
            machine.instruction_limit = 100
            machine.run()
        self.assertEqual(str(raised.exception), 'execution forcibly terminated after 100 instructions')

    def test_execution_out_of_bounds(self):
        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';123;Lc0').machine().run()
        self.assertEqual(str(raised.exception), 'current execution location 1 is out-of-bounds')

        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';;Ju2Xx').machine().run()
        self.assertEqual(str(raised.exception), 'current execution location 2 is out-of-bounds')

    def test_invalid_instruction(self):
        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';;?')
        self.assertEqual(str(raised.exception), 'invalid instruction "?"')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';;XX')
        self.assertEqual(str(raised.exception), 'invalid instruction "X"')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';;X0')
        self.assertEqual(str(raised.exception), 'invalid instruction "X"')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';;Xy')
        self.assertEqual(str(raised.exception), 'invalid instruction "Xy"')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';;0')
        self.assertEqual(str(raised.exception), 'invalid instruction "0"')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';;01')
        self.assertEqual(str(raised.exception), 'invalid instruction "0"')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';;0X')
        self.assertEqual(str(raised.exception), 'invalid instruction "0"')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';;Ju1Lx')
        self.assertEqual(str(raised.exception), 'invalid instruction "Lx"')

    def test_instruction_parameter_overflow(self):
        for param in ['65536', '123123123123']:
            with self.assertRaises(dsm.InvalidProgramError) as raised:
                dsm.Program(';;Lc' + param)
            self.assertEqual(str(raised.exception), 'instruction parameter is too large')

    def test_invalid_variable_slot(self):
        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program(';;Lv0Xx')
        self.assertEqual(str(raised.exception), 'reference to nonexistent variable slot 0')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program('a|b|c;;Lv3Xx')
        self.assertEqual(str(raised.exception), 'reference to nonexistent variable slot 3')

        with self.assertRaises(dsm.InvalidProgramError) as raised:
            dsm.Program('a|b|c;;Lv123Xx')
        self.assertEqual(str(raised.exception), 'reference to nonexistent variable slot 123')

        with self.assertRaises(KeyError) as raised:
            dsm.Program('a|b|c;;Xx').machine()['d'] = 42
        self.assertEqual(str(raised.exception), "'d'")

    def test_insufficient_operands(self):
        for instruction in ['Jn', 'St', 'Cp', 'Pp', 'Nt', 'Ng', 'Ab', 'Cl', 'Fl', 'Rd']:
            with self.assertRaises(dsm.ExecutionError) as raised:
                dsm.Program('a;;' + instruction).machine().run()
            self.assertEqual(str(raised.exception), 'instruction "{0}" requires 1 operand(s), but the stack only has 0'.format(instruction))

        for instruction in ['Eq', 'Gt', 'Ge', 'Ad', 'Sb', 'Ml', 'Dv', 'Pw', 'Mn', 'Mx']:
            with self.assertRaises(dsm.ExecutionError) as raised:
                dsm.Program('a;;' + instruction).machine().run()
            self.assertEqual(str(raised.exception), 'instruction "{0}" requires 2 operand(s), but the stack only has 0'.format(instruction))

            with self.assertRaises(dsm.ExecutionError) as raised:
                dsm.Program(';123;Lc0' + instruction).machine().run()
            self.assertEqual(str(raised.exception), 'instruction "{0}" requires 2 operand(s), but the stack only has 1'.format(instruction))

    def test_invalid_variable_value(self):
        for v in ['NaN', 'Inf', 'Infinity', '-Inf', '-Infinity']:
            with self.assertRaises(ValueError) as raised:
                dsm.Program('foo;;Xx').machine(foo=v)
            self.assertEqual(str(raised.exception), 'invalid variable value "{0}"'.format(v))

        with self.assertRaises(ValueError) as raised:
            dsm.Program('foo;;Xx').machine(foo='bogus')
        self.assertEqual(str(raised.exception), 'invalid variable value "bogus"')

        with self.assertRaises(ValueError) as raised:
            dsm.Program('foo;;Xx').machine()['foo'] = 'bogus'
        self.assertEqual(str(raised.exception), 'invalid variable value "bogus"')

    def test_variables_positional_parameter(self):
        with self.assertRaises(TypeError) as raised:
            dsm.Program('foo|bar;;Xx').machine(32)
        self.assertEqual(str(raised.exception), 'machine() does not accept positional parameters')

        machine = dsm.Program('foo|bar;;Xx').machine()
        with self.assertRaises(TypeError) as raised:
            machine.reset(32)
        self.assertEqual(str(raised.exception), 'reset() does not accept positional parameters')

    def test_nonexistent_variable_key(self):
        program = dsm.Program('foo;;Xx')
        with self.assertRaises(KeyError) as raised:
            program.machine(bogus=42)
        self.assertEqual(str(raised.exception), repr('bogus'))

        with self.assertRaises(KeyError) as raised:
            program.machine().reset(bogus=42)
        self.assertEqual(str(raised.exception), repr('bogus'))

        for key in ['bogus', 0, object()]:
            with self.assertRaises(KeyError) as raised:
                _ = program.machine()[key]
            self.assertEqual(str(raised.exception), repr(key))
            with self.assertRaises(KeyError) as raised:
                program.machine()[key] = 42
            self.assertEqual(str(raised.exception), repr(key))

    def test_variables(self):
        machine = dsm.Program(';;Xx').machine()
        self.assertEqual(len(machine), 0)

        machine = dsm.Program('foo|bar;;Xx').machine()
        self.assertEqual(machine['foo'], '0')
        self.assertEqual(machine['bar'], '0')
        self.assertEqual(len(machine), 2)

        machine['foo'] = False
        machine['bar'] = True
        self.assertEqual(machine['foo'], '0')
        self.assertEqual(machine['bar'], '1')

        for value in range(-100, 100):
            machine['foo'] = value
            self.assertEqual(machine['foo'], str(value))

        for value in [0, '.0', '-0.0', '0.000']:
            machine['foo'] = value
            self.assertEqual(machine['foo'], '0')

        for value in [-42, -42.0, '-42.000', '-4.20e+1', '-.4200e+2']:
            machine['foo'] = value
            self.assertEqual(machine['foo'], '-42')

        for value in ['3.14159', '3.141590', '3.1415900', '.314159e+1', '.0314159e+2']:
            machine['foo'] = value
            self.assertEqual(machine['foo'], '3.14159')

        machine['foo'] = '123e+13'
        self.assertEqual(machine['foo'], '1.23e+15')

        machine['foo'] = Decimal('-10000000.00000001')
        self.assertEqual(machine['foo'], '-10000000.00000001')

        machine['foo'] = 2 << 65 # overflows an int64_t
        self.assertIn(machine['foo'], ['73786976294838206464'])

    def test_value_roundtrip(self):
        cases = [
            (False, '0'),
            (True, '1'),
            (0, '0'),
            (0.0, '0'),
            ('0', '0'),
            ('0.', '0'),
            ('0.0', '0'),
            ('-0.0', '0'),
            ('0.000', '0'),
            (1, '1'),
            (1.0, '1'),
            ('1', '1'),
            ('1.0', '1'),
            ('1.', '1'),
            ('-1.0', '-1'),
            ('-1.000', '-1'),
            ('0e+0', '0'),
            ('0e-0', '0'),
            ('0e+5', '0'),
            ('0e-5', '0'),
            ('1e+0', '1'),
            ('1e-0', '1'),
            ('1e+5', '100000'),
            ('1e-5', '0.00001'),
            (123, '123'),
            (-123, '-123'),
            ('123.45e-5', '0.0012345'),
            ('123.45e-4', '0.012345'),
            ('123.45e-3', '0.12345'),
            ('123.45e-2', '1.2345'),
            ('123.45e-1', '12.345'),
            ('123.45e+0', '123.45'),
            ('123.45e+1', '1234.5'),
            ('123.45e+2', '12345'),
            ('123.45e+3', '123450'),
            ('123.45e+4', '1234500'),
            ('63e9', '6.3E+10'),
            ('1.001002003000000', '1.001002003'),
            ('0.000001e7', '10'),
            ('0.0000010e7', '10'),
            ('0.00000100e7', '10'),
            ('0.000001000e7', '10'),
            (2147483647, '2147483647'),
            (2147483648, '2147483648'),
            (2147483649, '2147483649'),
            (2147483650, '2147483650'),
            (-2147483647, '-2147483647'),
            (-2147483648, '-2147483648'),
            (-2147483649, '-2147483649'),
            (-2147483650, '-2147483650'),
            (18446744073709551616, '18446744073709551616'),
            (18446744073709551617, '18446744073709551617'),
            (-18446744073709551617, '-18446744073709551617'),
            (1234567890123456789012345678901234567890, '1.234567890123456789012345678901235E+39'),
            (1000000000000000000000000000000000, '1E+33'),
        ]
        machine = dsm.Program('value;;Lv0LoAdLoSbSt0Xx').machine()
        for idx, (machine['value'], expected_value) in enumerate(cases, 1):
            self.assertEqual(
                Decimal(machine['value']),
                Decimal(expected_value),
                'case {0} (write/read): {1} != {2}'.format(idx, machine['value'], expected_value)
            )
        for idx, (machine['value'], expected_value) in enumerate(cases, 1):
            machine.run()
            self.assertEqual(
                Decimal(machine['value']),
                Decimal(expected_value),
                'case {0} (write/modify/read): {1} != {2}'.format(idx, machine['value'], expected_value)
            )

    def test_Xx(self):
        self.assertEqual(dsm.Program(';;Xx').machine().run(), 1)

    def test_Ju(self):
        machine = dsm.Program('a;42;Ju3Lc0St0Xx').machine()
        self.assertEqual(machine.run(), 2)
        self.assertEqual(machine['a'], '0')

    def test_Jn(self):
        machine = dsm.Program('a|b;42;Lv0Jn4Lc0St1Xx').machine(a=1)
        self.assertEqual(machine.run(), 3)
        self.assertEqual(machine['a'], '1')
        self.assertEqual(machine['b'], '0')

        machine.reset(a=0)
        self.assertEqual(machine.run(), 5)
        self.assertEqual(machine['a'], '0')
        self.assertEqual(machine['b'], '42')

    def test_Jz(self):
        machine = dsm.Program('a|b;42;Lv0Jz4Lc0St1Xx').machine(a=1)
        self.assertEqual(machine.run(), 5)
        self.assertEqual(machine['a'], '1')
        self.assertEqual(machine['b'], '42')

        machine.reset(a=0)
        self.assertEqual(machine.run(), 3)
        self.assertEqual(machine['a'], '0')
        self.assertEqual(machine['b'], '0')

    def test_Lc(self):
        machine = dsm.Program('a|b;42|3.14;Lc0St0Lc1St1Xx').machine()
        self.assertEqual(machine.run(), 5)
        self.assertEqual(machine['a'], '42')
        self.assertEqual(machine['b'], '3.14')

    def test_Lv(self):
        machine = dsm.Program('a|b;;Lv0St1Xx').machine(a=42)
        self.assertEqual(machine.run(), 3)
        self.assertEqual(machine['a'], '42')
        self.assertEqual(machine['b'], '42')

    def test_Lr(self):
        machine = dsm.Program('a|b|c|d;;LrSt0LrSt1LrSt2LrSt3Xx').machine()

        default_random_number_iterator = dsm.random_number_iterator
        try:
            # No PRNG at all
            del dsm.random_number_iterator
            self.assertEqual(machine.run(), 9)
            self.assertEqual(machine['a'], '0')
            self.assertEqual(machine['b'], '0')
            self.assertEqual(machine['c'], '0')
            self.assertEqual(machine['d'], '0')

            # Default PRNG
            dsm.random_number_iterator = itertools.cycle(['0', '1', '2'])
            self.assertEqual(machine.run(), 9)
            self.assertEqual(machine['a'], '0')
            self.assertEqual(machine['b'], '1')
            self.assertEqual(machine['c'], '2')
            self.assertEqual(machine['d'], '0')
        finally:
            dsm.random_number_iterator = default_random_number_iterator

        # Machine-specific PRNG
        machine.random_number_iterator = itertools.cycle(['0.5', '3.14'])
        self.assertEqual(machine.run(), 9)
        self.assertEqual(machine['a'], '0.5')
        self.assertEqual(machine['b'], '3.14')
        self.assertEqual(machine['c'], '0.5')
        self.assertEqual(machine['d'], '3.14')

        with self.assertRaises(dsm.ExecutionError) as raised:
            machine.random_number_iterator = 42
            machine.run()
        self.assertEqual(str(raised.exception), 'random_number_iterator is not an iterator')

        with self.assertRaises(dsm.ExecutionError) as raised:
            machine.random_number_iterator = iter(['bogus'])
            machine.run()
        self.assertEqual(str(raised.exception), 'invalid random number value "bogus"')

        with self.assertRaises(dsm.ExecutionError) as raised:
            machine.random_number_iterator = iter(['1'])
            machine.run()
        self.assertEqual(str(raised.exception), 'random_number_iterator ran out of values')

        def bad_iterator():
            yield 1
            raise Exception('boom!')

        with self.assertRaises(Exception) as raised:
            machine.random_number_iterator = bad_iterator()
            machine.run()
        self.assertEqual(str(raised.exception), 'boom!')

    def test_Lz(self):
        machine = dsm.Program('a;;LzSt0Xx').machine()
        self.assertEqual(machine.run(), 3)
        self.assertEqual(machine['a'], '0')

    def test_Lo(self):
        machine = dsm.Program('a;;LoSt0Xx').machine()
        self.assertEqual(machine.run(), 3)
        self.assertEqual(machine['a'], '1')

    def test_St(self):
        machine = dsm.Program('a;42;Lc0St0Xx').machine()
        self.assertEqual(machine.run(), 3)
        self.assertEqual(machine['a'], '42')

    def test_Cp(self):
        machine = dsm.Program('a;;Lv0CpAdSt0Xx').machine(a=3)
        self.assertEqual(machine.run(), 5)
        self.assertEqual(machine['a'], '6')

    def test_Pp(self):
        machine = dsm.Program('a;;LoLzPpSt0Xx').machine()
        self.assertEqual(machine.run(), 5)
        self.assertEqual(machine['a'], '1')

    def run_unop_instruction(self, instruction, cases):
        machine = dsm.Program('a|result;;Lv0' + instruction + 'St1Xx').machine()
        for machine['a'], expected_result in cases:
            self.assertEqual(machine.run(), 4)
            self.assertEqual(Decimal(machine['result']), Decimal(expected_result))

    def run_binop_instruction(self, instruction, cases):
        machine = dsm.Program('a|b|result;;Lv0Lv1' + instruction + 'St2Xx').machine()
        for machine['a'], machine['b'], expected_result in cases:
            self.assertEqual(machine.run(), 5)
            self.assertEqual(Decimal(machine['result']), Decimal(expected_result))

    def test_Nt(self):
        self.run_unop_instruction('Nt', [
            ('0', '1'),
            ('0.000000', '1'),
            ('-0', '1'),
            ('-0.0', '1'),
            ('1', '0'),
            ('1.000000', '0'),
            ('42.001', '0'),
            ('-42.001', '0'),
            ('2147483647', '0'),
            ('2147483648', '0'),
            ('2147483649', '0'),
            ('-2147483647', '0'),
            ('-2147483648', '0'),
            ('-2147483649', '0'),
        ])

    def test_Ng(self):
        self.run_unop_instruction('Ng', [
            ('0', '0'),
            ('0.000000', '0'),
            ('-0.0', '0'),
            ('1', '-1'),
            ('1.000000', '-1'),
            ('42.001', '-42.001'),
            ('-42.001', '42.001'),
            ('2147483647', '-2147483647'),
            ('2147483648', '-2147483648'),
            ('2147483649', '-2147483649'),
            ('-2147483647', '2147483647'),
            ('-2147483648', '2147483648'),
            ('-2147483649', '2147483649'),
        ])

    def test_Ab(self):
        self.run_unop_instruction('Ab', [
            ('0', '0'),
            ('0.000000', '0'),
            ('-0.0', '0'),
            ('1', '1'),
            ('1.000000', '1'),
            ('42.001', '42.001'),
            ('-42.001', '42.001'),
            ('2147483647', '2147483647'),
            ('2147483648', '2147483648'),
            ('2147483649', '2147483649'),
            ('-2147483647', '2147483647'),
            ('-2147483648', '2147483648'),
            ('-2147483649', '2147483649'),
        ])

    def test_Cl(self):
        self.run_unop_instruction('Cl', [
            ('0', '0'),
            ('0.000000', '0'),
            ('-0.0', '0'),
            ('1', '1'),
            ('1.000000', '1'),
            ('42.001', '43'),
            ('-42.001', '-42'),
            ('2147483647', '2147483647'),
            ('2147483648', '2147483648'),
            ('2147483649', '2147483649'),
            ('-2147483647', '-2147483647'),
            ('-2147483648', '-2147483648'),
            ('-2147483649', '-2147483649'),
        ])

    def test_Fl(self):
        self.run_unop_instruction('Fl', [
            ('0', '0'),
            ('0.000000', '0'),
            ('-0.0', '0'),
            ('1', '1'),
            ('1.000000', '1'),
            ('42.001', '42'),
            ('-42.001', '-43'),
            ('2147483647', '2147483647'),
            ('2147483648', '2147483648'),
            ('2147483649', '2147483649'),
            ('-2147483647', '-2147483647'),
            ('-2147483648', '-2147483648'),
            ('-2147483649', '-2147483649'),
        ])

    def test_Rd(self):
        self.run_unop_instruction('Rd', [
            ('0', '0'),
            ('0.000000', '0'),
            ('-0.0', '0'),
            ('1', '1'),
            ('1.000000', '1'),
            ('42.001', '42'),
            ('42.9', '43'),
            ('-42.001', '-42'),
            ('-42.9', '-43'),
            ('2147483647', '2147483647'),
            ('2147483648', '2147483648'),
            ('2147483649', '2147483649'),
            ('-2147483647', '-2147483647'),
            ('-2147483648', '-2147483648'),
            ('-2147483649', '-2147483649'),
        ])

    def test_Eq(self):
        self.run_binop_instruction('Eq', [
            ('0', '0', '1'),
            ('0.000000', '0', '1'),
            ('-0.0', '0', '1'),
            ('1', '1', '1'),
            ('1.000000', '1', '1'),
            ('42.001', '42', '0'),
            ('-42.001', '-42', '0'),
            ('1e+500', '1e+400', '0'),
            ('1e+500', '1e+500', '1'),
        ])

    def test_Ne(self):
        self.run_binop_instruction('Ne', [
            ('0', '0', '0'),
            ('0.000000', '0', '0'),
            ('-0.0', '0', '0'),
            ('1', '1', '0'),
            ('1.000000', '1', '0'),
            ('42.001', '42', '1'),
            ('-42.001', '-42', '1'),
            ('1e+500', '1e+400', '1'),
            ('1e+500', '1e+500', '0'),
        ])

    def test_Gt(self):
        self.run_binop_instruction('Gt', [
            ('0', '0', '0'),
            ('0.000000', '0', '0'),
            ('-0.0', '0', '0'),
            ('1', '1', '0'),
            ('1.000000', '1', '0'),
            ('42.001', '42', '1'),
            ('-42.001', '-42', '0'),
            ('1e+500', '1e+400', '1'),
            ('1e+500', '1e+500', '0'),
        ])

    def test_Ge(self):
        self.run_binop_instruction('Ge', [
            ('0', '0', '1'),
            ('0.000000', '0', '1'),
            ('-0.0', '0', '1'),
            ('1', '1', '1'),
            ('1.000000', '1', '1'),
            ('42.001', '42', '1'),
            ('-42.001', '-42', '0'),
            ('1e+500', '1e+400', '1'),
            ('1e+500', '1e+500', '1'),
        ])

    def test_Ad(self):
        self.run_binop_instruction('Ad', [
            ('0', '0', '0'),
            ('42', '0', '42'),
            ('0', '42', '42'),
            ('0.000000', '0', '0'),
            ('-0.0', '0', '0'),
            ('1', '1', '2'),
            ('1.000000', '1', '2'),
            ('42.001', '42', '84.001'),
            ('-42.001', '-42', '-84.001'),
        ])

        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';9999999999999999999999999999999999e+6111|1e+6111;Lc0Lc1AdXx').machine().run()
        self.assertEqual(str(raised.exception), 'result of Ad at instruction 2 was too large')
        self.assertEqual(raised.exception.instruction, 2)
        self.assertEqual(raised.exception.opcode, 'Ad')

    def test_Sb(self):
        self.run_binop_instruction('Sb', [
            ('0', '0', '0'),
            ('0.000000', '0', '0'),
            ('-0.0', '0', '0'),
            ('1', '1', '0'),
            ('42', '9', '33'),
            ('42', '0', '42'),
            ('1.000000', '1', '0'),
            ('42.001', '42', '0.001'),
            ('-42.001', '-42', '-0.001'),
            ('3.14', '0.0', '3.14'),
            ('42', '42', '0'),
            ('0', '42', '-42'),
            ('0', '-3.14', '3.14'),
        ])

        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';9999999999999999999999999999999999e+6111|-1e+6111;Lc0Lc1SbXx').machine().run()
        self.assertEqual(str(raised.exception), 'result of Sb at instruction 2 was too large')
        self.assertEqual(raised.exception.instruction, 2)
        self.assertEqual(raised.exception.opcode, 'Sb')

    def test_Ml(self):
        self.run_binop_instruction('Ml', [
            ('0', '0', '0'),
            ('1.5', '0', '0'),
            ('0', '1.5', '0'),
            ('1.5', '1', '1.5'),
            ('1', '2.5', '2.5'),
            ('0.000000', '0', '0'),
            ('-0.0', '0', '0'),
            ('1', '1', '1'),
            ('1.000000', '1', '1'),
            ('42.001', '42', '1764.042'),
            ('-42.001', '-42', '1764.042'),
        ])

        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';1e+6144|10;Lc0Lc1MlXx').machine().run()
        self.assertEqual(str(raised.exception), 'result of Ml at instruction 2 was too large')
        self.assertEqual(raised.exception.instruction, 2)
        self.assertEqual(raised.exception.opcode, 'Ml')

    def test_Dv(self):
        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';0;Lc0CpDvXx').machine().run()
        self.assertEqual(str(raised.exception), 'illegal Dv at instruction 2')
        self.assertEqual(raised.exception.instruction, 2)
        self.assertEqual(raised.exception.opcode, 'Dv')

        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';5|0;Lc0Lc1DvXx').machine().run()
        self.assertEqual(str(raised.exception), 'illegal Dv at instruction 2')
        self.assertEqual(raised.exception.instruction, 2)
        self.assertEqual(raised.exception.opcode, 'Dv')

        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';5.00000|0.000000;Lc0Lc1DvXx').machine().run()
        self.assertEqual(str(raised.exception), 'illegal Dv at instruction 2')
        self.assertEqual(raised.exception.instruction, 2)
        self.assertEqual(raised.exception.opcode, 'Dv')

        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';5|-0;Lc0Lc1DvXx').machine().run()
        self.assertEqual(str(raised.exception), 'illegal Dv at instruction 2')
        self.assertEqual(raised.exception.instruction, 2)
        self.assertEqual(raised.exception.opcode, 'Dv')

        self.run_binop_instruction('Dv', [
            ('0', '1', '0'),
            ('0', '0.3', '0'),
            ('0.00', '100', '0'),
            ('0.9', '0.1', '9'),
            ('2', '1', '2'),
            ('-3', '1', '-3'),
            ('5', '5.0', '1'),
            ('20', '4', '5'),
            ('1.000000', '1', '1'),
            ('42.001', '42', '1.00002380952380952380952380952381'),
            ('-42.001', '-42', '1.00002380952380952380952380952381'),
        ])

        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';1e+6144|0.1;Lc0Lc1DvXx').machine().run()
        self.assertEqual(str(raised.exception), 'result of Dv at instruction 2 was too large')
        self.assertEqual(raised.exception.instruction, 2)
        self.assertEqual(raised.exception.opcode, 'Dv')

    def test_Pw(self):
        for a, b in [
                ('-1', '-0.5'),
                ('0', '-1'),
                ('0', '-2'),
        ]:
            with self.assertRaises(dsm.ExecutionError) as raised:
                dsm.Program(';{0}|{1};Lc0Lc1PwXx'.format(a, b)).machine().run()
            self.assertEqual(str(raised.exception), 'illegal Pw at instruction 2')
            self.assertEqual(raised.exception.instruction, 2)
            self.assertEqual(raised.exception.opcode, 'Pw')

        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';2e+256;Lc0CpPwXx').machine().run()
        self.assertEqual(str(raised.exception), 'result of Pw at instruction 2 was too large')
        self.assertEqual(raised.exception.instruction, 2)
        self.assertEqual(raised.exception.opcode, 'Pw')

        with self.assertRaises(dsm.ExecutionError) as raised:
            dsm.Program(';2e-256|2e+256;Lc0Lc1PwXx').machine().run()
        self.assertEqual(str(raised.exception), 'result of Pw at instruction 2 was too small')
        self.assertEqual(raised.exception.instruction, 2)
        self.assertEqual(raised.exception.opcode, 'Pw')

        self.run_binop_instruction('Pw', [
            ('0', '0', '0'),
            ('-0.0', '-0.00', '0'),
            ('0', '1', '0'),
            ('3.14', '1', '3.14'),
            ('-3.14', '1', '-3.14'),
            ('1', '1', '1'),
            ('1', '2', '1'),
            ('1', '3.14', '1'),
            ('3', '2', '9'),
            ('2.5', '2', '6.25'),
            ('1.000000', '1', '1'),
            ('2', '3', '8'),
            ('9', '0.5', '3'),
        ])

    def test_Mn(self):
        self.run_binop_instruction('Mn', [
            ('0', '0', '0'),
            ('0.000000', '0', '0'),
            ('-0.0', '0', '0'),
            ('1', '1', '1'),
            ('1.000000', '1', '1'),
            ('42.001', '42', '42'),
            ('-42.001', '-42', '-42.001'),
        ])

    def test_Mx(self):
        self.run_binop_instruction('Mx', [
            ('0', '0', '0'),
            ('0.000000', '0', '0'),
            ('-0.0', '0', '0'),
            ('1', '1', '1'),
            ('1.000000', '1', '1'),
            ('42.001', '42', '42.001'),
            ('-42.001', '-42', '-42'),
        ])

    def test_run(self):
        machine = dsm.Program('radius|area|diameter;3.14|2;Lv0Lv0MlLc0MlSt1Lv0Lc1MlLc0MlSt2Xx').machine(radius=3)
        self.assertEqual(machine.run(), 13)
        self.assertEqual(machine['area'], '28.26')
        self.assertEqual(machine['diameter'], '18.84')

    def test_stress_test(self):
        dsmal = (
            #0|1|  2|  3|  4|   5|  6|  7| 8| 9|10|11| 12|  13|  14|  15| 16| 17| 18|  19|  20| 21| 22
            'x|y|not|neg|abs|ceil|flr|rnd|eq|ne|gt|ge|sum|diff|prod|quot|exp|min|max|rand|zero|one|two;2;' +
            ''.join([
                'Lv0', 'Nt', 'St2',               # not = !x
                'Lv0', 'Ng', 'St3',               # neg = -x
                'Lv0', 'Ab', 'St4',               # abs = ABS(x)
                'Lv0', 'Cl', 'St5',               # ceil = CEILING(x)
                'Lv0', 'Fl', 'St6',               # flr = FLOOR(x)
                'Lv0', 'Rd', 'St7',               # rnd = ROUND(x)
                'Lv0', 'Lv1', 'Eq', 'St8',        # eq = x == y
                'Lv0', 'Lv1', 'Ne', 'St9',        # ne = x != y
                'Lv0', 'Lv1', 'Gt', 'St10',       # gt = x > y
                'Lv0', 'Lv1', 'Ge', 'St11',       # ge = x >= y
                'Lv0', 'Lv1', 'Ad', 'St12',       # sum = x + y
                'Lv0', 'Lv1', 'Sb', 'St13',       # diff = x - y
                'Lv0', 'Lv1', 'Ml', 'St14',       # prod = x * y
                'Lv1', 'Jz52', 'Lv0', 'Lv1',      # quot = y != 0 ? (x / y) : 0
                'Dv', 'Ju53', 'Lz', 'St15',
                'Lz', 'Cp', 'Lv0', 'Eq', 'Jn68',  # exp = 0 not in {x, y} ? (x ^ y) : 0
                'Cp', 'Lv1', 'Eq', 'Jn68',
                'Pp', 'Lv0', 'Lv1', 'Pw',
                'St16',
                'Lv0', 'Lv1', 'Mn', 'St17',       # min = MIN(x, y)
                'Lv0', 'Lv1', 'Mx', 'St18',       # max = MAX(x, y)
                'Lr', 'St19',                     # rand = random!
                'Lz', 'St20',                     # zero = 0
                'Lo', 'St21',                     # one = 1
                'Lc0', 'St22',                    # two = 2

                'Xx',
            ])
        )

        cases = [
            {'x': 0, 'y': 0},
            {'x': 1, 'y': 0},
            {'x': 0, 'y': 1},
            {'x': 1, 'y': 1},
            {'x': 3.14, 'y': 0},
            {'x': 0, 'y': 3.14},
            {'x': 3.14, 'y': 3.14},
        ]

        # Make sure all opcodes are present in the program.
        for opcode in [
                'Xx',
                'Ju', 'Jn', 'Jz',
                'Lc', 'Lv', 'Lr', 'Lz', 'Lo', 'St',
                'Cp', 'Pp',
                'Nt', 'Ng', 'Ab', 'Cl', 'Fl', 'Rd',
                'Eq', 'Ne', 'Gt', 'Ge',
                'Ad', 'Sb', 'Ml', 'Dv', 'Pw',
                'Mn', 'Mx',
        ]:
            self.assertIn(opcode, dsmal)

        # Make sure all instructions are covered by the test matrix.

        def coverage_check():
            machine = dsm.Program(dsmal).machine()
            self.assertNotIn(False, [any(v) for v in zip(*[
                machine.reset(**case).run_with_coverage()
                for case in cases
            ])])

        coverage_check()

        # Run the cases a bunch of times.

        def stress():
            reusable_machine = dsm.Program(dsmal).machine()
            for _ in range(10000):
                for case in cases:

                    # Reusable machine.
                    reusable_machine.reset(**case).run()
                    _ = ( # force variable values to be converted to strings
                        reusable_machine['x'],
                        reusable_machine['y'],
                        reusable_machine['not'],
                        reusable_machine['neg'],
                        reusable_machine['abs'],
                        reusable_machine['ceil'],
                        reusable_machine['flr'],
                        reusable_machine['rnd'],
                        reusable_machine['eq'],
                        reusable_machine['ne'],
                        reusable_machine['gt'],
                        reusable_machine['ge'],
                        reusable_machine['sum'],
                        reusable_machine['diff'],
                        reusable_machine['prod'],
                        reusable_machine['quot'],
                        reusable_machine['exp'],
                        reusable_machine['min'],
                        reusable_machine['max'],
                        reusable_machine['rand'],
                        reusable_machine['zero'],
                        reusable_machine['one'],
                        reusable_machine['two'],
                    )

                    # Single-use machine
                    one_time_machine = dsm.Program(dsmal).machine(**case)
                    one_time_machine.run()
                    _ = ( # force variable values to be converted to strings
                        one_time_machine['x'],
                        one_time_machine['y'],
                        one_time_machine['not'],
                        one_time_machine['neg'],
                        one_time_machine['abs'],
                        one_time_machine['ceil'],
                        one_time_machine['flr'],
                        one_time_machine['rnd'],
                        one_time_machine['eq'],
                        one_time_machine['ne'],
                        one_time_machine['gt'],
                        one_time_machine['ge'],
                        one_time_machine['sum'],
                        one_time_machine['diff'],
                        one_time_machine['prod'],
                        one_time_machine['quot'],
                        one_time_machine['exp'],
                        one_time_machine['min'],
                        one_time_machine['max'],
                        one_time_machine['rand'],
                        one_time_machine['zero'],
                        one_time_machine['one'],
                        one_time_machine['two'],
                    )

        stress()

    def test_coverage(self):
        machine = dsm.Program('a|b|multiply|c;;Lv0Lv1Lv2Jn6AdJu7MlSt3Xx').machine(a=3, b=5, multiply=0)
        self.assertEqual(machine.run_with_coverage(), (True, True, True, True, True, True, False, True, True))
        self.assertEqual(machine['c'], '8')

        machine.reset(multiply=1)
        self.assertEqual(machine.run_with_coverage(), (True, True, True, True, False, False, True, True, True))
        self.assertEqual(machine['c'], '15')

    def test_dsmal_attr(self):
        self.assertEqual(dsm.Program(';;Xx').dsmal, ';;Xx')

    def test_pickle(self):
        program1 = dsm.Program('radius|area|diameter;3.14|2;Lv0Lv0MlLc0MlSt1Lv0Lc1MlLc0MlSt2Xx')
        for protocol in (2, 3, 4):
            if protocol <= pickle.HIGHEST_PROTOCOL:
                p = pickle.dumps(program1, protocol=protocol)
                program2 = pickle.loads(p)
                self.assertEqual(program1.dsmal, program2.dsmal)
