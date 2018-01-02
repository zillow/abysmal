from collections import defaultdict, namedtuple
from decimal import Decimal
import itertools
import math
import re

from . import dsm # pylint:disable=no-name-in-module


DECIMAL_ZERO = Decimal('0')
DECIMAL_ONE = Decimal('1')


class CompilationError(ValueError):
    """
    An error that occurred while trying to compile an Absymal program.
    """

    def __init__(self, message, line_number=None, char_number=None):
        if line_number is not None:
            message += ' (line ' + str(line_number)
            if char_number is not None:
                message += ', char ' + str(char_number)
            message += ')'
        ValueError.__init__(self, message)


#
# Tokens
#

class Token(namedtuple('Token', ['type', 'value', 'line_number', 'char_number'])):

    def unexpected(self):
        raise CompilationError(
            'unexpected ' + self.type,
            line_number=self.line_number,
            char_number=self.char_number
        )


#
# Abstract syntax tree nodes
#

class Variable(namedtuple('Variable', ['name', 'token'])):

    __slots__ = ()

    def emit(self, code_generator):
        code_generator.emit('Lv', self.name)

    def rewrite(self, fn):
        return fn(self)


class Literal(namedtuple('Literal', ['value', 'token'])):

    __slots__ = ()

    def emit(self, code_generator):
        code_generator.emit('Lc', str(self.value))

    def rewrite(self, fn):
        return fn(self)


class RandomValue(tuple):

    __slots__ = ()

    def emit(self, code_generator): # pylint: disable=no-self-use
        code_generator.emit('Lr')

    def rewrite(self, fn):
        return fn(self)


class UnOp(namedtuple('UnOp', ['op', 'operand'])):

    __slots__ = ()

    OPS = frozenset(['!', '-', '+'])

    def emit(self, code_generator):
        self.operand.emit(code_generator)
        if self.op == '!':
            code_generator.emit('Nt')
        elif self.op == '-':
            code_generator.emit('Ng')

    def rewrite(self, fn):
        return fn(self._replace(operand=self.operand.rewrite(fn)))

    def fold_constants(self):
        if not isinstance(self.operand, Literal):
            return self
        value = self.operand.value
        if self.op == '!':
            value = DECIMAL_ZERO if value else DECIMAL_ONE
        elif self.op == '+':
            pass # no-op
        else:
            assert self.op == '-'
            value = -value
        return Literal(value, None)


class LogicalOp(namedtuple('LogicalOp', ['op', 'predicates'])):

    __slots__ = ()

    def emit(self, code_generator):
        if self.op == '||':
            true_label = code_generator.allocate_label()
            after_label = code_generator.allocate_label()
            for predicate in self.predicates:
                predicate.emit(code_generator)
                code_generator.emit('Jn', true_label)
            code_generator.emit('Lz')
            code_generator.emit('Ju', after_label)
            code_generator.label_next_instruction(true_label)
            code_generator.emit('Lo')
            code_generator.label_next_instruction(after_label)
        else:
            assert self.op == '&&'
            false_label = code_generator.allocate_label()
            after_label = code_generator.allocate_label()
            for predicate in self.predicates:
                predicate.emit(code_generator)
                code_generator.emit('Jz', false_label)
            code_generator.emit('Lo')
            code_generator.emit('Ju', after_label)
            code_generator.label_next_instruction(false_label)
            code_generator.emit('Lz')
            code_generator.label_next_instruction(after_label)

    def rewrite(self, fn):
        return fn(self._replace(predicates=[predicate.rewrite(fn) for predicate in self.predicates]))

    def fold_constants(self):
        predicates = []
        if self.op == '||':
            for predicate in self.predicates:
                if isinstance(predicate, Literal):
                    if predicate.value:
                        return Literal(DECIMAL_ONE, None)
                    # filter out zeroes
                else:
                    predicates.append(predicate)
            if not predicates:
                return Literal(DECIMAL_ZERO, None)
        else:
            assert self.op == '&&'
            for predicate in self.predicates:
                if isinstance(predicate, Literal):
                    if not predicate.value:
                        return Literal(DECIMAL_ZERO, None)
                    # filter out nonzeroes
                else:
                    predicates.append(predicate)
            if not predicates:
                return Literal(DECIMAL_ONE, None)
        return self._replace(predicates=predicates)


