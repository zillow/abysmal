#!/bin/sh -e

if [ -z "$TRAVIS" ]; then
    echo "$0 should only be run in the Travis-ci environment."
    exit 1
fi

sudo apt-get -qq update
sudo apt-get install -y python3-dev libmpdec-dev
pip install pylint coverage
