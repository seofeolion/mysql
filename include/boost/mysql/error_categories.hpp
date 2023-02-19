//
// Copyright (c) 2019-2023 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_MYSQL_ERROR_CATEGORIES_HPP
#define BOOST_MYSQL_ERROR_CATEGORIES_HPP

#include <boost/system/error_category.hpp>

namespace boost {
namespace mysql {

/**
 * \brief Returns the error_category associated to \ref client_errc.
 */
inline const boost::system::error_category& get_client_category() noexcept;

/**
 * \brief Returns the error_category associated to \ref common_server_errc.
 */
inline const boost::system::error_category& get_common_server_category() noexcept;

/**
 * \brief Returns the error_category associated to errors specific to MySQL.
 */
inline const boost::system::error_category& get_mysql_server_category() noexcept;

/**
 * \brief Returns the error_category associated to errors specific to MariaDB.
 */
inline const boost::system::error_category& get_mariadb_server_category() noexcept;

}  // namespace mysql
}  // namespace boost

#include <boost/mysql/impl/error_categories.hpp>

#endif
