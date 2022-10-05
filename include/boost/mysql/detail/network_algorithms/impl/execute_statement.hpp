//
// Copyright (c) 2019-2022 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_MYSQL_DETAIL_NETWORK_ALGORITHMS_IMPL_EXECUTE_STATEMENT_HPP
#define BOOST_MYSQL_DETAIL_NETWORK_ALGORITHMS_IMPL_EXECUTE_STATEMENT_HPP

#pragma once

#include <boost/mysql/statement_base.hpp>
#include <boost/mysql/resultset_base.hpp>
#include <boost/mysql/execute_params.hpp>
#include <boost/mysql/detail/network_algorithms/execute_statement.hpp>
#include <boost/mysql/detail/protocol/prepared_statement_messages.hpp>
#include <boost/mysql/detail/protocol/resultset_encoding.hpp>
#include <boost/mysql/detail/network_algorithms/execute_generic.hpp>

namespace boost {
namespace mysql {
namespace detail {

template <class FieldViewFwdIterator>
com_stmt_execute_packet<FieldViewFwdIterator> make_stmt_execute_packet(
    const execute_params<FieldViewFwdIterator>& params,
    const statement_base& stmt
)
{
    return com_stmt_execute_packet<FieldViewFwdIterator> {
        stmt.id(),
        std::uint8_t(0),  // flags
        std::uint32_t(1), // iteration count
        std::uint8_t(1),  // new params flag: set
        params.first(),
        params.last()
    };
}


} // detail
} // mysql
} // boost

template <class Stream, class FieldViewFwdIterator>
void boost::mysql::detail::execute_statement(
    channel<Stream>& chan,
    const statement_base& stmt,
    const execute_params<FieldViewFwdIterator>& params,
    resultset_base& output,
    error_code& err,
    error_info& info
)
{
    execute_generic(
        resultset_encoding::binary,
        chan,
        make_stmt_execute_packet(params, stmt),
        output,
        err,
        info
    );
}

template <class Stream, class FieldViewFwdIterator, class CompletionToken>
BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(
    CompletionToken,
    void(boost::mysql::error_code)
)
boost::mysql::detail::async_execute_statement(
    channel<Stream>& chan,
    const statement_base& stmt,
    const execute_params<FieldViewFwdIterator>& params,
    resultset_base& output,
    error_info& info,
    CompletionToken&& token
)
{
    return async_execute_generic(
        resultset_encoding::binary,
        chan,
        make_stmt_execute_packet(params, stmt),
        output,
        info,
        std::forward<CompletionToken>(token)
    );
}



#endif /* INCLUDE_BOOST_MYSQL_DETAIL_NETWORK_ALGORITHMS_IMPL_EXECUTE_STATEMENT_HPP_ */
