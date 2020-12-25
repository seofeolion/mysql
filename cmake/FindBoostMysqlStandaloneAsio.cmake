#
# Copyright (c) 2019-2020 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#

if(TARGET BoostMysqlStandaloneAsio::Asio)
    set(BoostMysqlStandaloneAsio_FOUND TRUE)
else()
    find_path(BoostMysqlStandaloneAsio_ROOT include/asio.hpp)
    if(NOT BoostMysqlStandaloneAsio_ROOT AND BoostMysqlStandaloneAsio_FIND_REQUIRED)
        message(FATAL_ERROR "Standalone Asio could not be found.")
    endif()

    if(NOT BoostMysqlStandaloneAsio_FIND_QUIETLY)
        if(BoostMysqlStandaloneAsio_ROOT)
            message(STATUS "Found Asio standalone: ${BoostMysqlStandaloneAsio_ROOT}.")
        else()
            message(STATUS "Could NOT find Asio standalone.")
        endif()
    endif()

    add_library(BoostMysqlStandaloneAsio::Asio INTERFACE IMPORTED GLOBAL)
    set_target_properties(BoostMysqlStandaloneAsio::Asio PROPERTIES
        INTERFACE_COMPILE_DEFINITIONS "ASIO_STANDALONE"
        INTERFACE_INCLUDE_DIRECTORIES "${BoostMysqlStandaloneAsio_ROOT}/include"
    )
    set(BoostMysqlStandaloneAsio_FOUND TRUE)
endif()