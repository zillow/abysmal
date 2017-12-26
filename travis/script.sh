#!/bin/sh -e

if [ -z "$TRAVIS" ]; then
    echo "$0 should only be run in the Travis-ci environment."
    exit 1
fi

python3 -m unittest -v tests/test_*.py
python3 -E -m coverage run --branch --source 'abysmal' -m unittest tests/test_*.py
python3 -E -m coverage xml
python3 -E -m coverage report