class BinOp(namedtuple('BinOp', ['left', 'op', 'right'])):

    __slots__ = ()

    LEFT_ASSOCIATIVE_OPS = frozenset(['==', '!=', '<', '<=', '>', '>=', '+', '-', '*', '/'])
    RIGHT_ASSOCIATIVE_OPS = frozenset(['^'])

    def emit(self, code_generator):
        if self.op == '+':
            self.left.emit(code_generator)
            self.right.emit(code_generator)
            code_generator.emit('Ad')
        elif self.op == '-':
            self.left.emit(code_generator)
            self.right.emit(code_generator)
            code_generator.emit('Sb')
        elif self.op == '*':
            self.left.emit(code_generator)
            self.right.emit(code_generator)
            code_generator.emit('Ml')
        elif self.op == '/':
            self.left.emit(code_generator)
            self.right.emit(code_generator)
            code_generator.emit('Dv')
        elif self.op == '^':
            self.left.emit(code_generator)
            self.right.emit(code_generator)
            code_generator.emit('Pw')
        elif self.op == '==':
            self.left.emit(code_generator)
            self.right.emit(code_generator)
            code_generator.emit('Eq')
        elif self.op == '!=':
            self.left.emit(code_generator)
            self.right.emit(code_generator)
            code_generator.emit('Ne')
        elif self.op == '<':
            self.right.emit(code_generator)
            self.left.emit(code_generator)
            code_generator.emit('Gt')
        elif self.op == '<=':
            self.right.emit(code_generator)
            self.left.emit(code_generator)
            code_generator.emit('Ge')
        elif self.op == '>':
            self.left.emit(code_generator)
            self.right.emit(code_generator)
            code_generator.emit('Gt')
        else:
            assert self.op == '>='
            self.left.emit(code_generator)
            self.right.emit(code_generator)
            code_generator.emit('Ge')

    def rewrite(self, fn):
        return fn(self._replace(left=self.left.rewrite(fn), right=self.right.rewrite(fn)))

    def fold_constants(self):
        if not isinstance(self.left, Literal) or not isinstance(self.right, Literal):
            return self
        left_value = self.left.value
        right_value = self.right.value

        if self.op == '+':
            value = left_value + right_value
        elif self.op == '-':
            value = left_value - right_value
        elif self.op == '*':
            value = left_value * right_value
        elif self.op == '/':
            try:
                value = left_value / right_value
            except:
                return self
        elif self.op == '^':
            try:
                value = left_value ** right_value
            except: # fractional powers of negatives -> 0
                return self
        elif self.op == '==':
            value = DECIMAL_ONE if left_value == right_value else DECIMAL_ZERO
        elif self.op == '!=':
            value = DECIMAL_ONE if left_value != right_value else DECIMAL_ZERO
        elif self.op == '<':
            value = DECIMAL_ONE if left_value < right_value else DECIMAL_ZERO
        elif self.op == '<=':
            value = DECIMAL_ONE if left_value <= right_value else DECIMAL_ZERO
        elif self.op == '>':
            value = DECIMAL_ONE if left_value > right_value else DECIMAL_ZERO
        else:
            assert self.op == '>='
            value = DECIMAL_ONE if left_value >= right_value else DECIMAL_ZERO

        return Literal(value, None)


class TerOp(namedtuple('TerOp', ['question', 'yes', 'no'])):

    __slots__ = ()

    def emit(self, code_generator):
        yes_label = code_generator.allocate_label()
        after_label = code_generator.allocate_label()
        self.question.emit(code_generator)
        code_generator.emit('Jn', yes_label)
        self.no.emit(code_generator)
        code_generator.emit('Ju', after_label)
        code_generator.label_next_instruction(yes_label)
        self.yes.emit(code_generator)
        code_generator.label_next_instruction(after_label)

    def rewrite(self, fn):
        return fn(self._replace(question=self.question.rewrite(fn), yes=self.yes.rewrite(fn), no=self.no.rewrite(fn)))

    def fold_constants(self):
        if not isinstance(self.question, Literal):
            return self
        return self.yes if self.question.value else self.no


class SetMembership(namedtuple('SetMembership', ['operand', 'members'])):

    __slots__ = ()

    def emit(self, code_generator):
        true_label = code_generator.allocate_label()
        after_label = code_generator.allocate_label()
        self.operand.emit(code_generator)
        for member in self.members:
            code_generator.emit('Cp')
            member.emit(code_generator)
            code_generator.emit('Eq')
            code_generator.emit('Jn', true_label)
        code_generator.emit('Pp')
        code_generator.emit('Lz')
        code_generator.emit('Ju', after_label)
        code_generator.label_next_instruction(true_label)
        code_generator.emit('Pp')
        code_generator.emit('Lo')
        code_generator.label_next_instruction(after_label)

    def rewrite(self, fn):
        return fn(self._replace(operand=self.operand.rewrite(fn), members=[member.rewrite(fn) for member in self.members]))

    def fold_constants(self):
        if not isinstance(self.operand, Literal) or not any(isinstance(member, Literal) for member in self.members):
            return self
        operand_value = self.operand.value
        nonliteral_members = []
        for member in self.members:
            if isinstance(member, Literal):
                if operand_value == member.value:
                    return Literal(DECIMAL_ONE, None)
            else:
                nonliteral_members.append(member)
        if not nonliteral_members:
            return Literal(DECIMAL_ZERO, None)
        return self._replace(members=nonliteral_members)


