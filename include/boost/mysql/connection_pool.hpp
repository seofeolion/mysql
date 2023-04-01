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
#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/sam/mutex.hpp>

#include <algorithm>
#include <atomic>
#include <deque>
#include <list>
#include <memory>
#include <mutex>

#include "boost/sam/condition_variable.hpp"
#include "boost/sam/guarded.hpp"
#include "boost/sam/lock_guard.hpp"

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

class pooled_connection_impl
{
public:
    // TODO: mysql is strictly request-reply, so I think this counts as an implicit
    // strand. Check this assumption.
    tcp_ssl_connection& get() noexcept { return conn_; }
    const tcp_ssl_connection& get() const noexcept { return conn_; }

    inline pooled_connection_impl(connection_pool& pool);
    pooled_connection_impl(const pooled_connection_impl&) = delete;
    pooled_connection_impl(pooled_connection_impl&&) = delete;
    pooled_connection_impl& operator=(const pooled_connection_impl&) = delete;
    pooled_connection_impl& operator=(pooled_connection_impl&&) = delete;
    inline ~pooled_connection_impl();

private:
    enum class state_t
    {
        not_connected,
        iddle,
        in_use,
        pending_reset
    };

    template <BOOST_ASIO_COMPLETION_TOKEN_FOR(void(::boost::mysql::error_code)) CompletionToken>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken, void(error_code))
    async_setup(diagnostics&, CompletionToken&& tok);

    connection_pool* pool_;
    state_t state_{state_t::not_connected};
    tcp_ssl_connection conn_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::steady_timer timer_;

    struct setup_op;
    friend class connection_pool;
    friend class pooled_connection;
};

class pooled_connection;

class connection_pool
{
public:
    connection_pool(
        connection_params how_to_connect,
        boost::asio::any_io_executor exec,
        size_t,
        size_t max_size
    )
        : ssl_ctx_(boost::asio::ssl::context::tls_client),
          how_to_connect_(std::move(how_to_connect)),
          max_size_(max_size),
          cv_(exec)
    {
        // TODO: honor initial_size
        // for (std::size_t i = 0; i < initial_size; ++i)
        // {
        //     add_connection();
        // }
    }

    boost::asio::any_io_executor get_executor() const { return cv_.get_executor(); }

    template <
        BOOST_ASIO_COMPLETION_TOKEN_FOR(void(::boost::mysql::error_code, ::boost::mysql::pooled_connection))
            CompletionToken>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken, void(error_code, pooled_connection))
    async_get_connection(diagnostics& diag, CompletionToken&& token);

private:
    std::shared_ptr<pooled_connection_impl> find_connection()
    {
        std::lock_guard<std::mutex> guard(mtx_);

        // If there are available connections, take one
        if (!iddle_conns_.empty())
        {
            auto conn = iddle_conns_.front();
            iddle_conns_.pop_front();
            return conn;
        }

        // If the limit allows us, create a new connection
        if (num_active_conns_ < max_size_)
        {
            auto res = std::make_shared<pooled_connection_impl>(*this);
            ++num_active_conns_;
            return res;
        }

        // No connection available
        return nullptr;
    }

    inline void return_connection(std::shared_ptr<pooled_connection_impl> conn)
    {
        std::lock_guard<std::mutex> guard(mtx_);
        conn->state_ = pooled_connection_impl::state_t::pending_reset;
        iddle_conns_.push_back(std::move(conn));
        cv_.notify_one();
    }

    void on_connection_destroyed()
    {
        std::lock_guard<std::mutex> guard(mtx_);
        --num_active_conns_;
        cv_.notify_one();
    }

    // Params
    boost::asio::ssl::context ssl_ctx_;
    connection_params how_to_connect_;
    std::size_t max_size_;

    // State
    std::mutex mtx_;
    std::deque<std::shared_ptr<pooled_connection_impl>> iddle_conns_;
    boost::sam::condition_variable cv_;
    std::size_t num_active_conns_{0};

    friend class pooled_connection_impl;
    friend class pooled_connection;
    struct get_connection_op;
};

class pooled_connection
{
    std::shared_ptr<pooled_connection_impl> impl_{};

public:
    pooled_connection() = default;
    pooled_connection(std::shared_ptr<pooled_connection_impl> impl) noexcept : impl_(std::move(impl)) {}
    pooled_connection(const pooled_connection&) = delete;
    pooled_connection(pooled_connection&& other) noexcept : impl_(std::move(other.impl_))
    {
        other.impl_ = nullptr;
    }
    pooled_connection& operator=(const pooled_connection&) = delete;
    pooled_connection& operator=(pooled_connection&& other) noexcept
    {
        std::swap(impl_, other.impl_);
        return *this;
    }
    ~pooled_connection() noexcept
    {
        if (impl_)
        {
            auto* pool = impl_->pool_;
            pool->return_connection(std::move(impl_));
        }
    }
    tcp_ssl_connection* operator->() noexcept { return &get(); }
    const tcp_ssl_connection* operator->() const noexcept { return &get(); }
    tcp_ssl_connection& get() noexcept { return impl_->conn_; }
    const tcp_ssl_connection& get() const noexcept { return impl_->conn_; }
};

}  // namespace mysql
}  // namespace boost

#include <boost/mysql/impl/connection_pool.hpp>

#endif
