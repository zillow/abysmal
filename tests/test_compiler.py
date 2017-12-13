from contextlib import contextmanager
from decimal import Decimal
import re
import unittest

import abysmal


ICE_CREAM_VARIABLES = {
    'flavor',
    'scoops',
    'cone',
    'sprinkles',
    'price',
    'tax',
    'total',
    'stampsEarned',
}

ICE_CREAM_CONSTANTS = {
    'VANILLA': 1,
    'CHOCOLATE': 2,
    'STRAWBERRY': 3,
    'SUGAR': 1,
    'WAFFLE': 2,
}


class Test_compile(unittest.TestCase):

    def assert_compiles_to(self, source_code, expected_variable_names, expected_constants, expected_instructions):
        program, source_map = abysmal.compile(source_code, ['x', 'y', 'result'], {'TRUE': True, 'FALSE': False, 'PI': Decimal('3.14159'), 'HALF': 0.5})
        actual_variable_names, actual_constants, actual_instructions = program.dsmal.split(';')
        self.assertEqual(actual_variable_names, expected_variable_names, 'variables section does not match')
        self.assertEqual(actual_constants, expected_constants, 'constants section does not match')
        actual_instructions = list(zip(re.findall(r'[A-Z][a-z]\d*', actual_instructions), source_map))
        self.assertEqual(actual_instructions, expected_instructions, 'instructions section does not match')

    def test_compile_literal_zero(self):
        for zero in ['0', '-0', '0.0', '0%', '0.0%', '0k', '0.0M']:
            self.assert_compiles_to(
                '''\
@start:
    result = ''' + zero,
                'result|x|y',
                '',
                [('Lz', 2), ('St0', 2), ('Xx', None)]
            )

    def test_compile_literal_one(self):
        for one in ['1', '1.0', '1.00000', '100%', '0.001K', '0.000001m']:
            self.assert_compiles_to(
                '''\
@start:
    result = ''' + one,
                'result|x|y',
                '',
                [('Lo', 2), ('St0', 2), ('Xx', None)]
            )

    def test_compile_literal(self):
        for forty_two in ['42', '42.000', '4200.000%', '0.042000k']:
            self.assert_compiles_to(
                '''\
@start:
    result = ''' + forty_two,
                'result|x|y',
                '42',
                [('Lc0', 2), ('St0', 2), ('Xx', None)]
            )

    def test_compile_constant(self):
        self.assert_compiles_to(
            '''\
@start:
    result = PI''',
            'result|x|y',
            '3.14159',
            [('Lc0', 2), ('St0', 2), ('Xx', None)]
        )

    def test_compile_variable(self):
        self.assert_compiles_to(
            '''\
@start:
    result = x''',
            'result|x|y',
            '',
            [('Lv1', 2), ('St0', 2), ('Xx', None)]
        )

    def test_compile_random(self):
        self.assert_compiles_to(
            '''\
@start:
    result = random!''',
            'result|x|y',
            '',
            [('Lr', 2), ('St0', 2), ('Xx', None)]
        )

    def test_compile_source_map(self):
        self.assert_compiles_to(
            '''\
let sum = x + y
let product = x * y

@start:
    x == y => @equal
    result = product > 0 ? product : \\
             product == 0 ? 0 : \\
             sum

@equal:
    result = product''',
            'product|x|y|result|sum',
            '',
            [
                ('Lv1', 1), ('Lv2', 1), ('Ad', 1), ('St4', 1), ('Lv1', 2), ('Lv2', 2), ('Ml', 2), ('St0', 2), ('Lv1', 5),
                ('Lv2', 5), ('Eq', 5), ('Jn27', 5), ('Lv0', (6, 8)), ('Lz', (6, 8)), ('Gt', (6, 8)), ('Jn24', (6, 8)),
                ('Lv0', (6, 8)), ('Lz', (6, 8)), ('Eq', (6, 8)), ('Jn22', (6, 8)), ('Lv4', (6, 8)), ('Ju25', (6, 8)),
                ('Lz', (6, 8)), ('Ju25', (6, 8)), ('Lv0', (6, 8)), ('St3', (6, 8)), ('Xx', None), ('Lv0', 11), ('St3', 11),
                ('Xx', None)
            ]
        )

    def test_compile_unconditional_branch(self):
        self.assert_compiles_to(
            '''\
@start:
    => @end

@end:''',
            'result|x|y',
            '',
            [('Xx', 2)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = y
    => @end

@end:
    result = x''',
            'result|x|y',
            '',
            [('Lv2', 2), ('St0', 2), ('Lv1', 6), ('St0', 6), ('Xx', None)]
        )

    def test_compile_conditional_branch(self):
        self.assert_compiles_to(
            '''\
@start:
    x => @end

@end:''',
            'x|result|y',
            '',
            [('Lv0', 2), ('Jn3', 2), ('Xx', None), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    !x => @end

@end:''',
            'x|result|y',
            '',
            [('Lv0', 2), ('Jz3', 2), ('Xx', None), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = y
    x => @end

@end:
    result = x''',
            'result|x|y',
            '',
            [('Lv2', 2), ('St0', 2), ('Lv1', 3), ('Jn5', 3), ('Xx', None), ('Lv1', 6), ('St0', 6), ('Xx', None)]
        )

    def test_compile_unary_operators(self):
        self.assert_compiles_to(
            '''\
@start:
    result = !x''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Nt', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = -x''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Ng', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = +x''',
            'result|x|y',
            '',
            [('Lv1', 2), ('St0', 2), ('Xx', None)]
        )

    def test_compile_arithmetic(self):
        self.assert_compiles_to(
            '''\
@start:
    result = 1 + 2''',
            'result|x|y',
            '3',
            [('Lc0', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = PI - 2''',
            'result|x|y',
            '1.14159',
            [('Lc0', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = 5 * x''',
            'result|x|y',
            '5',
            [('Lc0', 2), ('Lv1', 2), ('Ml', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x / y''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Lv2', 2), ('Dv', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x ^ 3''',
            'result|x|y',
            '3',
            [('Lv1', 2), ('Lc0', 2), ('Pw', 2), ('St0', 2), ('Xx', None)]
        )

    def test_compile_declared_variable(self):
        self.assert_compiles_to(
            '''\
let DOUBLE_PI = PI * 2
@start:
    result = x + DOUBLE_PI''',
            'result|x|y',
            '6.28318',
            [('Lv1', 3), ('Lc0', 3), ('Ad', 3), ('St0', 3), ('Xx', None)]
        )

    def test_compile_compare(self):
        self.assert_compiles_to(
            '''\
@start:
    result = x == PI''',
            'result|x|y',
            '3.14159',
            [('Lv1', 2), ('Lc0', 2), ('Eq', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x != PI''',
            'result|x|y',
            '3.14159',
            [('Lv1', 2), ('Lc0', 2), ('Ne', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x < PI''',
            'result|x|y',
            '3.14159',
            [('Lc0', 2), ('Lv1', 2), ('Gt', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x <= PI''',
            'result|x|y',
            '3.14159',
            [('Lc0', 2), ('Lv1', 2), ('Ge', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x > PI''',
            'result|x|y',
            '3.14159',
            [('Lv1', 2), ('Lc0', 2), ('Gt', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x >= PI''',
            'result|x|y',
            '3.14159',
            [('Lv1', 2), ('Lc0', 2), ('Ge', 2), ('St0', 2), ('Xx', None)]
        )

    def test_compile_or(self):
        self.assert_compiles_to(
            '''\
@start:
    result = x || y''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Jn6', 2), ('Lv2', 2), ('Jn6', 2), ('Lz', 2), ('Ju7', 2), ('Lo', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x || y * 2 || y''',
            'y|result|x',
            '2',
            [
                ('Lv2', 2), ('Jn10', 2), ('Lv0', 2), ('Lc0', 2), ('Ml', 2), ('Jn10', 2), ('Lv0', 2), ('Jn10', 2),
                ('Lz', 2), ('Ju11', 2), ('Lo', 2), ('St1', 2), ('Xx', None)
            ],
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x || (y * 2 || y)''',
            'y|result|x',
            '2',
            [
                ('Lv2', 2), ('Jn10', 2), ('Lv0', 2), ('Lc0', 2), ('Ml', 2), ('Jn10', 2), ('Lv0', 2), ('Jn10', 2),
                ('Lz', 2), ('Ju11', 2), ('Lo', 2), ('St1', 2), ('Xx', None)
            ],
        )

    def test_compile_and(self):
        self.assert_compiles_to(
            '''\
@start:
    result = x && y''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Jz6', 2), ('Lv2', 2), ('Jz6', 2), ('Lo', 2), ('Ju7', 2), ('Lz', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x && y - 1 && 5''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Jz8', 2), ('Lv2', 2), ('Lo', 2), ('Sb', 2), ('Jz8', 2), ('Lo', 2), ('Ju9', 2), ('Lz', 2), ('St0', 2), ('Xx', None)],
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x && (y - 1 && 5)''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Jz8', 2), ('Lv2', 2), ('Lo', 2), ('Sb', 2), ('Jz8', 2), ('Lo', 2), ('Ju9', 2), ('Lz', 2), ('St0', 2), ('Xx', None)],
        )

    def test_compile_ternary_operator(self):
        self.assert_compiles_to(
            '''\
@start:
    result = x ? y : PI''',
            'result|x|y',
            '3.14159',
            [('Lv1', 2), ('Jn4', 2), ('Lc0', 2), ('Ju5', 2), ('Lv2', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x ? y : x + y ? 4 : 2''',
            'x|y|result',
            '2|4',
            [
                ('Lv0', 2), ('Jn10', 2), ('Lv0', 2), ('Lv1', 2), ('Ad', 2), ('Jn8', 2), ('Lc0', 2), ('Ju11', 2),
                ('Lc1', 2), ('Ju11', 2), ('Lv1', 2), ('St2', 2), ('Xx', None)
            ]
        )

    def test_compile_in(self):
        self.assert_compiles_to(
            '''\
@start:
    result = x in { PI, y }''',
            'result|x|y',
            '3.14159',
            [
                ('Lv1', 2), ('Cp', 2), ('Lc0', 2), ('Eq', 2), ('Jn12', 2), ('Cp', 2), ('Lv2', 2), ('Eq', 2),
                ('Jn12', 2), ('Pp', 2), ('Lz', 2), ('Ju14', 2), ('Pp', 2), ('Lo', 2), ('St0', 2), ('Xx', None)
            ]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = x not in { PI, y }''',
            'result|x|y',
            '3.14159',
            [
                ('Lv1', 2), ('Cp', 2), ('Lc0', 2), ('Eq', 2), ('Jn12', 2), ('Cp', 2), ('Lv2', 2), ('Eq', 2),
                ('Jn12', 2), ('Pp', 2), ('Lo', 2), ('Ju14', 2), ('Pp', 2), ('Lz', 2), ('St0', 2), ('Xx', None)
            ]
        )

    def test_compile_functions(self):
        self.assert_compiles_to(
            '''\
@start:
    result = ABS(x)''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Ab', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = CEILING(x)''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Cl', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = FLOOR(x)''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Fl', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = ROUND(x)''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Rd', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = MIN(x, y)''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Lv2', 2), ('Mn', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = MIN(x, y, 100)''',
            'result|x|y',
            '100',
            [('Lv1', 2), ('Lv2', 2), ('Mn', 2), ('Lc0', 2), ('Mn', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = MAX(x, y)''',
            'result|x|y',
            '',
            [('Lv1', 2), ('Lv2', 2), ('Mx', 2), ('St0', 2), ('Xx', None)]
        )

        self.assert_compiles_to(
            '''\
@start:
    result = MAX(x, y, 100)''',
            'result|x|y',
            '100',
            [('Lv1', 2), ('Lv2', 2), ('Mx', 2), ('Lc0', 2), ('Mx', 2), ('St0', 2), ('Xx', None)]
        )

    def test_compile_fold_constants(self):
        program, _ = abysmal.compile(
            '''\
@start:
    a = 0 || 0 || 1 == 2 || 3 != 3 || 4 < 4 || 5 <= 4 || 6 > 7 || 8 >= 9
    b = 0 || 5 || 0
    c = 5 || 3
    d = 5 && 0
    e = 0 && 5 && 3
    f = 5 && (0 || 3) && 2
    g = 3 + 2
    h = 3 - 2
    i = 3 * 2
    j = 3 / 2
    k = 3 ^ 2
    l = !5
    m = !0
    n = +++5
    o = ---5
    p = ABS(-5)
    q = CEILING(-5.2)
    r = FLOOR(5.2)
    s = MAX(-1, 5, 0)
    t = MIN(-2, 13, 0)
    u = ROUND(5.2)
    v = ROUND(5.8)
    w = 0 ? 3 : 4
    x = 5 ? 3 : 4
    y = 5 in {1, a, 3, b}
    z = 5 not in {1, a, 5, b}
    0 => @deadcode
    1 => @alwaysexecuted

@deadcode:

@alwaysexecuted:
''',
            ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z'],
            {}
        )
        self.assertEqual(
            program.dsmal,
            'a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z;'
            '5|-5|6|-2|1.5|3|4|9;'
            'LzSt0LoSt1LoSt2LzSt3LzSt4LoSt5Lc0St6LoSt7Lc2St8Lc4St9Lc7St10LzSt11LoSt12Lc0St13Lc1St14Lc0St15Lc1St16Lc0St17Lc0St18Lc3St19Lc0St20Lc2St21Lc6St22Lc5St23Lc0CpLv0EqJn60CpLv1EqJn60PpLzJu62PpLoSt24LzSt25Xx' # pylint: disable=line-too-long
        )

    def test_compile_eliminate_constant_declared_variables(self):
        program, _ = abysmal.compile(
            '''\
let t1 = 5
let t2 = t1 * 3
let t3 = t2 + a
@start:
    b = t3
    c = t2 - 9
''',
            ['a', 'b', 'c'],
            {}
        )
        self.assertEqual(program.dsmal, 't3|a|b|c;15|6;Lc0Lv1AdSt0Lv0St2Lc1St3Xx')

    def test_compile_eliminated_unreachable_code(self):
        program, _ = abysmal.compile(
            '''\
@start:
    c = a
    => @a
    c = b
@a:
    0 > 1 => @nope
    a > b => @yup
@nope:
    => @nope2
@yup:
    c = b
@nope2:
    c = 100
    a = 200
@nope3:
    c = 300
''',
            ['a', 'b', 'c'],
            {}
        )
        self.assertEqual(program.dsmal, 'a|b|c;;Lv0St2Lv0Lv1GtJn7XxLv1St2Xx')

    @contextmanager
    def assert_raises_compilation_error(self, message, line_number=None, char_number=None):
        with self.assertRaises(abysmal.compiler.CompilationError) as raised:
            yield

        if line_number is not None:
            message += ' (line ' + str(line_number)
            if char_number is not None:
                message += ', char ' + str(char_number)
            message += ')'
        actual_message = str(raised.exception)
        self.assertEqual(
            actual_message,
            message,
            'error message did not match: {0!r} != {1!r}'.format(actual_message, message)
        )

    def test_parse_no_states(self):
        for source_code in ['', ' ', '# nothing', 'let z = 0']:
            with self.assert_raises_compilation_error('no states are defined'):
                abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_late_declaration(self):
        source_code = '''\
@start:
    let temp = 0
        '''
        with self.assert_raises_compilation_error('variables must be declared before the first state definition'):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_redeclared_constant(self):
        source_code = '''\
    let WAFFLE = 0
@start:
        '''
        with self.assert_raises_compilation_error('redeclaration of constant "WAFFLE"', line_number=1, char_number=8):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_redeclared_variable(self):
        source_code = '''\
    let flavor = 0
@start:
        '''
        with self.assert_raises_compilation_error('redeclaration of variable "flavor"', line_number=1, char_number=8):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_redeclared_custom_variable(self):
        source_code = '''\
    let temp = 0
    let temp = 1
@start:
        '''
        with self.assert_raises_compilation_error('redeclaration of variable "temp"', line_number=2, char_number=8):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_assigned_custom_variable_to_self(self):
        source_code = '''\
    let temp = temp
@start:
        '''
        with self.assert_raises_compilation_error('reference to undeclared variable "temp"', line_number=1, char_number=15):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_missing_start_state_label(self):
        for source_code in ['price = 1', '=> @_', 'flavor == 1 => @_']:
            with self.assert_raises_compilation_error('missing start state label'):
                abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_line_numbers(self):
        source_code = '#1\r#2\n#3\r\n\r\n\n\r$'
        with self.assert_raises_compilation_error('unknown token', line_number=7, char_number=0):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_unknown_token(self):
        for source_code in ['$flavor', '$4', '"flavor"']:
            with self.assert_raises_compilation_error('unknown token', line_number=1, char_number=0):
                abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_unexpected_token(self):
        source_code = '''\
@start
'''
        with self.assert_raises_compilation_error('expected :, but found end-of-line instead', line_number=1, char_number=6):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
let
'''
        with self.assert_raises_compilation_error('expected identifier, but found end-of-line instead', line_number=1, char_number=3):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
let temp
'''
        with self.assert_raises_compilation_error('expected =, but found end-of-line instead', line_number=1, char_number=8):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
let temp 3
'''
        with self.assert_raises_compilation_error('expected =, but found literal instead', line_number=1, char_number=9):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
let temp +
'''
        with self.assert_raises_compilation_error('expected =, but found + instead', line_number=1, char_number=9):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
let temp, temp2
'''
        with self.assert_raises_compilation_error('expected =, but found , instead', line_number=1, char_number=8):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
let temp =
'''
        with self.assert_raises_compilation_error('unexpected end-of-line', line_number=1, char_number=10):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
let temp = price = 3
'''
        with self.assert_raises_compilation_error('chained assignment is not allowed - did you mean == instead?', line_number=1, char_number=17):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
let temp = 0
@start:
    temp = price = 3
'''
        with self.assert_raises_compilation_error('chained assignment is not allowed - did you mean == instead?', line_number=3, char_number=17):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    => y
'''
        with self.assert_raises_compilation_error('expected label, but found identifier instead', line_number=2, char_number=7):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    price = flavor in 3
'''
        with self.assert_raises_compilation_error('expected {, but found literal instead', line_number=2, char_number=22):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    price = flavor not
'''
        with self.assert_raises_compilation_error('expected in, but found end-of-line instead', line_number=2, char_number=22):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    price = flavor not in 5
'''
        with self.assert_raises_compilation_error('expected {, but found literal instead', line_number=2, char_number=26):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    price = @b
'''
        with self.assert_raises_compilation_error('unexpected label', line_number=2, char_number=12):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    flavor || || => @b
'''
        with self.assert_raises_compilation_error('unexpected ||', line_number=2, char_number=14):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    < 3 => @b
'''
        with self.assert_raises_compilation_error('unexpected <', line_number=2, char_number=4):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    price = !
'''
        with self.assert_raises_compilation_error('unexpected end-of-line', line_number=2, char_number=13):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    price = flavor in {}
'''
        with self.assert_raises_compilation_error('unexpected }', line_number=2, char_number=23):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    price = flavor in {3,}
'''
        with self.assert_raises_compilation_error('unexpected }', line_number=2, char_number=25):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_unexpected_text_after_line_continuation(self):
        source_code = '''\
@start:
    price = scoops + \\ !
            sprinkles * 0.50
'''
        with self.assert_raises_compilation_error('unexpected text after line-continuation character', line_number=2, char_number=23):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_undeclared_variable(self):
        source_code = '''\
@start:
    bogus > 0 => @b
'''
        with self.assert_raises_compilation_error('reference to undeclared variable "bogus"', line_number=2, char_number=4):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_unknown_function(self):
        source_code = '''\
@start:
    price = BOGUS(flavor)
'''
        with self.assert_raises_compilation_error('reference to unknown function "BOGUS"', line_number=2, char_number=12):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_wrong_number_of_function_parameters(self):
        source_code = '''\
@start:
    price = MIN(1)
'''
        with self.assert_raises_compilation_error('function MIN() accepts between 2 and 100 parameters (1 provided)', line_number=2, char_number=12):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    price = ABS(-1, 2)
'''
        with self.assert_raises_compilation_error('function ABS() accepts 1 parameter (2 provided)', line_number=2, char_number=12):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_illegal_assignment(self):
        source_code = '''\
@start:
    2 = 1
'''
        with self.assert_raises_compilation_error('illegal assignment', line_number=2, char_number=6):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    WAFFLE = 1
'''
        with self.assert_raises_compilation_error('illegal assignment', line_number=2, char_number=11):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    random! = 1
'''
        with self.assert_raises_compilation_error('illegal assignment', line_number=2, char_number=12):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    price = tax = 1
'''
        with self.assert_raises_compilation_error('chained assignment is not allowed - did you mean == instead?', line_number=2, char_number=16):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_assignment_to_undeclared_variable(self):
        source_code = '''\
@start:
    bogus = 1
'''
        with self.assert_raises_compilation_error('reference to undeclared variable "bogus"', line_number=2, char_number=4):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_illegal_branch_condition(self):
        source_code = '''\
@start:
    bogus => @b
'''
        with self.assert_raises_compilation_error('reference to undeclared variable "bogus"', line_number=2, char_number=4):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

        source_code = '''\
@start:
    flavor || bogus => @b
'''
        with self.assert_raises_compilation_error('reference to undeclared variable "bogus"', line_number=2, char_number=14):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_duplicate_label(self):
        source_code = '''\
@a:
   => @b
@b:
   => @c
@a:
   price = 1
'''
        with self.assert_raises_compilation_error('duplicate label "@a"', line_number=5):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_trivial_cycle(self):
        source_code = '''\
@a:
   => @a
'''
        with self.assert_raises_compilation_error('branch to itself in state "@a"', line_number=2):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_cycle_detected(self):
        source_code = '''\
@a:
    => @b
    => @c
@b:
    => @d
    => @e
@c:
    => @d
@d:
    => @e
@e:
    => @c
'''
        with self.assert_raises_compilation_error('cycle exists between states "@c", "@e", "@d"'):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_parse_undefined_label(self):
        source_code = '''\
@start:
   => @bogus
'''
        with self.assert_raises_compilation_error('branch to undefined label "@bogus"', line_number=2):
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

    def test_overlapping_variable_and_constant_names(self):
        source_code = '''\
@start:
'''
        with self.assertRaises(ValueError) as raised:
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, dict(ICE_CREAM_CONSTANTS, scoops=1))
        self.assertEqual(
            str(raised.exception),
            '"scoops" cannot be both a variable and a constant'
        )

    def test_invalid_constant_value(self):
        source_code = '''\
@start:
'''
        with self.assertRaises(ValueError) as raised:
            abysmal.compile(source_code, ICE_CREAM_VARIABLES, dict(ICE_CREAM_CONSTANTS, WAFFLE=None))
        self.assertEqual(
            str(raised.exception),
            'the value of constant "WAFFLE" (None) is not an int, float, or Decimal'
        )

    def test_expressions(self):

        def check(expr, expected_result):
            source_code = '@start:\n    result = ' + expr
            program, _ = abysmal.compile(
                source_code,
                {'zero', 'one', 'tenth', 'result'},
                {
                    'TRUE': True,
                    'FALSE': False,
                    'TWO': 2,
                    'PI': Decimal('3.14159'),
                }
            )
            machine = program.machine(zero=0, one=1.0, tenth=Decimal('0.1'))
            machine.random_number_iterator = abysmal.DEFAULT_RANDOM_NUMBER_ITERATOR
            try:
                machine.run()
                result = Decimal(machine['result'])
            except abysmal.ExecutionError as ex:
                self.assertEqual(str(ex), expected_result)
            else:
                self.assertEqual(result, expected_result)

        check('0', 0)
        check('1', 1)
        check('2', 2)
        check('123', 123)
        check('+123', 123)
        check('-123', -123)
        check('42k', 42000)
        check('-0.5K', -500)
        check('1.2m', 1200000)
        check('-0.5M', -500000)
        check('63B', 63000000000)
        check('-0.5b', -500000000)
        check('0%', 0)
        check('5%', Decimal('0.05'))
        check('11.1%', Decimal('0.111'))
        check('100%', 1)
        check('-18.3%', Decimal('-0.183'))

        check('zero', 0)
        check('one', 1)
        check('TWO', 2)
        check('PI', Decimal('3.14159'))
        check('tenth', Decimal('0.1'))
        check('(((TWO)))', 2)

        check('TRUE', 1)
        check('FALSE', 0)

        check('! zero', 1)
        check('! one', 0)
        check('! TWO', 0)
        check('! 0.1', 0)
        check('! 0.0', 1)
        check('!!!zero', 1)

        check('+zero', 0)
        check('+one', 1)
        check('+TWO', 2)

        check('-zero', 0)
        check('-one', -1)
        check('-TWO', -2)

        check('-one && TWO', 1)
        check('-(one && TWO)', -1)

        check('zero == 0', 1)
        check('one == 1', 1)
        check('TWO == 2', 1)
        check('one == TWO', 0)
        check('one != TWO', 1)
        check('one < TWO', 1)
        check('one <= TWO', 1)
        check('one > TWO', 0)
        check('one >= TWO', 0)

        check('0 || 0', 0)
        check('5 || 0', 1)
        check('0 || 5', 1)
        check('3 || 5', 1)

        check('zero || zero', 0)
        check('one || zero', 1)
        check('zero || one', 1)

        check('zero || zero || tenth', 1)
        check('zero || zero || zero', 0)

        check('zero || one', 1)
        check('zero || TWO', 1)
        check('TWO || one', 1)

        check('zero && one', 0)
        check('one && zero', 0)
        check('one && TWO', 1)
        check('TWO && one', 1)

        check('zero == 0 && TWO == 2', 1)

        check('!TWO == one', 0)
        check('!(TWO == one)', 1)

        check('result', 0)
        check('!result', 1)
        check('-result', 0)
        check('result || !result', 1)

        check('1 < 2 == 2 > 0', 1)
        check('1 < (2 == 2) > 0', 0)

        check('1 + 2', 3)
        check('1 - 2', -1)
        check('PI + 1', Decimal('4.14159'))
        check('2 * 3', 6)
        check('3 / 2', 1.5)
        check('5 / 0', 'illegal Dv at instruction 2')

        check('2 ^ 5', 32)
        check('2 ^ 3 ^ 2', 512)
        check('1 + 1 ^ 3', 2)
        check('(1 + 1) ^ 3', 8)
        check('2 * 3 ^ 2', 18)

        check('-1 ^ 0.5', 'illegal Pw at instruction 2')
        check('-1 ^ 1.5', 'illegal Pw at instruction 2')
        check('1 ^ -0.5', 1)

        check('1 + 2 == 3 - 0', 1)
        check('2 + 3 * 4', 14)

        check('3 in {2}', 0)
        check('3 not in {2}', 1)
        check('2 in {one, TWO}', 1)
        check('2 not in {one, TWO}', 0)
        check('result in {result}', 1)
        check('result not in {result}', 0)
        check('1 in {-1, -1, 2}', 0)
        check('1 not in {-1, -1, 2}', 1)

        check('1 + 1 == 2 ? 1 : 0', 1)
        check('TWO + TWO == 3 ? 1 : 0', 0)
        check('one + TWO == 2 ? 1 : 0', 0)

        check('tenth * 9 == 9 / 10', 1)

        check('ABS(5)', 5)
        check('ABS(-5)', 5)

        check('CEILING(1.1)', 2)
        check('CEILING(1.9)', 2)
        check('CEILING(-1.1)', -1)
        check('CEILING(-1.9)', -1)

        check('FLOOR(1.1)', 1)
        check('FLOOR(1.9)', 1)
        check('FLOOR(-1.1)', -2)
        check('FLOOR(-1.9)', -2)

        check('MAX(1, 2)', 2)
        check('MAX(1, 2, 3-4)', 2)

        check('MIN(1, 2)', 1)
        check('MIN(1, 2, 3-4)', -1)

        check('ROUND(0.4)', 0)
        check('ROUND(0.499999999999)', 0)
        check('ROUND(0.5)', 0)
        check('ROUND(0.500000000001)', 1)
        check('ROUND(0.6)', 1)
        check('ROUND(-0.4)', 0)
        check('ROUND(-0.499999999999)', 0)
        check('ROUND(-0.5)', 0)
        check('ROUND(-0.500000000001)', -1)
        check('ROUND(-0.6)', -1)

        check('random! >= 0', 1)
        check('random! <= 1', 1)

    def test_run(self):
        source_code = '''\
@start:
    scoops == 0 => @explode
    price = (scoops * 1.25) + \\  ''' + '''
            ((cone == WAFFLE) * 1.00) + \\
            (sprinkles * 0.25)
    tax = price * 0.10
    total = price + tax
    stampsEarned = scoops > 5 ? 2 * scoops : scoops

@explode:
    price = scoops / 0 # division by zero!

@unused:
    price = 1M # optimized out
'''
        for newline in ['\n', '\r', '\r\n']:
            program, _ = abysmal.compile(source_code.replace('\n', newline), ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)
            machine = program.machine(flavor=1, scoops=1, cone=1, sprinkles=0)

            machine.run()
            self.assertEqual(machine['price'], '1.25')

            machine.reset()
            machine.run()
            self.assertEqual(machine['price'], '1.25')

            machine = program.machine(scoops=2, cone=2, sprinkles=True)
            machine.run()
            self.assertEqual(machine['price'], '3.75')

    def test_run_stress_test(self):
        source_code = '''\
# This model is designed to exercise every DSM opcode.
@a:
    result = 1.23
    result = PI
    result = random!
    result = 0
    result = 1
    result = !zero
    result = -one
    result = ABS(-half)
    result = CEILING(half)
    result = FLOOR(half)
    result = ROUND(half)
    result = x == y
    result = x != y
    result = x < y
    result = x <= y
    result = x > y
    result = x >= y
    result = x + y
    result = x - y
    result = x * y
    result = x / (y + one)
    result = x ^ 2
    result = MIN(x, y, PI)
    result = MAX(x, y, PI)
    result = x in { y, 0 }
    result = y not in { x, 1 }
    x == y => @b
    x != y => @c
@b:
    result = x && y
    => @d
@c:
    result = y || y
    => @d
@d:
    result = 0
'''
        program, source_map = abysmal.compile(
            source_code,
            {'zero', 'one', 'half', 'x', 'y', 'result'},
            {'PI': Decimal('3.14159')}
        )

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
            self.assertIn(opcode, program.dsmal)

        machine = program.machine(zero=0, one=1, half=0.5)
        coverages = [[False] * len(source_map)]
        for x in (0, 1):
            for y in (0, 1):
                machine.reset(x=x, y=y)
                coverages.append(machine.run_with_coverage())
                self.assertEqual(machine['result'], '0')

        # Make sure all lines were covered
        for idx_instruction, hit in enumerate(any(v) for v in zip(*coverages)):
            line_number = source_map[idx_instruction]
            self.assertTrue(hit or line_number is None, line_number)

    def test_parse_number(self):
        self.assertEqual(abysmal.compiler.parse_number('0'), Decimal(0))
        self.assertEqual(abysmal.compiler.parse_number('1.5'), Decimal('1.5'))
        self.assertEqual(abysmal.compiler.parse_number('3.14159'), Decimal('3.14159'))
        self.assertEqual(abysmal.compiler.parse_number('15.3%'), Decimal('0.153'))
        self.assertEqual(abysmal.compiler.parse_number('1k'), Decimal('1000'))
        self.assertEqual(abysmal.compiler.parse_number('2.5M'), Decimal('2500000'))
        self.assertIsNone(abysmal.compiler.parse_number('bogus'))
