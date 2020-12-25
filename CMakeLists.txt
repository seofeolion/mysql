#
# Copyright (c) 2019-2020 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#

cmake_minimum_required(VERSION 3.13.0) # Determined by requiring Boost 1.70
project(boost_mysql VERSION 0.0.1)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Standalone mode
option(BOOST_MYSQL_STANDALONE OFF "Build Boost.Mysql in standalone mode (requires standalone asio)")

# If we are the top-level project, enable testing
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
    if(BUILD_TESTING)
        set(_TESTING_ENABLED ON)
    endif()
    
    # Valgrind tests and Valgrind-friendly code (e.g. mark SSL buffers as initialized)
    option(BOOST_MYSQL_VALGRIND_TESTS OFF "Whether to run Valgrind tests or not (requires Valgrind)")
    mark_as_advanced(BOOST_MYSQL_VALGRIND_TESTS)
    
    # Build with coverage
    option(BOOST_MYSQL_COVERAGE OFF "Whether to build using coverage")
    mark_as_advanced(BOOST_MYSQL_COVERAGE)
endif()

# Includes
include(GNUInstallDirs)

# Dependencies
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

if (BOOST_MYSQL_STANDALONE)
    find_package(BoostMysqlStandaloneAsio REQUIRED)
else()
    find_package(Boost 1.75.0 REQUIRED COMPONENTS system)
endif()


# Interface library (header-only)
add_library(boost_mysql INTERFACE)
add_library(Boost::mysql ALIAS boost_mysql)

# Dependencies: asio
if (BOOST_MYSQL_STANDALONE)
    target_link_libraries(boost_mysql INTERFACE BoostMysqlStandaloneAsio::Asio)
    target_compile_definitions(boost_mysql INTERFACE "BOOST_MYSQL_STANDALONE")
else()
    target_link_libraries(boost_mysql INTERFACE Boost::system)
endif()

# Dependencies: common
target_link_libraries(
    boost_mysql 
    INTERFACE 
    Threads::Threads 
    OpenSSL::Crypto 
    OpenSSL::SSL
)

# Asio bases C++ feature detection on __cplusplus. Make MSVC
# define it correctly
if (MSVC)
    target_compile_options(boost_mysql INTERFACE /Zc:__cplusplus)
endif()

# Own includes/compile features
target_include_directories(
    boost_mysql
    INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
if (BOOST_MYSQL_STANDALONE)
    target_compile_features(boost_mysql INTERFACE cxx_std_17)
else()
    target_compile_features(boost_mysql INTERFACE cxx_std_11)
endif()

# Installing
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set_target_properties(boost_mysql PROPERTIES EXPORT_NAME mysql)
    install(
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ 
        DESTINATION include
    )
    install(
        TARGETS boost_mysql
        EXPORT boost_mysql_targets
    )
    install(
        EXPORT boost_mysql_targets
        FILE boost_mysql-targets.cmake
        NAMESPACE Boost::
        DESTINATION lib/cmake/boost_mysql
    )
    
    include(CMakePackageConfigHelpers)

    configure_package_config_file(
        cmake/config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/boost_mysql-config.cmake
        INSTALL_DESTINATION lib/cmake/boost_mysql
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/boost_mysql-config-version.cmake
        VERSION ${PROJECT_VERSION} 
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/boost_mysql-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/boost_mysql-config-version.cmake
        DESTINATION lib/cmake/boost_mysql
    )

    if (BOOST_MYSQL_STANDALONE)
        install(
            FILES cmake/FindBoostMysqlStandaloneAsio.cmake
            DESTINATION lib/cmake/boost_mysql
        )
    endif()
endif()

# Examples and tests
if(_TESTING_ENABLED)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_utils.cmake)
    add_subdirectory(example)
    add_subdirectory(test)
endif()