class RangeMembership(namedtuple('RangeMembership', ['operand', 'low', 'high', 'low_inclusive', 'high_inclusive'])):

    __slots__ = ()

    def emit(self, code_generator):
        false_label = code_generator.allocate_label()
        after_label = code_generator.allocate_label()
        self.operand.emit(code_generator)
        code_generator.emit('Cp')
        self.low.emit(code_generator)
        code_generator.emit('Ge' if self.low_inclusive else 'Gt')
        code_generator.emit('Jz', false_label)
        self.high.emit(code_generator)
        code_generator.emit('Gt' if self.high_inclusive else 'Ge')
        code_generator.emit('Nt')
        code_generator.emit('Ju', after_label)
        code_generator.label_next_instruction(false_label)
        code_generator.emit('Pp')
        code_generator.emit('Lz')
        code_generator.label_next_instruction(after_label)

    def rewrite(self, fn):
        return fn(self._replace(operand=self.operand.rewrite(fn), low=self.low.rewrite(fn), high=self.high.rewrite(fn)))

    def fold_constants(self):
        if not isinstance(self.operand, Literal):
            return self
        operand_value = self.operand.value
        if isinstance(self.low, Literal):
            if operand_value > self.low.value or (self.low_inclusive and operand_value == self.low.value):
                return BinOp(self.operand, '<=' if self.high_inclusive else '<', self.high)
            else:
                return Literal(DECIMAL_ZERO, None)
        if isinstance(self.high, Literal):
            if operand_value < self.high.value or (self.high_inclusive and operand_value == self.high.value):
                return BinOp(self.operand, '>=' if self.low_inclusive else '>', self.low)
            else:
                return Literal(DECIMAL_ZERO, None)
        return self


class FunctionCall(namedtuple('FunctionCall', ['function', 'params'])):

    __slots__ = ()

    FUNCTIONS = {
        'ABS': (1, 1),
        'CEILING': (1, 1),
        'FLOOR': (1, 1),
        'MAX': (2, 100),
        'MIN': (2, 100),
        'ROUND': (1, 1),
    }

    def emit(self, code_generator):
        if self.function == 'ABS':
            self.params[0].emit(code_generator)
            code_generator.emit('Ab')
        elif self.function == 'CEILING':
            self.params[0].emit(code_generator)
            code_generator.emit('Cl')
        elif self.function == 'FLOOR':
            self.params[0].emit(code_generator)
            code_generator.emit('Fl')
        elif self.function == 'MAX':
            first = True
            for param in self.params:
                param.emit(code_generator)
                if not first:
                    code_generator.emit('Mx')
                else:
                    first = False
        elif self.function == 'MIN':
            first = True
            for param in self.params:
                param.emit(code_generator)
                if not first:
                    code_generator.emit('Mn')
                else:
                    first = False
        else:
            assert self.function == 'ROUND'
            self.params[0].emit(code_generator)
            code_generator.emit('Rd')

    def rewrite(self, fn):
        return fn(self._replace(params=tuple(param.rewrite(fn) for param in self.params)))

    def fold_constants(self):
        if not all(isinstance(param, Literal) for param in self.params):
            return self
        param_values = [param.value for param in self.params]
        if self.function == 'ABS':
            value = abs(param_values[0])
        elif self.function == 'CEILING':
            value = Decimal(math.ceil(param_values[0])) # note: math.ceil() returns int
        elif self.function == 'FLOOR':
            value = Decimal(math.floor(param_values[0])) # note: math.floor() returns int
        elif self.function == 'MAX':
            value = max(param_values)
        elif self.function == 'MIN':
            value = min(param_values)
        else:
            assert self.function == 'ROUND'
            value = round(param_values[0], 0)
        return Literal(value, None)


class Branch(namedtuple('Branch', ['condition', 'destination', 'line_number'])):

    __slots__ = ()

    def emit(self, code_generator):
        code_generator.emitting_line_number = self.line_number
        if self.condition:
            if isinstance(self.condition, UnOp) and self.condition.op == '!':
                self.condition.operand.emit(code_generator)
                code_generator.emit('Jz', self.destination)
            else:
                self.condition.emit(code_generator)
                code_generator.emit('Jn', self.destination)
        else:
            code_generator.emit('Ju', self.destination)
        code_generator.emitting_line_number = None

    def rewrite(self, fn):
        return fn(self._replace(condition=self.condition.rewrite(fn)) if self.condition is not None else self)

    def fold_constants(self):
        if self.condition is None or not isinstance(self.condition, Literal):
            return self
        return self._replace(condition=None) if self.condition.value else None


class Assignment(namedtuple('Assignment', ['target', 'value', 'line_number'])):

    __slots__ = ()

    def emit(self, code_generator):
        code_generator.emitting_line_number = self.line_number
        self.value.emit(code_generator)
        code_generator.emit('St', self.target)
        code_generator.emitting_line_number = None

    def rewrite(self, fn):
        return fn(self._replace(value=self.value.rewrite(fn)))


class State(namedtuple('State', ['label', 'actions', 'line_number'])):

    __slots__ = ()

    @property
    def branches(self):
        for action in self.actions:
            if isinstance(action, Branch):
                yield action

    @property
    def assignments(self):
        for action in self.actions:
            if isinstance(action, Assignment):
                yield action

    def emit(self, code_generator):
        code_generator.label_next_instruction(self.label)
        for action in self.actions:
            action.emit(code_generator)
        if not code_generator.instructions or code_generator.instructions[-1].opcode != 'Ju':
            code_generator.emit('Xx')

    def rewrite(self, fn):
        actions = []
        for action in self.actions:
            rewritten = action.rewrite(fn)
            if rewritten is not None:  # pragma: no branch
                actions.append(rewritten)
        return fn(self._replace(actions=tuple(actions)))


