//
// Copyright (c) 2019-2020 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_MYSQL_DETAIL_CONFIG_HPP
#define BOOST_MYSQL_DETAIL_CONFIG_HPP

// Conditional includes
#ifndef BOOST_MYSQL_STANDALONE
# include <boost/config.hpp>
#endif

// Namespace
#ifdef BOOST_MYSQL_STANDALONE
# define BOOST_MYSQL_NAMESPACE_BEGIN \
    namespace boost { \
    namespace mysql { \
    inline namespace standalone {
# define BOOST_MYSQL_NAMESPACE_END } } }
#else
# define BOOST_MYSQL_NAMESPACE_BEGIN \
    namespace boost { \
    namespace mysql {
# define BOOST_MYSQL_NAMESPACE_END } }
#endif

// Constexpr specifiers
#ifdef BOOST_MYSQL_STANDALONE
#define BOOST_MYSQL_CXX14_CONSTEXPR constexpr
#else
#define BOOST_MYSQL_CXX14_CONSTEXPR BOOST_CXX14_CONSTEXPR
#endif

// Has std optional
#ifdef BOOST_MYSQL_STANDALONE
    #define BOOST_MYSQL_HAS_STD_OPTIONAL
#else
    #ifndef BOOST_NO_CXX17_HDR_OPTIONAL
    #define BOOST_MYSQL_HAS_STD_OPTIONAL
    #endif // BOOST_NO_CXX17_HDR_OPTIONAL
#endif // BOOST_MYSQL_STANDALONE

// Asio macros
#ifdef BOOST_MYSQL_STANDALONE
    #define BOOST_MYSQL_COMPLETION_TOKEN_FOR(...)          ASIO_COMPLETION_TOKEN_FOR(__VA_ARGS__)
    #define BOOST_MYSQL_DEFAULT_COMPLETION_TOKEN(...)      ASIO_DEFAULT_COMPLETION_TOKEN(__VA_ARGS__)
    #define BOOST_MYSQL_DEFAULT_COMPLETION_TOKEN_TYPE(...) ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(__VA_ARGS__)
    #define BOOST_MYSQL_INITFN_AUTO_RESULT_TYPE(...)       ASIO_INITFN_AUTO_RESULT_TYPE(__VA_ARGS__)
    #define BOOST_MYSQL_CORO_REENTER                       ASIO_CORO_REENTER
    #define BOOST_MYSQL_CORO_YIELD                         ASIO_CORO_YIELD
#else
    #define BOOST_MYSQL_COMPLETION_TOKEN_FOR(...)          BOOST_ASIO_COMPLETION_TOKEN_FOR(__VA_ARGS__)
    #define BOOST_MYSQL_DEFAULT_COMPLETION_TOKEN(...)      BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(__VA_ARGS__)
    #define BOOST_MYSQL_DEFAULT_COMPLETION_TOKEN_TYPE(...) BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(__VA_ARGS__)
    #define BOOST_MYSQL_INITFN_AUTO_RESULT_TYPE(...)       BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(__VA_ARGS__)
    #define BOOST_MYSQL_CORO_REENTER                       BOOST_ASIO_CORO_REENTER
    #define BOOST_MYSQL_CORO_YIELD                         BOOST_ASIO_CORO_YIELD
#endif // BOOST_MYSQL_STANDALONE

#endif
