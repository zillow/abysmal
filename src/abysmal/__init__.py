__version__ = '0.3.0'

from decimal import Decimal
import random

from . import dsm as _dsm

# expose as top-level exports
from .compiler import compile, CompilationError # pylint: disable=redefined-builtin
from .coverage import get_uncovered_lines, CoverageReport
from .dsm import ExecutionError # pylint: disable=no-name-in-module


def _random_numbers():
    random_range = 10 ** 9 # 9 decimal digits of randomness
    while True:
        yield Decimal(random.randrange(random_range)) / random_range


_dsm.random_number_iterator = iter(_random_numbers())
