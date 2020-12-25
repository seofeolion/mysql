#!/usr/bin/python3
#
# Copyright (c) 2019-2020 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#

from sys import argv
from subprocess import check_call
from os import chdir, path

REPO_BASE = path.abspath(path.join(path.dirname(__file__), '..'))

INSTALL_PREFIX = '/tmp/boost_mysql'

BASE_CONFIG = {
    'CMAKE_PREFIX_PATH': '/opt/boost-latest',
    'CMAKE_INSTALL_PREFIX': INSTALL_PREFIX
}

BASE_CONFIG_STANDALONE = {
    'CMAKE_PREFIX_PATH': '/opt/boost-latest;/opt/asio',
    'CMAKE_INSTALL_PREFIX': INSTALL_PREFIX,
    'BOOST_MYSQL_STANDALONE': 'ON'
}

CLANG_CONFIG = {
    'CMAKE_C_COMPILER': 'clang',
    'CMAKE_CXX_COMPILER': 'clang++'
}

ALL_CONFIGS = {
    'gcc-7': {
        **BASE_CONFIG,
        'CMAKE_C_COMPILER': 'gcc-7',
        'CMAKE_CXX_COMPILER': 'g++-7',
        'CMAKE_BUILD_TYPE': 'Debug'
    },
    'clang-debug': {
        **BASE_CONFIG,
        **CLANG_CONFIG,
        'CMAKE_BUILD_TYPE': 'Debug'
    },
    'clang-release': {
        **BASE_CONFIG,
        **CLANG_CONFIG,
        'CMAKE_BUILD_TYPE': 'Release'
    },
    'clang10-debug': {
        **BASE_CONFIG,
        'CMAKE_C_COMPILER': 'clang-10',
        'CMAKE_CXX_COMPILER': 'clang++-10',
        'CMAKE_BUILD_TYPE': 'Debug'
    },
    'install': {
        **BASE_CONFIG,
        'BUILD_TESTING': 'OFF'
    },
    'install-standalone': {
        **BASE_CONFIG_STANDALONE,
        'BUILD_TESTING': 'OFF'
    }
}

def usage():
    print('{} <config>'.format(argv[0]))
    print('Available configs:')
    for name in ALL_CONFIGS.keys():
        print('    ' + name)
    exit(1)
    
def cmd(args):
    print(' + ' + ' '.join(args))
    check_call(args)

def main():
    if len(argv) != 2:
        usage()
    cfg_name = argv[1]
    cfg = ALL_CONFIGS.get(cfg_name)
    if cfg is None:
        usage()

    build_dir = 'build-{}'.format(cfg_name)
    cmake_args = ['-D{}={}'.format(key, value) for key, value in cfg.items()]
    chdir(REPO_BASE)
    cmd(['rm', '-rf', build_dir])
    cmd(['mkdir', build_dir])
    chdir(build_dir)
    cmd(['cmake'] + cmake_args + ['..'])
    cmd(['make', '-j4', 'install'])

if __name__ == '__main__':
    main()
