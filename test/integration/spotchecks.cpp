//
// Copyright (c) 2019-2023 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/mysql/connection.hpp>
#include <boost/mysql/execution_state.hpp>
#include <boost/mysql/field_view.hpp>
#include <boost/mysql/resultset.hpp>
#include <boost/mysql/row_view.hpp>
#include <boost/mysql/rows_view.hpp>

#include "integration_test_common.hpp"
#include "metadata_validator.hpp"
#include "test_common.hpp"

using namespace boost::mysql::test;
using boost::mysql::execution_state;
using boost::mysql::field_view;
using boost::mysql::resultset;
using boost::mysql::row_view;
using boost::mysql::server_errc;

namespace {

BOOST_AUTO_TEST_SUITE(test_spotchecks)

auto err_net_samples = create_network_samples({
    "tcp_sync_errc",
    "tcp_sync_exc",
    "tcp_async_callback",
    "tcp_async_callback_noerrinfo",
});

// Handshake
BOOST_MYSQL_NETWORK_TEST(success, network_fixture, all_network_samples())
{
    setup_and_physical_connect(sample.net);
    conn->handshake(params).validate_no_error();
    BOOST_TEST(conn->uses_ssl() == var->supports_ssl());
}

BOOST_MYSQL_NETWORK_TEST(error, network_fixture, err_net_samples)
{
    setup_and_physical_connect(sample.net);
    params.set_database("bad_database");
    conn->handshake(params).validate_error(
        server_errc::dbaccess_denied_error,
        {"database", "bad_database"}
    );
}

// Connect: success is already widely tested throughout integ tests
BOOST_MYSQL_NETWORK_TEST(connect_error, network_fixture, err_net_samples)
{
    setup(sample.net);
    set_credentials("integ_user", "bad_password");
    conn->connect(params).validate_error(
        server_errc::access_denied_error,
        {"access denied", "integ_user"}
    );
    BOOST_TEST(!conn->is_open());
}

// Start query
BOOST_MYSQL_NETWORK_TEST(start_query_success, network_fixture, all_network_samples())
{
    setup_and_connect(sample.net);

    execution_state st;
    conn->start_query("SELECT * FROM empty_table", st).get();
    BOOST_TEST(!st.complete());
    validate_2fields_meta(st.meta(), "empty_table");
}

BOOST_MYSQL_NETWORK_TEST(start_query_error, network_fixture, err_net_samples)
{
    setup_and_connect(sample.net);

    execution_state st;
    conn->start_query("SELECT field_varchar, field_bad FROM one_row_table", st)
        .validate_error(server_errc::bad_field_error, {"unknown column", "field_bad"});
}

// Query
BOOST_MYSQL_NETWORK_TEST(query_success, network_fixture, all_network_samples())
{
    setup_and_connect(sample.net);

    resultset result;
    conn->query("SELECT 'hello', 42", result).get();
    BOOST_TEST(result.rows().size() == 1u);
    BOOST_TEST(result.rows()[0] == makerow("hello", 42));
    BOOST_TEST(result.meta().size() == 2u);
}

BOOST_MYSQL_NETWORK_TEST(query_error, network_fixture, err_net_samples)
{
    setup_and_connect(sample.net);

    resultset result;
    conn->query("SELECT field_varchar, field_bad FROM one_row_table", result)
        .validate_error(server_errc::bad_field_error, {"unknown column", "field_bad"});
}

// Prepare statement
BOOST_MYSQL_NETWORK_TEST(prepare_statement_success, network_fixture, all_network_samples())
{
    setup_and_connect(sample.net);
    conn->prepare_statement("SELECT * FROM empty_table WHERE id IN (?, ?)", *stmt)
        .validate_no_error();
    BOOST_TEST_REQUIRE(stmt->base().valid());
    BOOST_TEST(stmt->base().id() > 0u);
    BOOST_TEST(stmt->base().num_params() == 2u);
}

BOOST_MYSQL_NETWORK_TEST(prepare_statement_error, network_fixture, err_net_samples)
{
    setup_and_connect(sample.net);
    conn->prepare_statement("SELECT * FROM bad_table WHERE id IN (?, ?)", *stmt)
        .validate_error(server_errc::no_such_table, {"table", "doesn't exist", "bad_table"});
}

// Start statement execution (iterator version)
BOOST_MYSQL_NETWORK_TEST(
    start_statement_execution_it_success,
    network_fixture,
    all_network_samples()
)
{
    setup_and_connect(sample.net);

    // Prepare
    conn->prepare_statement("SELECT * FROM empty_table WHERE id IN (?, ?)", *stmt)
        .validate_no_error();

    // Execute
    execution_state st;
    std::forward_list<field_view> params{field_view("item"), field_view(42)};
    stmt->start_execution_it(params.begin(), params.end(), st).validate_no_error();
    validate_2fields_meta(st.meta(), "empty_table");
    BOOST_TEST(!st.complete());
}

BOOST_MYSQL_NETWORK_TEST(start_statement_execution_it_error, network_fixture, err_net_samples)
{
    setup_and_connect(sample.net);
    start_transaction();

    // Prepare
    conn->prepare_statement(
            "INSERT INTO inserts_table (field_varchar, field_date) VALUES (?, ?)",
            *stmt
    )
        .validate_no_error();

    // Execute
    execution_state st;
    std::forward_list<field_view> params{field_view("f0"), field_view("bad_date")};
    stmt->start_execution_it(params.begin(), params.end(), st)
        .validate_error(
            server_errc::truncated_wrong_value,
            {"field_date", "bad_date", "incorrect date value"}
        );
}

// Start statement execution (tuple version)
BOOST_MYSQL_NETWORK_TEST(
    start_statement_execution_tuple_success,
    network_fixture,
    all_network_samples()
)
{
    setup_and_connect(sample.net);

    // Prepare
    conn->prepare_statement("SELECT * FROM empty_table WHERE id IN (?, ?)", *stmt)
        .validate_no_error();

    // Execute
    execution_state st;
    stmt->start_execution_tuple2(field_view(42), field_view(40), st).validate_no_error();
    validate_2fields_meta(st.meta(), "empty_table");
    BOOST_TEST(!st.complete());
}

BOOST_MYSQL_NETWORK_TEST(start_statement_execution_tuple_error, network_fixture, err_net_samples)
{
    setup_and_connect(sample.net);
    start_transaction();

    // Prepare
    conn->prepare_statement(
            "INSERT INTO inserts_table (field_varchar, field_date) VALUES (?, ?)",
            *stmt
    )
        .validate_no_error();

    // Execute
    execution_state st;
    stmt->start_execution_tuple2(field_view("abc"), field_view("bad_date"), st)
        .validate_error(
            server_errc::truncated_wrong_value,
            {"field_date", "bad_date", "incorrect date value"}
        );
}

// Execute statement
BOOST_MYSQL_NETWORK_TEST(execute_statement_success, network_fixture, all_network_samples())
{
    setup_and_connect(sample.net);

    // Prepare
    conn->prepare_statement("SELECT * FROM empty_table WHERE id IN (?, ?)", *stmt)
        .validate_no_error();

    // Execute
    resultset result;
    stmt->execute_tuple2(field_view("item"), field_view(42), result).validate_no_error();
    BOOST_TEST(result.rows().size() == 0u);
}

BOOST_MYSQL_NETWORK_TEST(execute_statement_error, network_fixture, err_net_samples)
{
    setup_and_connect(sample.net);
    start_transaction();

    // Prepare
    conn->prepare_statement(
            "INSERT INTO inserts_table (field_varchar, field_date) VALUES (?, ?)",
            *stmt
    )
        .validate_no_error();

    // Execute
    resultset result;
    stmt->execute_tuple2(field_view("f0"), field_view("bad_date"), result)
        .validate_error(
            server_errc::truncated_wrong_value,
            {"field_date", "bad_date", "incorrect date value"}
        );
}

// Close statement: no server error spotcheck
BOOST_MYSQL_NETWORK_TEST(close_statement_success, network_fixture, all_network_samples())
{
    setup_and_connect(sample.net);

    // Prepare a statement
    conn->prepare_statement("SELECT * FROM empty_table", *stmt).validate_no_error();

    // Close the statement
    stmt->close().validate_no_error();

    // The statement is no longer valid
    BOOST_TEST(!stmt->base().valid());
}

// Read one row: no server error spotcheck
BOOST_MYSQL_NETWORK_TEST(read_one_row_success, network_fixture, all_network_samples())
{
    setup_and_connect(sample.net);

    // Generate an execution state
    execution_state st;
    conn->start_query("SELECT * FROM one_row_table", st);
    BOOST_TEST_REQUIRE(!st.complete());

    // Read the only row
    row_view r = conn->read_one_row(st).get();
    validate_2fields_meta(st.meta(), "one_row_table");
    BOOST_TEST((r == makerow(1, "f0")));
    BOOST_TEST(!st.complete());

    // Read next: end of resultset
    r = conn->read_one_row(st).get();
    BOOST_TEST(r == row_view());
    validate_eof(st);
}

// Read some rows: no server error spotcheck
BOOST_MYSQL_NETWORK_TEST(read_some_rows_success, network_fixture, all_network_samples())
{
    setup_and_connect(sample.net);

    // Generate an execution state
    execution_state st;
    conn->start_query("SELECT * FROM one_row_table", st);
    BOOST_TEST_REQUIRE(!st.complete());

    // Read once. The resultset may or may not be complete, depending
    // on how the buffer reallocated memory
    auto rows = conn->read_some_rows(st).get();
    BOOST_TEST((rows == makerows(2, 1, "f0")));

    // Reading again should complete the resultset
    rows = conn->read_some_rows(st).get();
    BOOST_TEST(rows.empty());
    validate_eof(st);

    // Reading again does nothing
    rows = conn->read_some_rows(st).get();
    BOOST_TEST(rows.empty());
    validate_eof(st);
}

// Quit connection: no server error spotcheck
BOOST_MYSQL_NETWORK_TEST(quit_success, network_fixture, all_network_samples())
{
    setup_and_connect(sample.net);

    // Quit
    conn->quit().validate_no_error();

    // We are no longer able to query
    resultset result;
    conn->query("SELECT 1", result).validate_any_error();
}

// Close connection: no server error spotcheck
BOOST_MYSQL_NETWORK_TEST(close_connection_success, network_fixture, all_network_samples())
{
    setup_and_connect(sample.net);

    // Close
    conn->close().validate_no_error();

    // We are no longer able to query
    boost::mysql::resultset result;
    conn->query("SELECT 1", result).validate_any_error();

    // The stream is closed
    BOOST_TEST(!conn->is_open());

    // Closing again returns OK (and does nothing)
    conn->close().validate_no_error();

    // Stream (socket) still closed
    BOOST_TEST(!conn->is_open());
}

// TODO: move this to a unit test
BOOST_MYSQL_NETWORK_TEST(not_open_connection, network_fixture, err_net_samples)
{
    setup(sample.net);
    conn->close().validate_no_error();
    BOOST_TEST(!conn->is_open());
}

BOOST_AUTO_TEST_SUITE_END()  // test_spotchecks

}  // namespace