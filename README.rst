=======
Abysmal
=======

.. include-documentation-begin-marker

.. image:: https://travis-ci.org/zillow/abysmal.svg?branch=master
        :target: https://travis-ci.org/zillow/abysmal

.. image:: https://codecov.io/gh/zillow/abysmal/branch/master/graph/badge.svg
        :target: https://codecov.io/gh/zillow/abysmal

Abysmal stands for "appallingly simple yet somehow mostly adequate language".

Abysmal is a programming language designed to allow non-programmers
to implement simple business logic for computing prices, rankings, or
other kinds of numeric values without incurring the security and
stability risks that would normally result when non-professional coders
contribute to production code. In other words, it's a sandbox in which
businesspeople can tinker with their business logic to their hearts'
content without involving your developers or breaking anything.


Features
--------

* Supports Python 3.5 and above


Dependencies
------------

* `python3-dev` native library including Python C header files
* `libmpdec-dev` native library for decimal arithmetic


.. include-documentation-end-marker


Language Reference
------------------

Abysmal programs are designed to be written by businesspeople, so the
language foregoes almost all the features programmers want in a programming
language in favor of mimicking something business people understand:
flowcharts.

Just about the only way your businesspeople can "crash" an Abysmal program
is by dividing by zero, because:

* it's not Turing-complete
* it can't allocate memory
* it can't access the host process or environment
* it operates on one and only one type: arbitrary-precision decimal numbers
* its only control flow construct is GOTO
* it doesn't even allow loops!

Example program
~~~~~~~~~~~~~~~

::

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
        price = scoops * (flavor == STRAWBERRY ? 1.25 : 1.00)
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


Control flow
~~~~~~~~~~~~

An Abysmal program models a flowchart containing one or more steps, or *states*.
Program execution begins at the beginning of the first state and continues
until it reaches a dead end. Along the way, variables can be assigned new
values, and execution can jump to other states. **That's it.**

Every state has a name that starts with `@`. A state is declared like this:

::

    @start:

A state declaration is followed by a sequence of *actions*. Each action appears
on its own line, and is one of the following:

(1) an *assignment* of a value to a variable, like this:

::

    price = scoops * flavor == STRAWBERRY ? 1.25 : 1.00

(2) a *conditional jump* to another state, like this:

::

    weekday not in {SATURDAY, SUNDAY} => @apply_weekday_discount

(3) an *unconditional jump* to another state, like this:

::

    => @compute_total

When execution reaches a state, that state's actions are executed in order.
If execution reaches the end of a state without jumping to a new state, the
program exits.

Actions are typically indented to make the state labels easier to see, but
this is just a stylistic convention and is not enforced by the language.

Comments
~~~~~~~~

Anything following a `#` on a line is treated as a comment and is ignored.

Line continuations
~~~~~~~~~~~~~~~~~~

A `\\` at the end of a line indicates that the next line is a continuation of
the line. This makes it easy to format long lines readably by splitting them
into multiple, shorter lines. Note that comments can appear after a `\\`.

Numbers
~~~~~~~

Abysmal supports integers and fixed-point decimal numbers like `123`,
`3.14159`, etc. In addition, numbers can have the following suffixes:

==========  ======================================================
suffix      meaning
==========  ======================================================
`%`         percent (`12.5%` is equivalent to `0.125`)
`k` or `K`  thousand (`50k` is equivalent to `50000`)
`m` or `M`  million (`1.2m` is equivalent to `1200000`)
`b` or `B`  billion (`0.5b` is equivalent to `500000000`)
==========  ======================================================

Scientific notation is not supported.

Booleans
~~~~~~~~

Abysmal uses `1` and `0` to represent the result of any operation that
yields a logical true/false value. When evaluating conditions in a
conditional jump or a `?` expression, zero is considered false and
any non-zero value is considered true.

Expressions
~~~~~~~~~~~

Programs can evaluate expressions containing the following operators
(in descending order of precedence):

