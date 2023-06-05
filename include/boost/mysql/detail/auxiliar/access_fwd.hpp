//
// Copyright (c) 2019-2023 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_MYSQL_DETAIL_AUXILIAR_ACCESS_FWD_HPP
#define BOOST_MYSQL_DETAIL_AUXILIAR_ACCESS_FWD_HPP

#include <utility>

namespace boost {
namespace mysql {
namespace detail {

// These structs expose additional functions that are "public" to
// other members of the library, but are not to be used by end users
struct connection_access;
struct diagnostics_access;
struct metadata_access;

// A generic access struct to enable access to the implementation of any class
struct impl_access
{
    template <class T>
    static decltype(std::declval<T>().impl_)& get_impl(T& obj) noexcept
    {
        return obj.impl_;
    }

    template <class T>
    static const decltype(std::declval<T>().impl_)& get_impl(const T& obj) noexcept
    {
        return obj.impl_;
    }

    template <class T, class... Args>
    static T construct(Args&&... args)
    {
        return T(std::forward<Args>(args)...);
    }
};

}  // namespace detail
}  // namespace mysql
}  // namespace boost

#endif
