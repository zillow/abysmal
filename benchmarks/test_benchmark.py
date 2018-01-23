from decimal import Decimal
import timeit
import unittest

import abysmal


class Test_benchmark(unittest.TestCase):

    def test_ice_cream_price(self):

        # abysmal implementation

        FLAVOR_CONSTANTS = {
            'VANILLA': 1,
            'CHOCOLATE': 2,
            'STRAWBERRY': 3,
        }
        CONE_CONSTANTS = {
            'SUGAR': 1,
            'WAFFLE': 2,
        }
        WEEKDAY_CONSTANTS = {
            'MONDAY': 1,
            'TUESDAY': 2,
            'WEDNESDAY': 3,
            'THURSDAY': 4,
            'FRIDAY': 5,
            'SATURDAY': 6,
            'SUNDAY': 7,
        }

        SOURCE_CODE = '''\
let TAX_RATE = 5.3%
let WEEKDAY_DISCOUNT = 25%

@start:
    price = scoops * (flavor == STRAWBERRY ? 1.25 : 1)
    price = price + (cone == WAFFLE ? 1.00 : 0.00)
    price = price + (sprinkles * 0.25)
    weekday not in {SATURDAY, SUNDAY} => @apply_weekday_discount
    => @compute_total

@apply_weekday_discount:
    price = price * (1 - WEEKDAY_DISCOUNT)
    => @compute_total

@compute_total:
    price = price * (1 + TAX_RATE)
'''

        program, _ = abysmal.compile(
            SOURCE_CODE,
            {
                'flavor',
                'scoops',
                'cone',
                'sprinkles',
                'weekday',
                'price',
            },
            dict(**FLAVOR_CONSTANTS, **CONE_CONSTANTS, **WEEKDAY_CONSTANTS)
        )
        machine = program.machine(
            flavor=FLAVOR_CONSTANTS['VANILLA'],
            scoops=1,
            cone=CONE_CONSTANTS['SUGAR'],
            sprinkles=False,
            weekday=WEEKDAY_CONSTANTS['MONDAY']
        )

        # native implementation

        STRAWBERRY_MULTIPLIER = Decimal('1.25')
        WAFFLE_CONE_COST = Decimal('1.00')
        SPRINKLES_COST = Decimal('0.25')
        WEEKDAY_MULTIPLIER = Decimal('0.75')
        TAX_MULTIPLIER = Decimal('1.053')

        def native(flavor, scoops, cone, sprinkles, weekday):
            price = Decimal(scoops)
            if flavor == FLAVOR_CONSTANTS['STRAWBERRY']:
                price *= STRAWBERRY_MULTIPLIER
            if cone == CONE_CONSTANTS['WAFFLE']:
                price += WAFFLE_CONE_COST
            if sprinkles:
                price += SPRINKLES_COST
            if weekday not in (WEEKDAY_CONSTANTS['SATURDAY'], WEEKDAY_CONSTANTS['SUNDAY']):
                price *= WEEKDAY_MULTIPLIER
            price = price * TAX_MULTIPLIER
            return price

        # test cases

        cases = [
            {
                'flavor': flavor,
                'scoops': scoops,
                'cone': cone,
                'sprinkles': sprinkles,
                'weekday': weekday,
            }
            for flavor in FLAVOR_CONSTANTS.values()
            for scoops in (1, 2, 3)
            for cone in CONE_CONSTANTS.values()
            for sprinkles in (False, True)
            for weekday in WEEKDAY_CONSTANTS.values()
        ]

        def run_abysmal():
            for case in cases:
                machine.reset(**case).run()
                _ = Decimal(machine['price'])

        def run_native():
            for case in cases:
                _ = native(**case)

        number = 1000
        runs = number * len(cases)
        abysmal_us = 1000000 * min(timeit.repeat(stmt='run_abysmal()', number=number, repeat=5, globals=locals())) / runs
        native_us = 1000000 * min(timeit.repeat(stmt='run_native()', number=number, repeat=5, globals=locals())) / runs
        print('''
ICE CREAM PRICE BENCHMARK RESULTS:
  abysmal : {0:.3f} us/run
  native  : {1:.3f} us/run'''.format(abysmal_us, native_us))