class AST(namedtuple('AST', ['declared_variable_names', 'initializations', 'states'])):

    __slots__ = ()

    def rewrite(self, fn):
        initializations = []
        for initialization in self.initializations:
            rewritten = initialization.rewrite(fn)
            if rewritten is not None: # pragma: no branch
                initializations.append(rewritten)
        states = []
        for state in self.states:
            rewritten = state.rewrite(fn)
            if rewritten is not None: # pragma: no branch
                states.append(rewritten)
        return self._replace(initializations=initializations, states=states)

    def optimize(self):
        ast = self
        keep_optimizing = True
        optimization_passes = 0

        def fold_constants(old_node):
            nonlocal keep_optimizing
            old_node_fold_constants = getattr(old_node, 'fold_constants', None)
            if old_node_fold_constants is not None:
                new_node = old_node_fold_constants()
                if new_node != old_node:
                    keep_optimizing = True
                    return new_node
            return old_node

        def convert_declared_variables_to_literals(ast):
            nonlocal keep_optimizing
            initializations = []
            assignment_targets = {assignment.target for state in ast.states for assignment in state.assignments}
            for idx_initialization in range(0, len(ast.initializations)):
                initialization = ast.initializations[idx_initialization]
                assert initialization.target in ast.declared_variable_names
                if isinstance(initialization.value, Literal) and initialization.target not in assignment_targets:
                    keep_optimizing = True
                    replace_variable = lambda node: initialization.value if isinstance(node, Variable) and node.name == initialization.target else node # pylint: disable=cell-var-from-loop
                    for idx_subsequent_initialization in range(idx_initialization + 1, len(ast.initializations)):
                        ast.initializations[idx_subsequent_initialization] = ast.initializations[idx_subsequent_initialization].rewrite(replace_variable)
                    for idx_state in range(0, len(ast.states)):
                        ast.states[idx_state] = ast.states[idx_state].rewrite(replace_variable)
                    ast.declared_variable_names.remove(initialization.target)
                else:
                    initializations.append(initialization)
            return ast._replace(initializations=initializations)

        while keep_optimizing and optimization_passes < 10:
            optimization_passes += 1
            keep_optimizing = False # can be set to True by subfunctions
            ast = ast.rewrite(fold_constants)
            ast = convert_declared_variables_to_literals(ast)

        return ast


#
# Parser
#

