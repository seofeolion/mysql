//
// Copyright (c) 2019-2023 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_MYSQL_IMPL_CONNECTION_POOL_HPP
#define BOOST_MYSQL_IMPL_CONNECTION_POOL_HPP

#pragma once
#include <boost/mysql/client_errc.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/tcp_ssl.hpp>

#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/compose.hpp>
#include <boost/asio/consign.hpp>
#include <boost/asio/coroutine.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/experimental/cancellation_condition.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/core/ignore_unused.hpp>

#include <chrono>
#include <memory>

#include "boost/sam/condition_variable.hpp"
#include "boost/sam/guarded.hpp"
#include "boost/sam/lock_guard.hpp"

namespace boost {
namespace mysql {
namespace detail {

struct initiate_wait
{
    // TODO: this should be customizable
    static constexpr std::chrono::seconds wait_timeout{10};

    struct cv_op
    {
        boost::sam::condition_variable& cv;

        template <class CompletionToken>
        BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken, void(error_code))
        operator()(CompletionToken&& token)
        {
            return cv.async_wait(std::move(token));
        }
    };

    struct timer_op
    {
        std::shared_ptr<boost::asio::steady_timer> timer;

        template <class CompletionToken>
        BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken, void(error_code))
        operator()(CompletionToken&& token)
        {
            timer->expires_after(wait_timeout);
            return timer->async_wait(std::move(token));
        }
    };

    template <typename CompletionHandler>
    void operator()(CompletionHandler&& completion_handler, boost::sam::condition_variable& cv) const
    {
        struct intermediate_handler
        {
            boost::sam::condition_variable& cv_;
            std::shared_ptr<boost::asio::steady_timer> timer_;
            typename std::decay<CompletionHandler>::type handler_;

            // The function call operator matches the completion signature of the
            // async_write operation.
            void operator()(
                std::array<std::size_t, 2> completion_order,
                boost::system::error_code ec1,
                boost::system::error_code ec2
            )
            {
                // Deallocate before calling the handler
                timer_.reset();

                switch (completion_order[0])
                {
                case 0: handler_(ec1); break;
                case 1: handler_(boost::asio::error::operation_aborted); break;
                }

                boost::ignore_unused(ec2);
            }

            // Preserve executor and allocator
            using executor_type = boost::asio::associated_executor_t<
                typename std::decay<CompletionHandler>::type,
                boost::sam::condition_variable::executor_type>;

            executor_type get_executor() const noexcept
            {
                return boost::asio::get_associated_executor(handler_, cv_.get_executor());
            }

            using allocator_type = boost::asio::
                associated_allocator_t<typename std::decay<CompletionHandler>::type, std::allocator<void>>;

            allocator_type get_allocator() const noexcept
            {
                return boost::asio::get_associated_allocator(handler_, std::allocator<void>{});
            }
        };

        auto timer = std::allocate_shared<boost::asio::steady_timer>(
            boost::asio::get_associated_allocator(completion_handler),
            cv.get_executor()
        );
        boost::asio::experimental::make_parallel_group(cv_op{cv}, timer_op{timer})
            .async_wait(
                boost::asio::experimental::wait_for_one(),
                intermediate_handler{
                    cv,
                    std::move(timer),
                    std::forward<CompletionHandler>(completion_handler),
                }
            );
    }
};

template <BOOST_ASIO_COMPLETION_TOKEN_FOR(void(::boost::mysql::error_code)) CompletionToken>
BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken, void(error_code))
async_wait_for(boost::sam::condition_variable& cv, CompletionToken&& token)
{
    return boost::asio::async_initiate<CompletionToken, void(error_code)>(
        initiate_wait{},
        token,
        std::ref(cv)
    );
}

}  // namespace detail
}  // namespace mysql
}  // namespace boost

boost::mysql::pooled_connection_impl::pooled_connection_impl(connection_pool& p)
    : pool_(&p),
      conn_(p.get_executor(), p.ssl_ctx_),
      resolver_(p.get_executor()),
      timer_(p.get_executor()),
      cleanup_ptr_(this)
{
}

void boost::mysql::pooled_connection_impl::cleanup() noexcept
{
    try
    {
        pool_->return_connection(*this);
    }
    catch (...)
    {
    }
}

struct boost::mysql::pooled_connection_impl::setup_op : boost::asio::coroutine
{
    struct deleter
    {
        void operator()(pooled_connection_impl* conn) const noexcept { conn->locked_ = false; }
    };
    using guard = std::unique_ptr<pooled_connection_impl, deleter>;

    // TODO: this strategy should be customizable
    static constexpr std::size_t max_num_tries = 2;
    static constexpr std::chrono::milliseconds between_tries{1000};

    pooled_connection_impl& conn_;
    diagnostics& diag_;
    std::size_t num_tries_{0};
    guard g_;

    setup_op(pooled_connection_impl& conn, diagnostics& diag) : conn_(conn), diag_(diag), g_(&conn) {}

    template <class Self>
    void complete(Self& self, error_code ec)
    {
        g_.reset();
        self.complete(ec);
    }

    template <class Self>
    void complete_ok(Self& self)
    {
        conn_.state_ = pooled_connection_impl::state_t::in_use;
        diag_.clear();
        complete(self, error_code());
    }

