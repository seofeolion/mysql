//
// Copyright (c) 2019-2020 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "boost/mysql/detail/config.hpp"

#ifdef BOOST_MYSQL_STANDALONE
#include <string_view>
#else
#include <boost/utility/string_view.hpp>
#endif

BOOST_MYSQL_NAMESPACE_BEGIN

#ifdef BOOST_MYSQL_STANDALONE
using string_view = std::string_view;
#else
using string_view = boost::string_view;

BOOST_MYSQL_NAMESPACE_END