//
// Copyright (c) 2019-2023 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_MYSQL_DETAIL_AUTH_AUTH_CALCULATOR_HPP
#define BOOST_MYSQL_DETAIL_AUTH_AUTH_CALCULATOR_HPP

#include <boost/mysql/error_code.hpp>
#include <boost/mysql/string_view.hpp>

#include <boost/config.hpp>

#include <vector>

namespace boost {
namespace mysql {
namespace detail {

struct auth_response
{
    std::vector<std::uint8_t> data;
    string_view plugin_name;

    string_view response() const noexcept
    {
        return string_view(reinterpret_cast<const char*>(data.data()), data.size());
    }
};

BOOST_ATTRIBUTE_NODISCARD
error_code compute_auth_response(
    string_view plugin_name,
    string_view password,
    string_view challenge,
    bool use_ssl,
    auth_response& output
);

}  // namespace detail
}  // namespace mysql
}  // namespace boost

#endif /* INCLUDE_BOOST_MYSQL_DETAIL_AUTH_AUTH_CALCULATOR_HPP_ */
