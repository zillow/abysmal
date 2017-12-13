__version__ = '0.1.0'

from decimal import Decimal
import random

from .compiler import compile, CompilationError # pylint: disable=redefined-builtin
from .coverage import get_uncovered_lines, CoverageReport
from .dsm import ExecutionError # pylint: disable=no-name-in-module


def _random_numbers():
    random_range = 10 ** 9 # 9 decimal digits of randomness
    while True:
        yield Decimal(random.randrange(random_range)) / random_range


DEFAULT_RANDOM_NUMBER_ITERATOR = iter(_random_numbers())