======================  ======================================================================
operator                meaning
======================  ======================================================================
`( exp )`               grouping; e.g. `(x + 1) * y`
`!`, `+`, `-`           logical NOT, unary plus, unary minus
`^`                     exponentiation (right associative)
`*`, `/`                multiplication, division
`+`, `-`                addition, subtraction
`in { members }`
`not in { members }`    set membership; e.g. `x not in {1, 4, 9, 16}`
`<`, `<=`, `>`, `>=`    comparison
`==`, `!=`              equality, inequality
`&&`                    logical AND
`||`                    logical OR
`exp ? exp : exp`       if-then-else; e.g. `x < 0 ? -x : x`
======================  ======================================================================

Functions
~~~~~~~~~

Expressions can take advantage of the following built-in functions:

======================  ======================================================================
function                returns
======================  ======================================================================
`ABS(exp)`              the absolute value of the specified value
`CEILING(exp)`          the nearest integer value greater than or equal to the specified value
`FLOOR(exp)`            the nearest integer value less than or equal to the specified value
`MAX(exp1, exp2, ...)`  the maximum of the specified values
`MIN(exp1, exp2, ...)`  the minimum of the specified values
`ROUND(exp)`            the specified value, rounded to the nearest integer
======================  ======================================================================

Variables
~~~~~~~~~

Abysmal programs can read from and write to variables that you define
when you compile the program. Some of these variables will be inputs,
whose values you will set before you run the program. Others will be outputs,
whose values the program will compute so that those values can be examined
after the program has terminated.

Abysmal does not distinguish between input and output variables.

*All* variables and constant values are decimal numbers. Abysmal does not
have any concept of strings, booleans, null, or any other types.

If not explicitly set, variables default to 0.

`random!` is a special, read-only variable that yields a new, random value
every time it is referenced.

You can also provide named constants to your programs when you compile them.
Constants cannot be modified.

A program can also declare custom variables that it can use to store
intermediate results while the model is being run, or simply to define
friendlier names for values that are used within the model. Custom variables
must be declared before the first state is declared.

Each custom variable is declared on its own line, like this:

::

    let PI = 3.14159
    let area = PI * r * r


Usage
-----

An Abysmal program must be compiled before it can be run. The compiler needs
to know the names of the variables that the program should have access to
and names and values of any constants you want to define:

.. code-block:: python

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

    compiled_program, source_map = abysmal.compile(source_code, ICE_CREAM_VARIABLES, ICE_CREAM_CONSTANTS)