class Parser(object):

    DECIMAL_TOKEN_SUFFIX_SHIFT_AMOUNTS = {'%': -2, 'k': 3, 'm': 6, 'b': 9}
    DECIMAL_TOKEN_PATTERN = r'(?P<decimal_whole>[0-9]+)(?P<decimal_fraction>\.[0-9]+)?(?P<decimal_suffix>[%kKmMbB])?'
    DECIMAL_TOKEN_REGEX = re.compile('^' + DECIMAL_TOKEN_PATTERN + '$')

    # Note: the order of this list matters!
    TOKENIZER_PATTERNS = (
        ('label', '@[a-zA-Z0-9_]+'),
        ('random', 'random!'),
        ('id', '[a-zA-Z][a-zA-Z0-9_]*'),
        ('decimal', DECIMAL_TOKEN_PATTERN),
        ('symbol', '|'.join(re.escape(symbol) for symbol in (
            '==', '=>', '=', '!=', '!', '<=', '<', '>=', '>',
            '&&', '||',
            '+', '-', '*', '/', '^',
            '?', ':',
            ',',
            '(', ')',
            '[', ']',
            '{', '}',
        ))),
        ('comment', '#'),
        ('linecontinuation', r'\\'),
        ('blankline', r'\s'),
        ('invalid', '.'),
    )
    TOKENIZER_REGEX = re.compile(r'\s*(?:{0})'.format('|'.join('(?P<{0}>{1})'.format(name, pattern) for name, pattern in TOKENIZER_PATTERNS)))
    NEWLINE_REGEX = re.compile(r'\r\n?|\n')

    # Left-binding-power, aka binary operator precedence.
    LBP = defaultdict(int, {
        '^':  100,
        '*':  90, '/':  90,
        '+':  80, '-':  80,
        'in': 70, 'not': 70,
        '<':  60, '<=': 60, '>':  60, '>=': 60,
        '==': 50, '!=': 50,
        '&&': 40,
        '||': 30,
        '?':  20,
        '=':  10,
    })

    KEYWORDS = frozenset(['in', 'let', 'not'])

    def __init__(self, variable_names, constants):
        self.variable_names = variable_names # variables pre-defined externally
        self.constants = constants
        self.declared_variable_names = set() # extra variables defined inside the source code
        self.initializations = []
        self.states = []
        self.state_dict = {}
        self.line_number = 1
        self.line_continuations = 0
        self.tokens = None
        self.token = None
        self.current_state_label_token = None
        self.current_state_actions = None
        self.in_assignment = False

    @staticmethod
    def _number_from_match(m):
        digits = m.group('decimal_whole')
        decimal_point = len(digits)

        fraction = m.group('decimal_fraction')
        if fraction:
            digits += fraction[1:]

        suffix = m.group('decimal_suffix')
        if suffix:
            decimal_point += Parser.DECIMAL_TOKEN_SUFFIX_SHIFT_AMOUNTS[suffix.lower()]

        if decimal_point <= 0:
            digits = ('0' * -decimal_point) + digits
            decimal_point = 0
        elif decimal_point > len(digits):
            digits = digits + ('0' * (decimal_point - len(digits)))
            decimal_point = len(digits)

        s = digits[:decimal_point].lstrip('0') + '.' + digits[decimal_point:].rstrip('0')
        return Decimal(s) if s != '.' else DECIMAL_ZERO

    @staticmethod
    def parse_number(s):
        m = Parser.DECIMAL_TOKEN_REGEX.match(s)
        return Parser._number_from_match(m) if m else None

    @property
    def line_number_or_range(self):
        return self.line_number if not self.line_continuations else (self.line_number - self.line_continuations, self.line_number)

    # Return a generator that yields the tokens in the source code.
    # Updates self.line_number and self.line_continuations as it tokenizes.
    def _tokenize(self, source_code):
        continues_to_next_line = False
        for line in self.NEWLINE_REGEX.split(source_code):
            if continues_to_next_line:
                self.line_continuations += 1
                continues_to_next_line = False
            else:
                self.line_continuations = 0
            iter_line_tokens = iter(self.TOKENIZER_REGEX.finditer(line))
            while True:
                m = next(iter_line_tokens, None) # pylint: disable=stop-iteration-return
                if m is None:
                    break
                if m.group('label'):
                    yield Token('label', m.group('label'), self.line_number, m.start('label'))
                elif m.group('id'):
                    name = m.group('id')
                    if name in self.KEYWORDS:
                        value = None
                    else:
                        value = name
                        name = 'identifier'
                    yield Token(name, value, self.line_number, m.start('id'))
                elif m.group('random'):
                    yield Token('random', None, self.line_number, m.start('random'))
                elif m.group('decimal'):
                    yield Token('literal', Parser._number_from_match(m), self.line_number, m.start('decimal'))
                elif m.group('symbol'):
                    yield Token(m.group('symbol'), None, self.line_number, m.start('symbol'))
                elif m.group('comment') or m.group('blankline'):
                    break
                elif m.group('linecontinuation'):
                    m = next(iter_line_tokens, None) # pylint: disable=stop-iteration-return
                    if m is not None and not m.group('comment') and not m.group('blankline'):
                        raise CompilationError(
                            'unexpected text after line-continuation character',
                            line_number=self.line_number,
                            char_number=m.start(m.lastgroup)
                        )
                    continues_to_next_line = True
                    break
                else:
                    raise CompilationError('unknown token', line_number=self.line_number, char_number=m.start('invalid'))
            if not continues_to_next_line:
                yield Token('end-of-line', None, self.line_number, len(line))
            self.line_number += 1
        yield Token('end-of-input', None, self.line_number, 0)

    # Check that the current token matches the specified type. Does not consume the token or otherwise modify the parser state.
    def _check(self, *expected_types):
        if self.token.type not in expected_types:
            raise CompilationError(
                'expected {0} but found {1} instead'.format(' or '.join(expected_types), self.token.type),
                line_number=self.token.line_number,
                char_number=self.token.char_number
            )

    # Advance the current token. If a token type is specified, check that the new token matches it.
    # Returns the (new) current token, as a convenience.
    def _advance(self, *args):
        t = self.token = next(self.tokens)
        if args:
            self._check(*args)
        return t

    def _end_state(self):
        if self.current_state_label_token:
            state = State(self.current_state_label_token.value, tuple(self.current_state_actions), self.current_state_label_token.line_number)
            self.states.append(state)
            self.state_dict[state.label] = state

    def _make_branch(self, condition, destination):
        if destination == self.current_state_label_token.value:
            raise CompilationError('branch to itself in state "{0}"'.format(destination), line_number=self.line_number)
        return Branch(condition, destination, self.line_number_or_range)

    # See _parse_expression for details.
    def _nud(self, t): # pylint: disable=inconsistent-return-statements
        if t.type == 'identifier':
            if self.token.type == '(':
                if t.value in FunctionCall.FUNCTIONS:
                    self._advance()
                    params = []
                    while True:
                        params.append(self._parse_expression())
                        if self.token.type == ')':
                            break
                        self._check(',')
                        self._advance()
                    self._advance()
                    min_params, max_params = FunctionCall.FUNCTIONS[t.value]
                    if not min_params <= len(params) <= max_params:
                        if min_params == max_params:
                            raise CompilationError(
                                'function {0}() accepts {1} {2} ({3} provided)'.format(
                                    t.value,
                                    min_params,
                                    'parameter' if min_params == 1 else 'parameters',
                                    len(params)
                                ),
                                line_number=t.line_number,
                                char_number=t.char_number
                            )
                        else:
                            raise CompilationError(
                                'function {0}() accepts between {1} and {2} parameters ({3} provided)'.format(
                                    t.value,
                                    min_params,
                                    max_params,
                                    len(params)
                                ),
                                line_number=t.line_number,
                                char_number=t.char_number
                            )
                    return FunctionCall(t.value, params)
                else:
                    raise CompilationError(
                        'reference to unknown function "{0}"'.format(t.value),
                        line_number=t.line_number,
                        char_number=t.char_number
                    )
            elif t.value in self.constants:
                return Literal(self.constants[t.value], t)
            elif t.value in self.variable_names or t.value in self.declared_variable_names:
                return Variable(t.value, t)
            else:
                raise CompilationError(
                    'reference to undeclared variable "{0}"'.format(t.value),
                    line_number=t.line_number,
                    char_number=t.char_number
                )
        elif t.type == 'literal':
            return Literal(t.value, t)
        elif t.type == 'random':
            return RandomValue()
        elif t.type in UnOp.OPS:
            return UnOp(t.type, self._parse_expression(1000))
        elif t.type == '(':
            inner = self._parse_expression()
            self._check(')')
            self._advance()
            return inner
        else:
            t.unexpected()

    # See _parse_expression for details.
    def _led(self, t, left): # pylint: disable=inconsistent-return-statements
        if t.type == '=':
            if self.in_assignment:
                raise CompilationError('chained assignment is not allowed - did you mean == instead?', line_number=t.line_number, char_number=t.char_number)
            if not isinstance(left, Variable):
                raise CompilationError('illegal assignment', line_number=t.line_number, char_number=t.char_number)
            self.in_assignment = True
            assignment = Assignment(left.name, self._parse_expression(), self.line_number_or_range)
            self.in_assignment = False
            return assignment
        elif t.type == '?':
            yes = self._parse_expression()
            self._check(':')
            self._advance()
            no = self._parse_expression()
            return TerOp(left, yes, no)
        elif t.type in ('in', 'not'):
            if t.type == 'in':
                negate = False
            else:
                self._check('in')
                self._advance()
                negate = True
            self._check('{', '[', '(')
            if self.token.type == '{':
                members = []
                self._advance()
                while True:
                    members.append(self._parse_expression())
                    if self.token.type == '}':
                        break
                    self._check(',')
                    self._advance()
                self._advance()
                membership = SetMembership(left, members)
            else:
                range_low_inclusive = self.token.type == '['
                self._advance()
                range_low = self._parse_expression()
                self._check(',')
                self._advance()
                range_high = self._parse_expression()
                self._check(']', ')')
                range_high_inclusive = self.token.type == ']'
                self._advance()
                membership = RangeMembership(left, range_low, range_high, range_low_inclusive, range_high_inclusive)
            return UnOp('!', membership) if negate else membership
        elif t.type in ('||', '&&'):
            right = self._parse_expression(self.LBP[t.type])
            predicates = (left.predicates if isinstance(left, LogicalOp) and left.op == t.type else [left]) + \
                         (right.predicates if isinstance(right, LogicalOp) and right.op == t.type else [right])
            return LogicalOp(t.type, predicates)
        elif t.type in BinOp.LEFT_ASSOCIATIVE_OPS:
            return BinOp(left, t.type, self._parse_expression(self.LBP[t.type]))
        elif t.type in BinOp.RIGHT_ASSOCIATIVE_OPS:
            return BinOp(left, t.type, self._parse_expression(self.LBP[t.type] - 1))
        else:
            t.unexpected() # pragma: nocover

    # The expression subparser is implemented using a top-down operator precedence parser,
    # aka a Pratt parser. The nud and led functions are implemented on the parser itself,
    # rather than on the token objects. This is quite a bit more efficient than the typical
    # approach, where each token type has its own token class, which inherits from a base
    # token class; it also substantially reduces the number of lines of code.
    def _parse_expression(self, rbp=0):
        t = self.token
        self._advance()
        left = self._nud(t)
        while rbp < self.LBP[self.token.type]:
            t = self.token
            self._advance()
            left = self._led(t, left)
        return left

    def _parse_statement(self):

        # blank line
        if self.token.type == 'end-of-line':
            return

        # variable declaration
        elif self.token.type == 'let':
            if self.current_state_label_token:
                raise CompilationError('variables must be declared before the first state definition')
            self._advance('identifier')
            declared_variable_name = self.token.value
            if declared_variable_name in self.constants:
                raise CompilationError(
                    'redeclaration of constant "{0}"'.format(declared_variable_name),
                    line_number=self.token.line_number,
                    char_number=self.token.char_number
                )
            if declared_variable_name in self.variable_names or declared_variable_name in self.declared_variable_names:
                raise CompilationError(
                    'redeclaration of variable "{0}"'.format(declared_variable_name),
                    line_number=self.token.line_number,
                    char_number=self.token.char_number
                )
            self._advance('=')
            self._advance()
            self.in_assignment = True
            expr = self._parse_expression()
            self.in_assignment = False
            self.declared_variable_names.add(declared_variable_name)
            self.initializations.append(Assignment(declared_variable_name, expr, self.line_number_or_range))
            self._check('end-of-line')

        # state declaration
        elif self.token.type == 'label':
            label_token = self.token
            self._advance(':')
            self._end_state()
            if label_token.value in self.state_dict:
                raise CompilationError('duplicate label "{0}"'.format(label_token.value), line_number=self.line_number)
            self.current_state_label_token = label_token
            self.current_state_actions = []
            self._advance()

        elif not self.current_state_label_token:
            raise CompilationError('missing start state label')

        # unconditional branch
        elif self.token.type == '=>':
            label = self._advance('label').value
            self.current_state_actions.append(self._make_branch(None, label))
            self._advance()

        # assignment or conditional branch
        else:
            expr = self._parse_expression()
            if isinstance(expr, Assignment):
                self.current_state_actions.append(expr)
            else:
                self._check('=>')
                label = self._advance('label').value
                self.current_state_actions.append(self._make_branch(expr, label))
                self._advance()

    # Adapted from Wikipedia's article on Tarjan Strongly Connected Components Algorithm.
    # http://en.wikipedia.org/wiki/Tarjan%E2%80%99s_strongly_connected_components_algorithm
    def _strongly_connected_components(self):
        stack = []
        stack_set = set() # used to keep the "w in stack" check O(1)
        indices = {}
        lowlinks = {}
        available_index = itertools.count()
        components = []

        def strongconnect(v):

            # Set the depth index for v to the smallest unused index.
            indices[v] = lowlinks[v] = next(available_index)
            stack.append(v)
            stack_set.add(v)

            # Follow branches out from this node.
            for branch in self.state_dict[v].branches:
                w = branch.destination
                if w not in lowlinks:
                    # The successor has not been visited, so visit it.
                    strongconnect(w)
                    lowlinks[v] = min(lowlinks[v], lowlinks[w])
                elif w in stack_set:
                    # Successor has already been visited, so it is an SCC.
                    lowlinks[v] = min(lowlinks[v], indices[w])

            # If node is a root node, pop the stack and generate an SCC.
            if lowlinks[v] == indices[v]:
                component = []
                while True:
                    w = stack.pop()
                    stack_set.remove(w)
                    component.append(w)
                    if w == v:
                        break
                components.append(component)

        for state in self.states:
            if state.label not in lowlinks:
                strongconnect(state.label)

        return components

    def parse(self, source_code):

        # Tokenize the source code.
        self.tokens = self._tokenize(source_code)

        # Parse the source code.
        while self._advance().type != 'end-of-input':
            self._parse_statement()
            self._check('end-of-line')
        self._end_state()

        # Make sure the source code isn't completely empty.
        if not self.states:
            raise CompilationError('no states are defined')

        # Make sure there aren't any branches that jump to undefined labels.
        labels = {state.label for state in self.states}
        undefined_branch = next((branch for state in self.states for branch in state.branches if branch.destination not in labels), None)
        if undefined_branch:
            raise CompilationError(
                'branch to undefined label "{}"'.format(undefined_branch.destination),
                line_number=undefined_branch.line_number
            )

        # Detect cycles. Tarjan's algorithm doesn't catch pathological cycles (node connected to itself)
        # but we already checked for that while parsing the source code.
        cycle = next((component for component in self._strongly_connected_components() if len(component) > 1), None)
        if cycle:
            cycle_states = ', '.join('"' + label + '"' for label in cycle)
            raise CompilationError('cycle exists between states ' + cycle_states)

        return AST(self.declared_variable_names, self.initializations, self.states)


