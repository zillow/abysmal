#!/bin/sh -e

if [ -z "$TRAVIS" ]; then
    echo "$0 should only be run in the Travis-ci environment."
    exit 1
fi

python3 setup.py sdist
ABYSMAL_COVER=1 ABYSMAL_DEBUG=1 pip install dist/abysmal*.tar.gz