    template <class Self>
    void operator()(
        Self& self,
        error_code err = {},
        boost::asio::ip::tcp::resolver::results_type endpoints = {}
    )
    {
        using st_t = pooled_connection_impl::state_t;

        BOOST_ASIO_CORO_REENTER(*this)
        {
            assert(conn_.state_ != st_t::in_use);
            assert(conn_.locked_);

            for (;;)
            {
                if (conn_.state_ == st_t::not_connected)
                {
                    // Resolve endpoints
                    BOOST_ASIO_CORO_YIELD conn_.resolver_.async_resolve(
                        conn_.pool_->how_to_connect_.hostname,
                        conn_.pool_->how_to_connect_.port,
                        std::move(self)
                    );
                    if (err)
                    {
                        if (++num_tries_ >= max_num_tries)
                        {
                            complete(self, err);
                            BOOST_ASIO_CORO_YIELD break;
                        }
                        conn_.timer_.expires_after(between_tries);
                        BOOST_ASIO_CORO_YIELD conn_.timer_.async_wait(std::move(self));
                        if (err)
                        {
                            complete(self, err);
                            BOOST_ASIO_CORO_YIELD break;
                        }
                        continue;
                    }

                    // connect
                    BOOST_ASIO_CORO_YIELD conn_.conn_.async_connect(
                        *endpoints.begin(),
                        conn_.pool_->how_to_connect_.hparams(),
                        diag_,
                        std::move(self)
                    );
                    if (err)
                    {
                        if (++num_tries_ >= max_num_tries)
                        {
                            complete(self, err);
                            BOOST_ASIO_CORO_YIELD break;
                        }
                        conn_.conn_ = tcp_ssl_connection(
                            conn_.resolver_.get_executor(),
                            conn_.pool_->ssl_ctx_
                        );
                        conn_.timer_.expires_after(between_tries);
                        BOOST_ASIO_CORO_YIELD conn_.timer_.async_wait(std::move(self));
                        if (err)
                        {
                            complete(self, err);
                            BOOST_ASIO_CORO_YIELD break;
                        }
                        continue;
                    }

                    complete_ok(self);
                    BOOST_ASIO_CORO_YIELD break;
                }

                if (conn_.state_ == st_t::pending_reset)
                {
                    // TODO: reset connection not implemented yet
                    complete_ok(self);
                    BOOST_ASIO_CORO_YIELD break;
                }

                if (conn_.state_ == st_t::iddle)
                {
                    BOOST_ASIO_CORO_YIELD conn_.conn_.async_ping(std::move(self));
                    if (err)
                    {
                        // Close the connection as gracefully as we can. Ignoring any errors on purpose
                        BOOST_ASIO_CORO_YIELD conn_.conn_.async_close(std::move(self));

                        // Recreate the connection, since SSL streams can't be reconnected.
                        // TODO: we could provide a method to reuse the connection's internal buffers while
                        // recreating the stream
                        conn_.conn_ = tcp_ssl_connection(
                            conn_.resolver_.get_executor(),
                            conn_.pool_->ssl_ctx_
                        );

                        // Mark it as initial and retry
                        conn_.state_ = st_t::not_connected;
                        if (++num_tries_ >= max_num_tries)
                        {
                            complete(self, err);
                            BOOST_ASIO_CORO_YIELD break;
                        }
                        conn_.timer_.expires_after(between_tries);
                        BOOST_ASIO_CORO_YIELD conn_.timer_.async_wait(std::move(self));
                        if (err)
                        {
                            complete(self, err);
                            BOOST_ASIO_CORO_YIELD break;
                        }
                        continue;
                    }
                    complete_ok(self);
                    BOOST_ASIO_CORO_YIELD break;
                }
            }
        }
    }
};

template <BOOST_ASIO_COMPLETION_TOKEN_FOR(void(::boost::mysql::error_code)) CompletionToken>
BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken, void(error_code))
boost::mysql::pooled_connection_impl::async_setup(diagnostics& diag, CompletionToken&& token)
{
    return boost::asio::async_compose<CompletionToken, void(error_code)>(
        setup_op(*this, diag),
        token,
        resolver_.get_executor()
    );
}

struct boost::mysql::connection_pool::get_connection_op : boost::asio::coroutine
{
    connection_pool& pool_;
    diagnostics& diag_;
    pooled_connection_impl* conn_{nullptr};

    get_connection_op(connection_pool& pool, diagnostics& diag) noexcept : pool_(pool), diag_(diag) {}

    template <class Self>
    void operator()(Self& self, error_code err = {}, boost::sam::lock_guard lockg = {})
    {
        BOOST_ASIO_CORO_REENTER(*this)
        {
            while (true)
            {
                // Lock
                BOOST_ASIO_CORO_YIELD boost::sam::async_lock(pool_.mtx_, std::move(self));
                if (err)
                {
                    self.complete(err, nullptr);
                    BOOST_ASIO_CORO_YIELD break;
                }

                // Find a connection we can return to the user
                conn_ = pool_.find_connection();

                if (conn_)
                {
                    // Mark the connection as locked
                    conn_->locked_ = true;
                    lockg = boost::sam::lock_guard();

                    // Setup code
                    BOOST_ASIO_CORO_YIELD conn_->async_setup(diag_, std::move(self));
                    if (err)
                    {
                        self.complete(err, nullptr);
                        BOOST_ASIO_CORO_YIELD break;
                    }

                    // Done
                    self.complete(error_code(), pooled_connection(conn_));
                    BOOST_ASIO_CORO_YIELD break;
                }

                // Pool is full and everything is in use - wait
                BOOST_ASIO_CORO_YIELD detail::async_wait_for(pool_.cv_, std::move(self));
            }
        }
    }
};

template <BOOST_ASIO_COMPLETION_TOKEN_FOR(void(::boost::mysql::error_code, ::boost::mysql::pooled_connection))
              CompletionToken>
BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken, void(error_code, pooled_connection))
boost::mysql::connection_pool::async_get_connection(diagnostics& diag, CompletionToken&& token)
{
    return boost::asio::async_compose<CompletionToken, void(error_code, pooled_connection)>(
        get_connection_op(*this, diag),
        token,
        cv_
    );
}

#endif