class IRInstruction(object):

    __slots__ = ('opcode', 'param', 'labels', 'line_number')

    def __init__(self, opcode, param, labels, line_number):
        self.opcode = opcode
        self.param = param
        self.labels = labels
        self.line_number = line_number

    def generate_code(self, code_generator):
        opcode = self.opcode
        param = self.param
        if opcode in ('Lv', 'St'):
            param = code_generator.variable_slot_assignments[param]
        elif opcode == 'Lc':
            param = code_generator.constant_slot_assignments[param]
        elif opcode in ('Jn', 'Jz', 'Ju'):
            param = str(code_generator.label_positions[param])
        else:
            param = ''
        return opcode + param


class CodeGenerator(object):

    __slots__ = ('variable_names', 'constants', 'instructions', 'pending_labels', 'last_allocated_label',
                 'emitting_line_number', 'variable_slot_assignments', 'constant_slot_assignments', 'label_positions')

    def __init__(self, variable_names, constants):
        self.variable_names = variable_names
        self.constants = constants
        self.instructions = []
        self.pending_labels = []
        self.last_allocated_label = 0
        self.emitting_line_number = None
        self.variable_slot_assignments = None
        self.constant_slot_assignments = None
        self.label_positions = None

    def emit(self, opcode, param=None):
        instruction = IRInstruction(opcode, param, self.pending_labels, self.emitting_line_number)
        self.pending_labels = []
        self.instructions.append(instruction)
        return instruction

    def label_next_instruction(self, label):
        self.pending_labels.append(label)

    def allocate_label(self):
        self.last_allocated_label += 1
        return self.last_allocated_label

    def _optimize(self):

        instructions = self.instructions

        # Condense chains of unconditional jumps.
        label_to_instruction = {label: (i, instruction) for i, instruction in enumerate(instructions) for label in instruction.labels}
        for instruction in instructions:
            if instruction.opcode in ('Jn', 'Jz', 'Ju'):
                target_label = instruction.param
                while True:
                    target_instruction = label_to_instruction[target_label][1]
                    if target_instruction.opcode != 'Ju':
                        break
                    instruction.param = target_label = target_instruction.param
                if instruction.opcode == 'Ju' and target_instruction.opcode == 'Xx':
                    instruction.opcode = 'Xx'
                    instruction.param = None

        # Delete unreachable instructions.
        reachable = [False] * len(instructions)
        queue = [0]
        while queue:
            i = queue.pop()
            if not reachable[i]:
                reachable[i] = True
                instruction = instructions[i]
                if instruction.opcode != 'Xx':
                    if instruction.opcode in ('Jn', 'Jz', 'Ju'):
                        queue.append(label_to_instruction[instruction.param][0])
                    if instruction.opcode != 'Ju':
                        queue.append(i + 1)
        instructions = [instruction for i, instruction in enumerate(instructions) if reachable[i]]

        # Delete unconditional jumps to the next instruction.
        label_to_instruction = {label: (i, instruction) for i, instruction in enumerate(instructions) for label in instruction.labels}
        for i, instruction in enumerate(instructions):
            if instruction.opcode == 'Ju' and label_to_instruction[instruction.param][0] == i + 1:
                label_to_instruction[instruction.param][1].labels.extend(instruction.labels)
                instruction.opcode = None
        instructions = [instruction for instruction in instructions if instruction.opcode is not None]
        self.instructions = instructions

    def generate_code(self, initializations, states):

        # Generate IR instructions.
        # Instruction params will be symbolic, and will be replaced with final "linked" values
        # during the codegen pass at the end.
        for initialization in initializations:
            initialization.emit(self)
        for state in states:
            state.emit(self)
        if not self.instructions or self.instructions[-1].opcode != 'Xx':
            self.emit('Xx')
        assert not self.pending_labels
        self._optimize()

        # Assign variables to slots so that the most-frequently-used variables
        # get the lowest-numbered slots. Use lexographical sort as a tiebreaker
        # to ensure stable results for unit-testing purposes.
        variable_use_counts = defaultdict(int)
        for instruction in self.instructions:
            if instruction.opcode in ('Lv', 'St'):
                variable_use_counts[instruction.param] += 1
        variable_slots = sorted(self.variable_names, key=lambda v: (-variable_use_counts[v], v))
        self.variable_slot_assignments = {v: str(slot_number) for slot_number, v in enumerate(variable_slots)}

        # Assign constants to slots so that the most-frequently-used constants
        # get the lowest-numbered slots. Use lexographical sort as a tiebreaker
        # to ensure stable results for unit-testing purposes.
        constant_use_counts = defaultdict(int)
        for instruction in self.instructions:
            if instruction.opcode == 'Lc':
                if instruction.param == '0':
                    instruction.opcode = 'Lz'
                    instruction.param = None
                elif instruction.param == '1':
                    instruction.opcode = 'Lo'
                    instruction.param = None
                else:
                    constant_use_counts[instruction.param] += 1
        constant_slots = sorted(constant_use_counts.keys(), key=lambda c: (-constant_use_counts[c], c))
        self.constant_slot_assignments = {c: str(slot_number) for slot_number, c in enumerate(constant_slots)}

        # Determine final label positions.
        label_positions = {}
        for position, instruction in enumerate(self.instructions):
            for label in instruction.labels:
                label_positions[label] = position
        self.label_positions = label_positions

        program = ';'.join([
            '|'.join(variable_slots),
            '|'.join(constant_slots),
            ''.join(instruction.generate_code(self) for instruction in self.instructions),
        ])

        source_map = tuple(instruction.line_number for instruction in self.instructions)
        return (program, source_map)


