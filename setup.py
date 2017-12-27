#!/usr/bin/env python

from __future__ import absolute_import
from __future__ import print_function

import io
import os
import re
import sys
from glob import glob
from os.path import basename, dirname, join, relpath, splitext

from setuptools import Extension, find_packages, setup


if not sys.version_info >= (3, 5):
    raise Exception('*** abysmal only supports Python 3.5 and above ***')


ENABLE_C_ASSERT = False


def read(*names, **kwargs):
    return io.open(
        join(dirname(__file__), *names),
        encoding=kwargs.get('encoding', 'utf8')
    ).read()


setup(
    name='abysmal',
    version='0.3.0',
    license='MIT license',
    description='Abysmal (Appallingly Basic Yet Somehow Mostly Adequate Language)',
    long_description='%s\n%s' % (
        re.compile('^.. include-documentation-end-marker.*^.. include-documentation-end-marker', re.M | re.S).sub('', read('README.rst')),
        re.sub(':[a-z]+:`~?(.*?)`', r'``\1``', read('CHANGELOG.rst'))
    ),
    author='John-Anthony Owens',
    author_email='johnao@zillowgroup.com',
    url='https://github.com/johnanthonyowens/abysmal',
    packages=find_packages('src'),
    package_dir={'': 'src'},
    py_modules=[splitext(basename(path))[0] for path in glob('src/*.py')],
    include_package_data=True,
    zip_safe=False,
    classifiers=[
        # complete classifier list: http://pypi.python.org/pypi?%3Aaction=list_classifiers
        'Development Status :: 5 - Production/Stable',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Operating System :: Unix',
        'Operating System :: POSIX',
        'Programming Language :: Python',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: Implementation :: CPython',
        'Topic :: Utilities',
    ],
    keywords=['absymal', 'programming', 'language'],
    ext_modules=[
        Extension(
            splitext(relpath(path, 'src').replace(os.sep, '.'))[0],
            sources=[path],
            include_dirs=[dirname(path)],
            libraries=['mpdec'],
            undef_macros=['NDEBUG'] if ENABLE_C_ASSERT else [],
            extra_compile_args=['-O3'],  # setuptools specifies -O2 -- override it
            extra_link_args=['-O3'],     # setuptools specifies -O1 -- override it
        )
        for root, _, _ in os.walk('src')
        for path in glob(join(root, '*.c'))
    ]
)
