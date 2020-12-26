//
// Copyright (c) 2019-2020 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "boost/mysql/detail/config.hpp"

// TODO: document

#ifdef BOOST_MYSQL_STANDALONE
#include <variant>
#else
#include <boost/variant2/variant.hpp>
#endif // BOOST_MYSQL_STANDALONE

BOOST_MYSQL_NAMESPACE_BEGIN

#ifdef BOOST_MYSQL_STANDALONE
template <clas... Args>
using variant = std::variant<Args...>;
#else
template <class... Args>
using variant = boost::variant2::variant<Args...>;
#endif // BOOST_MYSQL_STANDALONE

namespace detail {

// Simplify calling std::holds_altrnative / boost::variant2::holds_alternative
#ifdef BOOST_MYSQL_STANDALONE
namespace variant_ns = std;
#else
namespace variant_ns = boost::variant2;
#endif // BOOST_MYSQL_STANDALONE

} // detail 

BOOST_MYSQL_NAMESPACE_END