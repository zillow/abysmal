#!/bin/sh -e

if [ -z "$TRAVIS" ]; then
    echo "$0 should only be run in the Travis-ci environment."
    exit 1
fi

python3 setup.py sdist
pip install dist/abysmal*.tar.gz
