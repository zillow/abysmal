__version__ = '1.2.0'


# import dependencies using private aliases to avoid "exporting" them
import decimal as _decimal
import random as _random
from . import dsm as _dsm # pylint: disable=import-error, no-name-in-module


# expose abysmal API as top-level exports
from .compiler import compile, CompilationError # pylint: disable=redefined-builtin
from .coverage import get_uncovered_lines, CoverageReport
from .dsm import ExecutionError, InstructionLimitExceededError # pylint: disable=import-error, no-name-in-module


# yields values in the range [0, 1] with 9 decimal digits of randomness
def _random_numbers():
    random_range = 1000000000
    while True:
        yield _decimal.Decimal(_random.randrange(random_range)) / random_range


_dsm.random_number_iterator = iter(_random_numbers())
