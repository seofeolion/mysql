//
// Copyright (c) 2019-2020 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "boost/mysql/detail/config.hpp"

#ifdef BOOST_MYSQL_STANDALONE
#include <optional>
#else
#include <boost/optional.hpp>
#endif // BOOST_MYSQL_STANDALONE

BOOST_MYSQL_NAMESPACE_BEGIN
namespace detail {

#ifdef BOOST_MYSQL_STANDALONE
template <class T>
using optional = std::optional<T>;
#else
template <class T>
using optional = boost::optional<T>;
#endif // BOOST_MYSQL_STANDALONE

} // detail
BOOST_MYSQL_NAMESPACE_END