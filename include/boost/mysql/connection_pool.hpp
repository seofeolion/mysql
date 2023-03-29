//
// Copyright (c) 2019-2023 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_MYSQL_CONNECTION_POOL_HPP
#define BOOST_MYSQL_CONNECTION_POOL_HPP

#include <boost/mysql/connection.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/handshake_params.hpp>
#include <boost/mysql/string_view.hpp>
#include <boost/mysql/tcp_ssl.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/sam/mutex.hpp>

#include <algorithm>
#include <atomic>
#include <list>
#include <memory>

#include "boost/sam/condition_variable.hpp"
#include "boost/sam/guarded.hpp"

namespace boost {
namespace mysql {

// owning handshake params + hostname and port
struct connection_params
{
    std::string hostname;
    std::string port;
    std::string username;
    std::string password;
    std::string database;
    // TODO: ssl mode, collation, etc

    connection_params(
        const handshake_params& hparams,
        std::string hostname,
        std::string port = default_port_string
    )
        : hostname(std::move(hostname)),
          port(std::move(port)),
          username(hparams.username()),
          password(hparams.password()),
          database(hparams.database())
    {
    }

    handshake_params hparams() const noexcept { return handshake_params(username, password, database); }
};

class connection_pool;

class pooled_connection
{
public:
    // TODO: mysql is strictly request-reply, so I think this counts as an implicit
    // strand. Check this assumption.
    tcp_ssl_connection& get() noexcept { return conn_; }
    const tcp_ssl_connection& get() const noexcept { return conn_; }

private:
    pooled_connection(connection_pool& pool);

    template <class CompletionToken>
    auto async_setup(diagnostics&, CompletionToken&& tok);

    enum class state_t
    {
        not_connected,
        iddle,
        in_use,
        pending_reset
    };

    state_t state() const noexcept { return state_; }
    void set_state(state_t st) noexcept { state_ = st; }

    void cleanup() noexcept;

    struct deleter
    {
        void operator()(pooled_connection* pc) noexcept { pc->cleanup(); }
    };

    connection_pool* pool_;
    std::atomic_bool locked_{false};
    state_t state_{state_t::not_connected};
    tcp_ssl_connection conn_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::steady_timer timer_;
    std::unique_ptr<pooled_connection, deleter> cleanup_ptr_;

    struct setup_op;
    friend class connection_pool;
};

class connection_pool
{
public:
    connection_pool(
        connection_params how_to_connect,
        boost::asio::any_io_executor exec,
        size_t initial_size,
        size_t max_size
    )
        : ssl_ctx_(boost::asio::ssl::context::tls_client),
          how_to_connect_(std::move(how_to_connect)),
          max_size_(max_size),
          timer_(exec),
          mtx_(exec),
          cv_(exec)
    {
        for (std::size_t i = 0; i < initial_size; ++i)
        {
            conns_.emplace_back(how_to_connect, ssl_ctx_, exec);
        }
    }

    boost::asio::any_io_executor get_executor() const { return mtx_.get_executor(); }

    template <class CompletionToken>
    auto async_get_connection(diagnostics& diag, CompletionToken&& token);

    void return_connection(pooled_connection& conn, bool should_reset = true)
    {
        using st_t = pooled_connection::state_t;
        bool prev = conn.locked_.exchange(true);
        assert(!prev);
        boost::ignore_unused(prev);
        conn.set_state(should_reset ? st_t::pending_reset : st_t::in_use);
        conn.locked_ = false;
        cv_.notify_one();
    }

private:
    pooled_connection* find_connection()
    {
        // Prefer iddle connections
        auto it = std::find_if(conns_.begin(), conns_.end(), [](const pooled_connection& conn) {
            return !conn.locked_ && conn.state() == pooled_connection::state_t::iddle;
        });

        // Otherwise, prefer connections pending reset
        if (it == conns_.end())
        {
            it = std::find_if(conns_.begin(), conns_.end(), [](const pooled_connection& conn) {
                return !conn.locked_ && conn.state() == pooled_connection::state_t::pending_reset;
            });
        }

        // Otherwise, get a not connected connection, which is the most expensive to setup
        if (it == conns_.end())
        {
            it = std::find_if(conns_.begin(), conns_.end(), [](const pooled_connection& conn) {
                return !conn.locked_ && conn.state() == pooled_connection::state_t::not_connected;
            });
        }

        // No available connection, we need to create one, if limits allow us
        if (it == conns_.end() && conns_.size() < max_size_)
        {
            conns_.emplace_back(how_to_connect_, ssl_ctx_, cv_.get_executor());
            it = std::prev(conns_.end());
        }

        return it == conns_.end() ? nullptr : &*it;
    }

    boost::asio::ssl::context ssl_ctx_;
    connection_params how_to_connect_;
    std::size_t max_size_;
    boost::asio::steady_timer timer_;
    boost::sam::mutex mtx_;
    boost::sam::condition_variable cv_;
    std::list<pooled_connection> conns_;

    friend class pooled_connection;
    struct get_connection_op;
    struct return_connection_op;
};

}  // namespace mysql
}  // namespace boost

#include <boost/mysql/impl/connection_pool.hpp>

#endif
