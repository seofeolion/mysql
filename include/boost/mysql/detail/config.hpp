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

#endif