def canonicalize_number_literal(s):
    """
    Canonicalizes a number literal from an Absymal program.

    Returns the canonical string format of the parsed number,
    or None if parsing failed.
    """
    return Parser.parse_number(s)


def compile(source_code, variable_names, constants): # pylint: disable=redefined-builtin
    """
    Compiles an Absymal program.

    Returns a tuple containing 2 items:
      * the compiled program
      * the source map (used for coverage)
    """
    variable_names = frozenset(variable_names)
    constants = dict(constants) if constants is not None else {}

    # Check that variables and constants don't share the same names.
    shared_names = variable_names & set(constants.keys())
    if shared_names:
        shared_name = next(iter(shared_names))
        raise ValueError('"{0}" cannot be both a variable and a constant'.format(shared_name))

    # Check that constants have valid values and convert any integer or float values to Decimal.
    for name, value in constants.items():
        if isinstance(value, int):
            # The int case also handles bool values.
            constants[name] = Decimal(value)
        elif isinstance(value, float):
            # We format the value with str() before turning it into a Decimal
            # so that, for example, 1/10 gets turned into Decimal('0.1') rather than
            # Decimal('0.1000000000000000055511151231257827021181583404541015625').
            constants[name] = Decimal(str(value))
        elif not isinstance(value, Decimal):
            raise ValueError('the value of constant "{0}" ({1!r}) is not an int, float, or Decimal'.format(name, value))

    ast = Parser(variable_names, constants).parse(source_code)
    ast = ast.optimize()

    dsmal, source_map = CodeGenerator(
        variable_names=list(variable_names) + list(ast.declared_variable_names),
        constants={constant_name: str(constant_value) for constant_name, constant_value in constants.items()}
    ).generate_code(ast.initializations, ast.states)

    return (dsm.Program(dsmal), source_map)
