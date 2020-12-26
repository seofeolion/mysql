//
// Copyright (c) 2019-2020 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "boost/mysql/detail/config.hpp"

#ifdef BOOST_MYSQL_STANDALONE
#include <asio/ip/tcp.hpp>
#include <asio/local/stream_protocol.hpp>
#else
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#endif // BOOST_MYSQL_STANDALONE

BOOST_MYSQL_NAMESPACE_BEGIN

// TODO: document
#ifdef BOOST_MYSQL_STANDALONE
    using tcp_socket = asio::ip::tcp::socket;
    #ifdef ASIO_HAS_LOCAL_SOCKETS
        #define BOOST_MYSQL_HAS_LOCAL_SOCKETS
        using unix_socket = asio::local::stream_protocol::socket;
    #endif // ASIO_HAS_LOCAL_SOCKETS
#else
    using tcp_socket = boost::asio::ip::tcp::socket;
    #ifdef BOOST_ASIO_HAS_LOCAL_SOCKETS
        #define BOOST_MYSQL_HAS_LOCAL_SOCKETS
        using unix_socket = boost::asio::local::stream_protocol::socket;
    #endif // BOOST_MYSQL_HAS_LOCAL_SOCKETS
#endif // BOOST_MYSQL_STANDALONE

BOOST_MYSQL_NAMESPACE_END
