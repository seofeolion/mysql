//
// Copyright (c) 2019-2023 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/mysql/execution_state.hpp>
#include <boost/mysql/field.hpp>
#include <boost/mysql/field_view.hpp>
#include <boost/mysql/resultset.hpp>
#include <boost/mysql/row.hpp>
#include <boost/mysql/row_view.hpp>
#include <boost/mysql/tcp.hpp>

#include <boost/test/unit_test.hpp>

#include <tuple>

#include "er_connection.hpp"
#include "integration_test_common.hpp"
#include "tcp_network_fixture.hpp"
#include "test_common.hpp"

using namespace boost::mysql::test;
using boost::mysql::execution_state;
using boost::mysql::resultset;
using boost::mysql::row;
using boost::mysql::row_view;
using boost::mysql::tcp_statement;

namespace {

BOOST_AUTO_TEST_SUITE(test_prepared_statements)

BOOST_FIXTURE_TEST_CASE(multiple_executions, tcp_network_fixture)
{
    connect();

    // Prepare a statement
    tcp_statement stmt;
    conn.prepare_statement("SELECT * FROM two_rows_table WHERE id = ? OR field_varchar = ?", stmt);

    // Execute it. Only one row will be returned (because of the id)
    resultset result;
    stmt.execute(std::make_tuple(1, "non_existent"), result);
    validate_2fields_meta(result.meta(), "two_rows_table");
    BOOST_TEST_REQUIRE(result.rows().size() == 1u);
    BOOST_TEST((result.rows()[0] == makerow(1, "f0")));

    // Execute it again, but with different values. This time, two rows are returned
    stmt.execute(std::make_tuple(1, "f1"), result);
    validate_2fields_meta(result.meta(), "two_rows_table");
    BOOST_TEST_REQUIRE(result.rows().size() == 2u);
    BOOST_TEST((result.rows()[0] == makerow(1, "f0")));
    BOOST_TEST((result.rows()[1] == makerow(2, "f1")));

    // Close it
    stmt.close();
}

BOOST_FIXTURE_TEST_CASE(multiple_statements, tcp_network_fixture)
{
    connect();
    start_transaction();

    // Prepare an update and a select
    tcp_statement stmt_update, stmt_select;
    resultset result;
    conn.prepare_statement(
        "UPDATE updates_table SET field_int = ? WHERE field_varchar = ?",
        stmt_update
    );
    conn.prepare_statement(
        "SELECT field_int FROM updates_table WHERE field_varchar = ?",
        stmt_select
    );
    BOOST_TEST(stmt_update.num_params() == 2u);
    BOOST_TEST(stmt_select.num_params() == 1u);
    BOOST_TEST(stmt_update.id() != stmt_select.id());

    // Execute update
    stmt_update.execute(std::make_tuple(210, "f0"), result);
    BOOST_TEST(result.meta().size() == 0u);
    BOOST_TEST(result.affected_rows() == 1u);

    // Execute select
    stmt_select.execute(std::make_tuple("f0"), result);
    BOOST_TEST(result.rows().size() == 1u);
    BOOST_TEST(result.rows()[0] == makerow(210));

    // Execute update again
    stmt_update.execute(std::make_tuple(220, "f0"), result);
    BOOST_TEST(result.meta().size() == 0u);
    BOOST_TEST(result.affected_rows() == 1u);

    // Update no longer needed, close it
    stmt_update.close();
    BOOST_TEST(!stmt_update.valid());

    // Execute select again
    stmt_select.execute(std::make_tuple("f0"), result);
    BOOST_TEST(result.rows().size() == 1u);
    BOOST_TEST(result.rows()[0] == makerow(220));

    // Close select
    stmt_select.close();
}

BOOST_FIXTURE_TEST_CASE(statement_without_params, tcp_network_fixture)
{
    connect();

    // Prepare the statement
    tcp_statement stmt;
    conn.prepare_statement("SELECT * FROM empty_table", stmt);
    BOOST_TEST(stmt.valid());
    BOOST_TEST(stmt.num_params() == 0u);

    // Execute doesn't error
    resultset result;
    stmt.execute(std::make_tuple(), result);
    validate_2fields_meta(result.meta(), "empty_table");
    BOOST_TEST(result.rows().size() == 0u);
}

// Note: multifn query is already covered in spotchecks
BOOST_FIXTURE_TEST_CASE(multifn_read_one, tcp_network_fixture)
{
    connect();

    // Prepare the statement
    tcp_statement stmt;
    conn.prepare_statement("SELECT * FROM two_rows_table", stmt);

    // Execute it
    execution_state st;
    stmt.start_execution(std::make_tuple(), st);
    BOOST_TEST_REQUIRE(!st.complete());
    validate_2fields_meta(st.meta(), "two_rows_table");

    // Read first row
    auto r = conn.read_one_row(st);
    BOOST_TEST((r == makerow(1, "f0")));
    BOOST_TEST(!st.complete());

    // Read next row
    r = conn.read_one_row(st);
    BOOST_TEST((r == makerow(2, "f1")));
    BOOST_TEST(!st.complete());

    // Read next: end of resultset
    r = conn.read_one_row(st);
    BOOST_TEST(r == makerow());
    validate_eof(st);
}

BOOST_FIXTURE_TEST_CASE(multifn_read_some, tcp_network_fixture)
{
    connect();

    // Prepare the statement
    tcp_statement stmt;
    conn.prepare_statement("SELECT * FROM three_rows_table", stmt);

    // Execute it
    execution_state st;
    stmt.start_execution(std::make_tuple(), st);
    BOOST_TEST_REQUIRE(!st.complete());

    // We don't know how many rows there will be in each batch,
    // but they will come in order
    std::size_t call_count = 0;
    std::vector<row> all_rows;
    while (!st.complete() && call_count <= 4)
    {
        ++call_count;
        for (row_view rv : conn.read_some_rows(st))
            all_rows.emplace_back(rv);
    }

    // Verify rows
    BOOST_TEST_REQUIRE(all_rows.size() == 3u);
    BOOST_TEST(all_rows[0] == makerow(1, "f0"));
    BOOST_TEST(all_rows[1] == makerow(2, "f1"));
    BOOST_TEST(all_rows[2] == makerow(3, "f2"));

    // Verify eof
    validate_eof(st);

    // Reading again does nothing
    auto rows = conn.read_some_rows(st);
    BOOST_TEST(rows.empty());
    validate_eof(st);
}

BOOST_AUTO_TEST_SUITE_END()  // test_prepared_statements

}  // namespace