Ignore the second value returned by `abysmal.compile()` for now (refer to the
Measuring Coverage section to see what it's useful for).

Next, need to make a virtual machine for the compiled program to run on:

.. code-block:: python

    machine = compiled_program.machine()

Next, we can set any variables as we see fit:

.. code-block:: python

    # Variables can be set in bulk during reset()...
    machine.reset(
        flavor=ICE_CREAM_CONSTANTS['CHOCOLATE'],
        scoops=2,
        cone=ICE_CREAM_CONSTANTS['WAFFLE']
    )

    # ... or one at a time (though this is less efficient)
    machine['sprinkles'] = True  # automatically converted to '1'

Finally, we can run the machine and examine final variable values:

.. code-block:: python

    price = Decimal('0.00')
    try:
        machine.run()
        price = round(Decimal(machine['price']), 2)
    except abysmal.ExecutionError as ex:
        print('The ice cream pricing algorithm is broken: ' + str(ex))
    else:
        print('Two scoops of chocolate ice cream in a waffle cone with sprinkles costs: ${0}'.format(price))

Note that the virtual machine treats variable values as strings.
Variables can be set from int, float, bool, Decimal, and string values
but are converted to strings when assigned. When examining variables
after running a machine, you need to convert to the values back to
Decimal, float, or whatever numeric type you are interested in.


Random Numbers
--------------

By default, `random!` generates numbers between 0 and 1 with 9 decimal
places of precision, and uses the default Python PRNG (`random.randrange`).

If you require a more secure PRNG, or different precision, or if you want
to force certain values to be produced for testing purposes, you can supply
your own random number iterator before running a machine:

.. code-block:: python

    # force random! to yield 0, 1, 0, 1, ...
    machine.random_number_iterator = itertools.cycle([0, 1])

The values you return are not required to fall within any particular
range, but [0, 1] is recommended, for consistency with the default behavior.


Errors
------

`abysmal.CompilationError`
    raised by `abysmal.compile()` if the source code cannot be compiled
`abysmal.ExecutionError`
    raised by `machine.run()` and `machine.run_with_coverage()`
    if a program encounters an error while running; this includes conditions
    such as: division by zero, invalid exponentiation, stack overflow,
    out-of-space, and failure to generate a random number
`abysmal.InstructionLimitExceededError`
    raised by `machine.run()` and `machine.run_with_coverage()`
    if a program exceeds its allowed instruction count and is aborted;
    this error is a subclass of `abysmal.ExecutionError`


Performance Tips
----------------

Abysmal programs run very quickly once compiled, and the virtual machine is
optimized to make repeated runs with different inputs as cheap as possible.
To get the best performance, follow these tips:

Avoid recompilation
~~~~~~~~~~~~~~~~~~~

Save the compiled program and reuse it rather than recompiling every time.
Compiled programs are pickleable, so they are easy to cache.

Use baseline images
~~~~~~~~~~~~~~~~~~~

When you create a machine, you can pass keyword arguments to set the machine's
variables to initial values. The state of the variables at this moment is
called a *baseline image*. When you reset a machine, it restores all variables
to the baseline image very efficiently. Therefore, if you are going to run a
particular program repeatedly with some inputs having the same values for all
the runs, you should specify those input values in the baseline.

For example:

.. code-block:: python

    def compute_shipping_costs(product, weight, zip_codes, compiled_program):
        shipping_costs = {}
        machine = compiled_program.machine(product=product, weight=weight)
        for zip_code in zip_codes:
            machine.reset(zip=zip_code).run()
            shipping_costs[zip_code] = round(Decimal(machine['shippingCost']), 2)
        return shipping_costs

Limit instruction execution
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Since Abysmal does not support loops, it is very difficult to create a program
that runs for very long. However, you can impose an additional limit on the
number of instructions that a program can execute by setting the `instruction_limit`
attribute of a machine:

.. code-block:: python

    machine.instruction_limit = 5000

If a program exceeds its instruction limit, it will raise an `abysmal.InstructionLimitExceededError`.

The default instruction limit is 10000.

The `run()` method returns the number of instructions that were run before
the program exited.


Measuring Coverage
------------------

In addition to `run()`, virtual machines expose a `run_with_coverage()` method
which can be used in conjunction with the source map returned by
`abysmal.compile()` to generate coverage reports for Abysmal programs.

.. code-block:: python

    coverage_tuples = [
        machine.reset(**test_case_inputs).run_with_coverage()
        for test_case_inputs in test_cases
    ]
    coverage_report = abysmal.get_uncovered_lines(source_map, coverage_tuples)
    print('Partially covered lines: ' + ', '.join(map(str, coverage_report.partially_covered_line_numbers)))
    print('Totally uncovered lines: ' + ', '.join(map(str, coverage_report.uncovered_line_numbers))

How coverage works:

`run_with_coverage()` returns a *coverage tuple* whose length is equal
to the number of instructions in the compiled program. The value at index *i*
in the coverage tuple will be True or False depending on whether instruction
*i* was executed during the program's run.

The *source map* is another tuple, with the same length as the coverage tuple.
The value at index *i* in the source map indicates which line or lines in the
source code generated instruction *i* of the compiled program. There are three
possibilities:

* None - the instruction was not directly generated by any source line
* int - the instruction was generated by a single source line
* (int, int, ...) - the instruction was generated by multiple source lines
  (due to line continuations being used)


Installation
------------

Note that native library dependencies must be installed BEFORE
you install the `abysmal` library.

.. code-block:: console

    pip install abysmal


Development
-----------

.. code-block:: console

    # Install system-level dependencies on Debian/Ubuntu
    make setup

    # Run unit tests
    make test

    # Check code cleanliness
    make pylint

    # Check code coverage
    make cover

    # Create sdist package
    make package
