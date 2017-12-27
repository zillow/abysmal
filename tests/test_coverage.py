import itertools
import unittest

import abysmal


ICE_CREAM_VARIABLES = {
    # inputs
    'flavor',
    'scoops',
    'cone',
    'sprinkles',
    'weekday',

    # outputs
    'price',
}

ICE_CREAM_CONSTANTS = {
    # flavors
    'VANILLA': 1,
    'CHOCOLATE': 2,
    'STRAWBERRY': 3,

    # cones
    'SUGAR': 1,
    'WAFFLE': 2,

    # weekdays
    'MONDAY': 1,
    'TUESDAY': 2,
    'WEDNESDAY': 3,
    'THURSDAY': 4,
    'FRIDAY': 5,
    'SATURDAY': 6,
    'SUNDAY': 7,
}

ICE_CREAM_SOURCE_CODE = '''\
# input variables:
#
#    flavor:         VANILLA, CHOCOLATE, or STRAWBERRY
#    scoops:         1, 2, etc.
#    cone:           SUGAR or WAFFLE
#    sprinkles:      0 or 1
#    weekday:        MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY, or SUNDAY

# output variables:
#
#    price:          total price, including tax

let TAX_RATE = 5.3%
let WEEKDAY_DISCOUNT = 25%
let GIVEAWAY_RATE = 1%

@start:
    random! <= GIVEAWAY_RATE => @giveaway_winner
    price = scoops * \\
        (flavor == STRAWBERRY ? 1.25 : 1.00)
    price = price + (cone == WAFFLE ? 1.00 : 0.00)
    price = price + (sprinkles * 0.25)
    weekday not in {SATURDAY, SUNDAY} => @apply_weekday_discount
    => @compute_total

@apply_weekday_discount:
    price = price * (1 - WEEKDAY_DISCOUNT)
    => @compute_total

@giveaway_winner:
    price = 0.00

@compute_total:
    price = price * (1 + TAX_RATE)
'''


class Test_coverage(unittest.TestCase):

    def test_get_uncovered_lines(self):
        compiled_program, source_map = abysmal.compile(ICE_CREAM_SOURCE_CODE, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)
        machine = compiled_program.machine(
            sprinkles=False,
            weekday=ICE_CREAM_CONSTANTS['SATURDAY']
        )
        machine.random_number_iterator = itertools.cycle([1]) # random! is always 1

        coverage_tuples = [
            machine.reset(flavor=flavor, scoops=scoops, cone=cone).run_with_coverage()
            for flavor in [ICE_CREAM_CONSTANTS['VANILLA'], ICE_CREAM_CONSTANTS['CHOCOLATE']]
            for scoops in [1, 2, 3]
            for cone in [ICE_CREAM_CONSTANTS['SUGAR'], ICE_CREAM_CONSTANTS['WAFFLE']]
        ]
        self.assertEqual(
            abysmal.get_uncovered_lines(source_map, coverage_tuples),
            abysmal.CoverageReport(
                partially_covered_line_numbers=[19, 20, 23],
                uncovered_line_numbers=[27, 28, 31]
            )
        )

        # No runs means all lines are uncovered.
        self.assertEqual(
            abysmal.get_uncovered_lines(source_map, []),
            abysmal.CoverageReport(
                partially_covered_line_numbers=[],
                uncovered_line_numbers=[18, 19, 20, 21, 22, 23, 24, 27, 28, 31, 34]
            )
        )
