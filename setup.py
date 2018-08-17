"""A setuptools based setup module.

See:
https://packaging.python.org/en/latest/distributing.html
https://github.com/brmmm3/circularbuffer
"""

import os

from distutils.core import setup, Extension

PKGVERSION = "0.1.0"
BASEDIR = os.path.dirname(__file__)
PKGNAME = 'circularbuffer'
PKGDIR = os.path.join(BASEDIR, PKGNAME)

# Get the long description from the README file
with open(os.path.join(BASEDIR, 'README.rst'), encoding='utf-8') as F:
    long_description = F.read()


setup(
    name=PKGNAME,
    version=PKGVERSION,
    description='An efficient and fast circular buffer implementation in C.',
    long_description=long_description,
    long_description_content_type='text/x-rst',

    url='https://github.com/brmmm3/' + PKGNAME,
    download_url = 'https://github.com/brmmm3/%s/releases/download/%s/%s-%s.tar.gz' % (PKGNAME, PKGVERSION, PKGNAME, PKGVERSION),

    author='Martin Bammer',
    author_email='mrbm74@gmail.com',
    license='MIT',

    classifiers=[  # Optional
        'Development Status :: 5 - Production/Stable',

        'Intended Audience :: Developers',
        'Topic :: Software Development :: Build Tools',

        'License :: OSI Approved :: MIT License',

        'Operating System :: OS Independent',

        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.4',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
    ],

    keywords='fast circular buffer',
    include_package_data=True,
    ext_modules=[Extension(PKGNAME, [os.path.join(PKGDIR, "circularbuffer.c")])]
)

