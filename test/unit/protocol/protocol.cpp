//
// Copyright (c) 2019-2023 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "protocol/protocol.hpp"

#include <boost/mysql/client_errc.hpp>
#include <boost/mysql/column_type.hpp>
#include <boost/mysql/common_server_errc.hpp>
#include <boost/mysql/date.hpp>
#include <boost/mysql/datetime.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/error_categories.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/field_view.hpp>
#include <boost/mysql/mysql_collations.hpp>
#include <boost/mysql/string_view.hpp>

#include <boost/test/tools/context.hpp>
#include <boost/test/unit_test.hpp>

#include <array>

#include "operators.hpp"
#include "protocol/constants.hpp"
#include "protocol/db_flavor.hpp"
#include "serialization_test.hpp"
#include "test_common/create_basic.hpp"
#include "test_common/printing.hpp"
#include "test_unit/create_err.hpp"
#include "test_unit/create_meta.hpp"
#include "test_unit/create_ok.hpp"

using namespace boost::mysql::detail;
using namespace boost::mysql::test;
namespace collations = boost::mysql::mysql_collations;
using boost::span;
using boost::mysql::client_errc;
using boost::mysql::column_type;
using boost::mysql::common_server_errc;
using boost::mysql::date;
using boost::mysql::datetime;
using boost::mysql::diagnostics;
using boost::mysql::error_code;
using boost::mysql::field_view;
using boost::mysql::get_mariadb_server_category;
using boost::mysql::get_mysql_server_category;
using boost::mysql::string_view;

BOOST_AUTO_TEST_SUITE(test_protocol)

//
// Frame header
//
BOOST_AUTO_TEST_CASE(frame_header_serialization)
{
    struct
    {
        const char* name;
        frame_header value;
        std::array<std::uint8_t, 4> serialized;
    } test_cases[] = {
        {"small_packet_seqnum_0",     {3, 0},           {0x03, 0x00, 0x00, 0x00}},
        {"small_packet_seqnum_not_0", {9, 2},           {0x09, 0x00, 0x00, 0x02}},
        {"big_packet_seqnum_0",       {0xcacbcc, 0xfa}, {0xcc, 0xcb, 0xca, 0xfa}},
        {"max_packet_max_seqnum",     {0xffffff, 0xff}, {0xff, 0xff, 0xff, 0xff}}
    };

    for (const auto& tc : test_cases)
    {
        BOOST_TEST_CONTEXT(tc.name << " serialization")
        {
            serialization_buffer buffer(4);
            serialize_frame_header(tc.value, span<std::uint8_t, frame_header_size>(buffer.data(), 4));
            buffer.check(tc.serialized);
        }
        BOOST_TEST_CONTEXT(tc.name << " deserialization")
        {
            deserialization_buffer buffer(tc.serialized);
            auto actual = deserialize_frame_header(span<const std::uint8_t, frame_header_size>(buffer));
            BOOST_TEST(actual.size == tc.value.size);
            BOOST_TEST(actual.sequence_number == tc.value.sequence_number);
        }
    }
}

//
// OK packets
//
BOOST_AUTO_TEST_CASE(ok_view_success)
{
    struct
    {
        const char* name;
        ok_view expected;
        deserialization_buffer serialized;
    } test_cases[] = {
  // clang-format off
        {
            "successful_update",
            ok_builder()
                .affected_rows(4)
                .last_insert_id(0)
                .flags(SERVER_STATUS_AUTOCOMMIT | SERVER_QUERY_NO_INDEX_USED)
                .warnings(0)
                .info("Rows matched: 5  Changed: 4  Warnings: 0")
                .build(),
            {0x04, 0x00, 0x22, 0x00, 0x00, 0x00, 0x28, 0x52, 0x6f, 0x77, 0x73, 0x20, 0x6d, 0x61, 0x74, 0x63,
             0x68, 0x65, 0x64, 0x3a, 0x20, 0x35, 0x20, 0x20, 0x43, 0x68, 0x61, 0x6e, 0x67, 0x65, 0x64, 0x3a,
             0x20, 0x34, 0x20, 0x20, 0x57, 0x61, 0x72, 0x6e, 0x69, 0x6e, 0x67, 0x73, 0x3a, 0x20, 0x30},
        },
        {
            "successful_insert",
            ok_builder()
                .affected_rows(1)
                .last_insert_id(6)
                .flags(SERVER_STATUS_AUTOCOMMIT)
                .warnings(0)
                .info("")
                .build(),
            {0x01, 0x06, 0x02, 0x00, 0x00, 0x00},
        },
        {
            "successful_login",
            ok_builder()
                .affected_rows(0)
                .last_insert_id(0)
                .flags(SERVER_STATUS_AUTOCOMMIT)
                .warnings(0)
                .info("")
                .build(),
            {0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
        }
  // clang-format on
    };

    for (const auto& tc : test_cases)
    {
        BOOST_TEST_CONTEXT(tc.name)
        {
            ok_view actual{};
            error_code err = deserialize_ok_packet(tc.serialized, actual);

            // No error
            BOOST_TEST(err == error_code());

            // Actual value
            BOOST_TEST(actual.affected_rows == tc.expected.affected_rows);
            BOOST_TEST(actual.last_insert_id == tc.expected.last_insert_id);
            BOOST_TEST(actual.status_flags == tc.expected.status_flags);
            BOOST_TEST(actual.warnings == tc.expected.warnings);
            BOOST_TEST(actual.info == tc.expected.info);
        }
    }
}

BOOST_AUTO_TEST_CASE(ok_view_error)
{
    struct
    {
        const char* name;
        client_errc expected_err;
        deserialization_buffer serialized;
    } test_cases[] = {
        {"empty",                client_errc::incomplete_message, {}                                                    },
        {"error_affected_rows",  client_errc::incomplete_message, {0xff}                                                },
        {"error_last_insert_id", client_errc::incomplete_message, {0x01, 0xff}                                          },
        {"error_last_insert_id", client_errc::incomplete_message, {0x01, 0x06, 0x02}                                    },
        {"error_warnings",       client_errc::incomplete_message, {0x01, 0x06, 0x02, 0x00, 0x00}                        },
        {"error_info",           client_errc::incomplete_message, {0x04, 0x00, 0x22, 0x00, 0x00, 0x00, 0x28}            },
        {"extra_bytes",          client_errc::extra_bytes,        {0x01, 0x06, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}}
    };

    for (const auto& tc : test_cases)
    {
        BOOST_TEST_CONTEXT(tc.name)
        {
            ok_view value{};
            error_code err = deserialize_ok_packet(tc.serialized, value);
            BOOST_TEST(err == tc.expected_err);
        }
    }
}

//
// error packets
//
BOOST_AUTO_TEST_CASE(err_view_success)
{
    struct
    {
        const char* name;
        err_view expected;
        deserialization_buffer serialized;
    } test_cases[] = {
  // clang-format off
        {
            "wrong_use_database",
            {1049, "Unknown database 'a'"},
            {0x19, 0x04, 0x23, 0x34, 0x32, 0x30, 0x30, 0x30, 0x55, 0x6e, 0x6b, 0x6e, 0x6f, 0x77,
             0x6e, 0x20, 0x64, 0x61, 0x74, 0x61, 0x62, 0x61, 0x73, 0x65, 0x20, 0x27, 0x61, 0x27},
        },
        {
            "unknown_table",
            {1146, "Table 'awesome.unknown' doesn't exist"},
            {0x7a, 0x04, 0x23, 0x34, 0x32, 0x53, 0x30, 0x32, 0x54, 0x61, 0x62, 0x6c, 0x65, 0x20, 0x27,
             0x61, 0x77, 0x65, 0x73, 0x6f, 0x6d, 0x65, 0x2e, 0x75, 0x6e, 0x6b, 0x6e, 0x6f, 0x77, 0x6e,
             0x27, 0x20, 0x64, 0x6f, 0x65, 0x73, 0x6e, 0x27, 0x74, 0x20, 0x65, 0x78, 0x69, 0x73, 0x74},
        },
        {
            "failed_login",
            {1045, "Access denied for user 'root'@'localhost' (using password: YES)"},
            {0x15, 0x04, 0x23, 0x32, 0x38, 0x30, 0x30, 0x30, 0x41, 0x63, 0x63, 0x65, 0x73, 0x73, 0x20,
             0x64, 0x65, 0x6e, 0x69, 0x65, 0x64, 0x20, 0x66, 0x6f, 0x72, 0x20, 0x75, 0x73, 0x65, 0x72,
             0x20, 0x27, 0x72, 0x6f, 0x6f, 0x74, 0x27, 0x40, 0x27, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x68,
             0x6f, 0x73, 0x74, 0x27, 0x20, 0x28, 0x75, 0x73, 0x69, 0x6e, 0x67, 0x20, 0x70, 0x61, 0x73,
             0x73, 0x77, 0x6f, 0x72, 0x64, 0x3a, 0x20, 0x59, 0x45, 0x53, 0x29},
        },
        {
            "no_error_message",
            {1045, ""},
            {0x15, 0x04, 0x23, 0x32, 0x38, 0x30, 0x30, 0x30},
        }
  // clang-format on
    };

    for (const auto& tc : test_cases)
    {
        BOOST_TEST_CONTEXT(tc.name)
        {
            err_view actual{};
            error_code err = deserialize_error_packet(tc.serialized, actual);

            // No error
            BOOST_TEST(err == error_code());

            // Actual value
            BOOST_TEST(actual.error_code == tc.expected.error_code);
            BOOST_TEST(actual.error_message == tc.expected.error_message);
        }
    }
}

BOOST_AUTO_TEST_CASE(err_view_error)
{
    struct
    {
        const char* name;
        deserialization_buffer serialized;
    } test_cases[] = {
        {"empty",                  {}                      },
        {"error_error_code",       {0x15}                  },
        {"error_sql_state_marker", {0x15, 0x04}            },
        {"error_sql_state",        {0x15, 0x04, 0x23, 0x32}},
    };
    // Note: not possible to get extra bytes here, since the last field is a string_eof

    for (const auto& tc : test_cases)
    {
        BOOST_TEST_CONTEXT(tc.name)
        {
            err_view value{};
            error_code err = deserialize_error_packet(tc.serialized, value);
            BOOST_TEST(err == client_errc::incomplete_message);
        }
    }
}

BOOST_AUTO_TEST_CASE(process_error_packet_)
{
    // It's OK to use err_builder() here, since the deserialization function
    // has already been tested
    struct
    {
        const char* name;
        db_flavor flavor;
        deserialization_buffer serialized;
        error_code ec;
        const char* msg;
    } test_cases[] = {
        {"bad_error_packet", db_flavor::mariadb, {0xff, 0x00, 0x01}, client_errc::incomplete_message, ""},
        {"code_lt_min",
         db_flavor::mariadb,
         err_builder().code(999).message("abc").build_body_without_header(),
         error_code(999, get_mariadb_server_category()),
         "abc"},
        {"code_common",
         db_flavor::mariadb,
         err_builder().code(1064).message("abc").build_body_without_header(),
         common_server_errc::er_parse_error,
         "abc"},
        {"code_common_hole_mysql",
         db_flavor::mysql,
         err_builder().code(1076).build_body_without_header(),
         error_code(1076, get_mysql_server_category()),
         ""},
        {"code_common_hole_mariadb",
         db_flavor::mariadb,
         err_builder().code(1076).build_body_without_header(),
         error_code(1076, get_mariadb_server_category()),
         ""},
        {"code_mysql",
         db_flavor::mysql,
         err_builder().code(4004).build_body_without_header(),
         error_code(4004, get_mysql_server_category()),
         ""},
        {"code_mariadb",
         db_flavor::mariadb,
         err_builder().code(4004).build_body_without_header(),
         error_code(4004, get_mariadb_server_category()),
         ""},
    };

    for (const auto& tc : test_cases)
    {
        BOOST_TEST_CONTEXT(tc.name)
        {
            diagnostics diag;
            auto ec = process_error_packet(tc.serialized, tc.flavor, diag);
            BOOST_TEST(ec == tc.ec);
            BOOST_TEST(diag.server_message() == tc.msg);
        }
    }
}

//
// coldef
//
BOOST_AUTO_TEST_CASE(coldef_view_success)
{
    struct
    {
        const char* name;
        coldef_view expected;
        deserialization_buffer serialized;
    } test_cases[] = {
  // clang-format off
        {
            "numeric_auto_increment_primary_key",
            meta_builder()
                .database("awesome")
                .table("test_table")
                .org_table("test_table")
                .name("id")
                .org_name("id")
                .collation_id(collations::binary)
                .column_length(11)
                .type(column_type::int_)
                .flags(
                    column_flags::not_null | column_flags::pri_key | column_flags::auto_increment |
                    column_flags::part_key
                )
                .decimals(0)
                .build_coldef(),
            {
                0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65, 0x73, 0x6f, 0x6d, 0x65, 0x0a, 0x74,
                0x65, 0x73, 0x74, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x0a, 0x74, 0x65, 0x73, 0x74,
                0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x02, 0x69, 0x64, 0x02, 0x69, 0x64, 0x0c, 0x3f,
                0x00, 0x0b, 0x00, 0x00, 0x00, 0x03, 0x03, 0x42, 0x00, 0x00, 0x00
            },
        },
        {
            "varchar_field_aliased_field_and_table_names_join",
            meta_builder()
                .database("awesome")
                .table("child")
                .org_table("child_table")
                .name("field_alias")
                .org_name("field_varchar")
                .collation_id(collations::utf8_general_ci)
                .column_length(765)
                .type(column_type::varchar)
                .flags(0)
                .decimals(0)
                .build_coldef(),
            {
                0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65,
                0x73, 0x6f, 0x6d, 0x65, 0x05, 0x63, 0x68, 0x69,
                0x6c, 0x64, 0x0b, 0x63, 0x68, 0x69, 0x6c, 0x64,
                0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x0b, 0x66,
                0x69, 0x65, 0x6c, 0x64, 0x5f, 0x61, 0x6c, 0x69,
                0x61, 0x73, 0x0d, 0x66, 0x69, 0x65, 0x6c, 0x64,
                0x5f, 0x76, 0x61, 0x72, 0x63, 0x68, 0x61, 0x72,
                0x0c, 0x21, 0x00, 0xfd, 0x02, 0x00, 0x00, 0xfd,
                0x00, 0x00, 0x00, 0x00, 0x00
            },
        },
        {
            "float_field",
            meta_builder()
                .database("awesome")
                .table("test_table")
                .org_table("test_table")
                .name("field_float")
                .org_name("field_float")
                .collation_id(collations::binary)
                .column_length(12)
                .type(column_type::float_)
                .flags(0)
                .decimals(31)
                .build_coldef(),
            {
                0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65,
                0x73, 0x6f, 0x6d, 0x65, 0x0a, 0x74, 0x65, 0x73,
                0x74, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x0a,
                0x74, 0x65, 0x73, 0x74, 0x5f, 0x74, 0x61, 0x62,
                0x6c, 0x65, 0x0b, 0x66, 0x69, 0x65, 0x6c, 0x64,
                0x5f, 0x66, 0x6c, 0x6f, 0x61, 0x74, 0x0b, 0x66,
                0x69, 0x65, 0x6c, 0x64, 0x5f, 0x66, 0x6c, 0x6f,
                0x61, 0x74, 0x0c, 0x3f, 0x00, 0x0c, 0x00, 0x00,
                0x00, 0x04, 0x00, 0x00, 0x1f, 0x00, 0x00
            },
        },
        {
            "no_final_padding", // edge case
            meta_builder()
                .database("awesome")
                .table("test_table")
                .org_table("test_table")
                .name("field_float")
                .org_name("field_float")
                .collation_id(collations::binary)
                .column_length(12)
                .type(column_type::float_)
                .flags(0)
                .decimals(31)
                .build_coldef(),
            {
                0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65,
                0x73, 0x6f, 0x6d, 0x65, 0x0a, 0x74, 0x65, 0x73,
                0x74, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x0a,
                0x74, 0x65, 0x73, 0x74, 0x5f, 0x74, 0x61, 0x62,
                0x6c, 0x65, 0x0b, 0x66, 0x69, 0x65, 0x6c, 0x64,
                0x5f, 0x66, 0x6c, 0x6f, 0x61, 0x74, 0x0b, 0x66,
                0x69, 0x65, 0x6c, 0x64, 0x5f, 0x66, 0x6c, 0x6f,
                0x61, 0x74, 0x0a, 0x3f, 0x00, 0x0c, 0x00, 0x00,
                0x00, 0x04, 0x00, 0x00, 0x1f
            },
        },
        {
            "more_final_padding", // test for extensibility - we don't fail if mysql adds more fields in the end
            meta_builder()
                .database("awesome")
                .table("test_table")
                .org_table("test_table")
                .name("field_float")
                .org_name("field_float")
                .collation_id(collations::binary)
                .column_length(12)
                .type(column_type::float_)
                .flags(0)
                .decimals(31)
                .build_coldef(),
            {
                0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65,
                0x73, 0x6f, 0x6d, 0x65, 0x0a, 0x74, 0x65, 0x73,
                0x74, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x0a,
                0x74, 0x65, 0x73, 0x74, 0x5f, 0x74, 0x61, 0x62,
                0x6c, 0x65, 0x0b, 0x66, 0x69, 0x65, 0x6c, 0x64,
                0x5f, 0x66, 0x6c, 0x6f, 0x61, 0x74, 0x0b, 0x66,
                0x69, 0x65, 0x6c, 0x64, 0x5f, 0x66, 0x6c, 0x6f,
                0x61, 0x74, 0x0d, 0x3f, 0x00, 0x0c, 0x00, 0x00,
                0x00, 0x04, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00
            },
        },
  // clang-format on
    };

    for (const auto& tc : test_cases)
    {
        BOOST_TEST_CONTEXT(tc.name)
        {
            coldef_view actual{};
            error_code err = deserialize_column_definition(tc.serialized, actual);

            // No error
            BOOST_TEST_REQUIRE(err == error_code());

            // Actual value
            BOOST_TEST(actual.database == tc.expected.database);
            BOOST_TEST(actual.table == tc.expected.table);
            BOOST_TEST(actual.org_table == tc.expected.org_table);
            BOOST_TEST(actual.name == tc.expected.name);
            BOOST_TEST(actual.org_name == tc.expected.org_name);
            BOOST_TEST(actual.collation_id == tc.expected.collation_id);
            BOOST_TEST(actual.column_length == tc.expected.column_length);
            BOOST_TEST(actual.type == tc.expected.type);
            BOOST_TEST(actual.flags == tc.expected.flags);
            BOOST_TEST(actual.decimals == tc.expected.decimals);
        }
    }
}

BOOST_AUTO_TEST_CASE(coldef_view_error)
{
    struct
    {
        const char* name;
        error_code expected_err;
        deserialization_buffer serialized;
    } test_cases[] = {
  // clang-format off
        {
            "empty",
            client_errc::incomplete_message,
            {}
        },
        {
            "error_catalog",
            client_errc::incomplete_message,
            {0xff}
        },
        {
            "error_database",
            client_errc::incomplete_message,
            {0x03, 0x64, 0x65, 0x66, 0xff}
        },
        {
            "error_table",
            client_errc::incomplete_message,
            {0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65, 0x73, 0x6f, 0x6d, 0x65, 0xff}
        },
        {   "error_org_table",
            client_errc::incomplete_message,
            {0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65, 0x73, 0x6f, 0x6d, 0x65, 0x05,
            0x63, 0x68, 0x69, 0x6c, 0x64, 0xff}
        },
        {
            "error_name",
            client_errc::incomplete_message,
            {0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65, 0x73, 0x6f, 0x6d, 0x65, 0x05, 0x63, 0x68, 0x69,
            0x6c, 0x64, 0x0b, 0x63, 0x68, 0x69, 0x6c, 0x64, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0xff}
        },
        {
            "error_org_name",
            client_errc::incomplete_message,
            {0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65, 0x73, 0x6f, 0x6d, 0x65, 0x05, 0x63, 0x68,
            0x69, 0x6c, 0x64, 0x0b, 0x63, 0x68, 0x69, 0x6c, 0x64, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65,
            0x0b, 0x66, 0x69, 0x65, 0x6c, 0x64, 0x5f, 0x61, 0x6c, 0x69, 0x61, 0x73, 0xff}
        },
        {
            "error_fixed_fields",
            client_errc::incomplete_message,
            {0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65, 0x73, 0x6f, 0x6d, 0x65, 0x05, 0x63, 0x68,
            0x69, 0x6c, 0x64, 0x0b, 0x63, 0x68, 0x69, 0x6c, 0x64, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65,
            0x0b, 0x66, 0x69, 0x65, 0x6c, 0x64, 0x5f, 0x61, 0x6c, 0x69, 0x61, 0x73, 0x0d, 0x66, 0x69,
            0x65, 0x6c, 0x64, 0x5f, 0x76, 0x61, 0x72, 0x63, 0x68, 0x61, 0x72, 0xff}
        },
        {
            "error_collation_id",
            client_errc::incomplete_message,
            {0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65, 0x73, 0x6f, 0x6d, 0x65, 0x05, 0x63, 0x68,
            0x69, 0x6c, 0x64, 0x0b, 0x63, 0x68, 0x69, 0x6c, 0x64, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65,
            0x0b, 0x66, 0x69, 0x65, 0x6c, 0x64, 0x5f, 0x61, 0x6c, 0x69, 0x61, 0x73, 0x0d, 0x66, 0x69,
            0x65, 0x6c, 0x64, 0x5f, 0x76, 0x61, 0x72, 0x63, 0x68, 0x61, 0x72, 0x01, 0x00}
        },
        {
            "error_column_length",
            client_errc::incomplete_message,
            {0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65, 0x73, 0x6f, 0x6d, 0x65, 0x05, 0x63, 0x68,
            0x69, 0x6c, 0x64, 0x0b, 0x63, 0x68, 0x69, 0x6c, 0x64, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65,
            0x0b, 0x66, 0x69, 0x65, 0x6c, 0x64, 0x5f, 0x61, 0x6c, 0x69, 0x61, 0x73, 0x0d, 0x66, 0x69,
            0x65, 0x6c, 0x64, 0x5f, 0x76, 0x61, 0x72, 0x63, 0x68, 0x61, 0x72, 0x03, 0x00, 0x00, 0x00}
        },
        {
            "error_column_type",
            client_errc::incomplete_message,
            {0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65, 0x73, 0x6f, 0x6d, 0x65, 0x05, 0x63, 0x68, 0x69,
            0x6c, 0x64, 0x0b, 0x63, 0x68, 0x69, 0x6c, 0x64, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x0b, 0x66,
            0x69, 0x65, 0x6c, 0x64, 0x5f, 0x61, 0x6c, 0x69, 0x61, 0x73, 0x0d, 0x66, 0x69, 0x65, 0x6c, 0x64,
            0x5f, 0x76, 0x61, 0x72, 0x63, 0x68, 0x61, 0x72, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
        },
        {
            "error_flags",
            client_errc::incomplete_message,
            {0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65, 0x73, 0x6f, 0x6d, 0x65, 0x05,
            0x63, 0x68, 0x69, 0x6c, 0x64, 0x0b, 0x63, 0x68, 0x69, 0x6c, 0x64, 0x5f, 0x74,
            0x61, 0x62, 0x6c, 0x65, 0x0b, 0x66, 0x69, 0x65, 0x6c, 0x64, 0x5f, 0x61, 0x6c,
            0x69, 0x61, 0x73, 0x0d, 0x66, 0x69, 0x65, 0x6c, 0x64, 0x5f, 0x76, 0x61, 0x72,
            0x63, 0x68, 0x61, 0x72, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
        },
        {
            "error_decimals",
            client_errc::incomplete_message,
            {0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65, 0x73, 0x6f, 0x6d, 0x65, 0x05, 0x63,
            0x68, 0x69, 0x6c, 0x64, 0x0b, 0x63, 0x68, 0x69, 0x6c, 0x64, 0x5f, 0x74, 0x61, 0x62,
            0x6c, 0x65, 0x0b, 0x66, 0x69, 0x65, 0x6c, 0x64, 0x5f, 0x61, 0x6c, 0x69, 0x61, 0x73,
            0x0d, 0x66, 0x69, 0x65, 0x6c, 0x64, 0x5f, 0x76, 0x61, 0x72, 0x63, 0x68, 0x61, 0x72,
            0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
        },
        {
            "extra_bytes",
            client_errc::extra_bytes,
            {0x03, 0x64, 0x65, 0x66, 0x07, 0x61, 0x77, 0x65,
            0x73, 0x6f, 0x6d, 0x65, 0x0a, 0x74, 0x65, 0x73,
            0x74, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x0a,
            0x74, 0x65, 0x73, 0x74, 0x5f, 0x74, 0x61, 0x62,
            0x6c, 0x65, 0x0b, 0x66, 0x69, 0x65, 0x6c, 0x64,
            0x5f, 0x66, 0x6c, 0x6f, 0x61, 0x74, 0x0b, 0x66,
            0x69, 0x65, 0x6c, 0x64, 0x5f, 0x66, 0x6c, 0x6f,
            0x61, 0x74, 0x0d, 0x3f, 0x00, 0x0c, 0x00, 0x00,
            0x00, 0x04, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0xff}
        }
  // clang-format on
    };

    for (const auto& tc : test_cases)
    {
        BOOST_TEST_CONTEXT(tc.name)
        {
            coldef_view value{};
            error_code err = deserialize_column_definition(tc.serialized, value);
            BOOST_TEST(err == tc.expected_err);
        }
    }
}

// TODO: move this to common section
template <class T>
void do_serialize_toplevel_test(const T& value, span<const std::uint8_t> serialized)
{
    // Size
    std::size_t expected_size = serialized.size();
    std::size_t actual_size = value.get_size();
    BOOST_TEST(actual_size == expected_size);

    // Serialize
    serialization_buffer buffer(actual_size);
    value.serialize(buffer);

    // Check buffer
    buffer.check(serialized);
}

//
// quit
//
BOOST_AUTO_TEST_CASE(quit_serialization)
{
    quit_command cmd;
    const std::uint8_t serialized[] = {0x01};
    do_serialize_toplevel_test(cmd, serialized);
}

//
// ping
//
BOOST_AUTO_TEST_CASE(ping_serialization)
{
    ping_command cmd;
    const std::uint8_t serialized[] = {0x0e};
    do_serialize_toplevel_test(cmd, serialized);
}

BOOST_AUTO_TEST_CASE(deserialize_ping_response_)
{
    struct
    {
        const char* name;
        deserialization_buffer message;
        error_code expected_err;
        const char* expected_msg;
    } test_cases[] = {
        {"success",              ok_builder().build_ok_body(),                                error_code(),                      ""},
        {"empty_message",        {},                                                          client_errc::incomplete_message,   ""},
        {"invalid_message_type", {0xab},                                                      client_errc::protocol_value_error, ""},
        {"bad_ok_packet",        {0x00, 0x01},                                                client_errc::incomplete_message,   ""},
        {"err_packet",
         err_builder().code(common_server_errc::er_bad_db_error).message("abc").build_body(),
         common_server_errc::er_bad_db_error,
         "abc"                                                                                                                     },
        {"bad_err_packet",       {0xff, 0x01},                                                client_errc::incomplete_message,   ""},
    };

    for (const auto& tc : test_cases)
    {
        BOOST_TEST_CONTEXT(tc.name)
        {
            diagnostics diag;
            auto err = deserialize_ping_response(tc.message, db_flavor::mariadb, diag);

            BOOST_TEST(err == tc.expected_err);
            BOOST_TEST(diag.server_message() == tc.expected_msg);
        }
    }
}

//
// query
//
BOOST_AUTO_TEST_CASE(query_serialization)
{
    query_command cmd{"show databases"};
    const std::uint8_t serialized[] =
        {0x03, 0x73, 0x68, 0x6f, 0x77, 0x20, 0x64, 0x61, 0x74, 0x61, 0x62, 0x61, 0x73, 0x65, 0x73};
    do_serialize_toplevel_test(cmd, serialized);
}

//
// prepare statement
//
BOOST_AUTO_TEST_CASE(prepare_statement_serialization)
{
    prepare_stmt_command cmd{"SELECT * from three_rows_table WHERE id = ?"};
    const std::uint8_t serialized[] = {0x16, 0x53, 0x45, 0x4c, 0x45, 0x43, 0x54, 0x20, 0x2a, 0x20, 0x66,
                                       0x72, 0x6f, 0x6d, 0x20, 0x74, 0x68, 0x72, 0x65, 0x65, 0x5f, 0x72,
                                       0x6f, 0x77, 0x73, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x20, 0x57,
                                       0x48, 0x45, 0x52, 0x45, 0x20, 0x69, 0x64, 0x20, 0x3d, 0x20, 0x3f};
    do_serialize_toplevel_test(cmd, serialized);
}

BOOST_AUTO_TEST_CASE(deserialize_prepare_stmt_response_impl_success)
{
    // Data (statement_id, num fields, num params)
    prepare_stmt_response expected{1, 2, 3};
    deserialization_buffer serialized{0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00};
    prepare_stmt_response actual{};
    auto err = deserialize_prepare_stmt_response_impl(serialized, actual);

    // No error
    BOOST_TEST_REQUIRE(err == error_code());

    // Actual value
    BOOST_TEST(actual.id == expected.id);
    BOOST_TEST(actual.num_columns == expected.num_columns);
    BOOST_TEST(actual.num_params == expected.num_params);
}

BOOST_AUTO_TEST_CASE(deserialize_prepare_stmt_response_impl_error)
{
    struct
    {
        const char* name;
        error_code expected_err;
        deserialization_buffer serialized;
    } test_cases[] = {
        {"empty",              client_errc::incomplete_message, {}                                              },
        {"error_id",           client_errc::incomplete_message, {0x01}                                          },
        {"error_num_columns",  client_errc::incomplete_message, {0x01, 0x00, 0x00, 0x00, 0x02}                  },
        {"error_num_params",   client_errc::incomplete_message, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03}      },
        {"error_reserved",     client_errc::incomplete_message, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00}},
        {"error_num_warnings",
         client_errc::incomplete_message,
         {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00}                                           },
        {"extra_bytes",
         client_errc::extra_bytes,
         {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0xff}                               },
    };

    for (const auto& tc : test_cases)
    {
        BOOST_TEST_CONTEXT(tc.name)
        {
            prepare_stmt_response output{};
            auto err = deserialize_prepare_stmt_response_impl(tc.serialized, output);
            BOOST_TEST(err == tc.expected_err);
        }
    }
}

BOOST_AUTO_TEST_CASE(deserialize_prepare_stmt_response_success)
{
    // Data (statement_id, num fields, num params)
    prepare_stmt_response expected{1, 2, 3};
    deserialization_buffer serialized{0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00};
    prepare_stmt_response actual{};
    diagnostics diag;

    auto err = deserialize_prepare_stmt_response(serialized, db_flavor::mysql, actual, diag);

    // No error
    BOOST_TEST_REQUIRE(err == error_code());
    BOOST_TEST(diag == diagnostics());

    // Actual value
    BOOST_TEST(actual.id == expected.id);
    BOOST_TEST(actual.num_columns == expected.num_columns);
    BOOST_TEST(actual.num_params == expected.num_params);
}

BOOST_AUTO_TEST_CASE(deserialize_prepare_stmt_response_error)
{
    struct
    {
        const char* name;
        error_code expected_err;
        const char* expected_diag;
        deserialization_buffer serialized;
    } test_cases[] = {
  // clang-format off
        {
            "error_message_type",
            client_errc::incomplete_message,
            "",
            {},
        },
        {
            "unknown_message_type",
            client_errc::protocol_value_error,
            "",
            {0xab, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00},
        },
        {
            "error_packet",
            common_server_errc::er_bad_db_error,
            "bad db",
            err_builder().code(common_server_errc::er_bad_db_error).message("bad db").build_body(),
        },
        {
            "error_deserializing_response",
            client_errc::incomplete_message,
            "",
            {0x00, 0x01, 0x00},
        },
  // clang-format on
    };

    for (const auto& tc : test_cases)
    {
        BOOST_TEST_CONTEXT(tc.name)
        {
            prepare_stmt_response output{};
            diagnostics diag;

            auto err = deserialize_prepare_stmt_response(tc.serialized, db_flavor::mariadb, output, diag);

            BOOST_TEST(err == tc.expected_err);
            BOOST_TEST(diag.server_message() == tc.expected_diag);
        }
    }
}

//
// execute statement
//
BOOST_AUTO_TEST_CASE(execute_statement_serialization)
{
    constexpr std::uint8_t blob_buffer[] = {0x70, 0x00, 0x01, 0xff};

    struct
    {
        const char* name;
        std::uint32_t stmt_id;
        std::vector<field_view> params;
        std::vector<std::uint8_t> serialized;
    } test_cases[] = {
  // clang-format off
        {
            "uint64_t",
            1,
            make_fv_vector(std::uint64_t(0xabffffabacadae)),
            {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x08, 0x80, 0xae, 0xad, 0xac, 0xab, 0xff, 0xff, 0xab, 0x00},
        },
        {
            "int64_t",
            1,
            make_fv_vector(std::int64_t(-0xabffffabacadae)),
            {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x08, 0x00, 0x52, 0x52, 0x53, 0x54, 0x00, 0x00, 0x54, 0xff}
        },
        {
            "string",
            1,
            make_fv_vector(string_view("test")),
            {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
            0x01, 0xfe, 0x00, 0x04, 0x74, 0x65, 0x73, 0x74}
        },
        {
            "blob",
            1,
            make_fv_vector(span<const std::uint8_t>(blob_buffer)),
            {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
            0x01, 0xfc, 0x00, 0x04, 0x70, 0x00, 0x01, 0xff}
        },
        {
            "float",
            1,
            make_fv_vector(3.14e20f),
            {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x04, 0x00, 0x01, 0x2d, 0x88, 0x61}
        },
        {
            "double",
            1,
            make_fv_vector(2.1e214),
            {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x05, 0x00, 0x56, 0xc0, 0xee, 0xa6, 0x95, 0x30, 0x6f, 0x6c}
        },
        {
            "date",
            1,
            make_fv_vector(date(2010u, 9u, 3u)),
            {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x0a, 0x00, 0x04, 0xda, 0x07, 0x09, 0x03}
        },
        {
            "datetime",
            1,
            make_fv_vector(datetime(2010u, 9u, 3u, 10u, 30u, 59u, 231800u)),
            {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x0c, 0x00, 0x0b, 0xda, 0x07, 0x09, 0x03, 0x0a, 0x1e, 0x3b,
            0x78, 0x89, 0x03, 0x00}
        },
        {
            "time",
            1,
            make_fv_vector(maket(230, 30, 59, 231800)),
            {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x0b, 0x00, 0x0c, 0x00, 0x09, 0x00, 0x00, 0x00, 0x0e, 0x1e,
            0x3b, 0x78, 0x89, 0x03, 0x00}
        },
        {
            "null",
            1,
            make_fv_vector(nullptr),
            {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x06, 0x00}
        },
        {
            "several_params",
            2,
            make_fv_vector(
                std::uint64_t(0xabffffabacadae),
                std::int64_t(-0xabffffabacadae),
                string_view("test"),
                nullptr,
                2.1e214,
                date(2010u, 9u, 3u),
                datetime(2010u, 9u, 3u, 10u, 30u, 59u, 231800u),
                maket(230, 30, 59, 231800),
                nullptr
            ),
            {0x17, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x01,
            0x01, 0x08, 0x80, 0x08, 0x00, 0xfe, 0x00, 0x06, 0x00, 0x05, 0x00, 0x0a,
            0x00, 0x0c, 0x00, 0x0b, 0x00, 0x06, 0x00, 0xae, 0xad, 0xac, 0xab, 0xff,
            0xff, 0xab, 0x00, 0x52, 0x52, 0x53, 0x54, 0x00, 0x00, 0x54, 0xff, 0x04,
            0x74, 0x65, 0x73, 0x74, 0x56, 0xc0, 0xee, 0xa6, 0x95, 0x30, 0x6f, 0x6c,
            0x04, 0xda, 0x07, 0x09, 0x03, 0x0b, 0xda, 0x07, 0x09, 0x03, 0x0a, 0x1e,
            0x3b, 0x78, 0x89, 0x03, 0x00, 0x0c, 0x00, 0x09, 0x00, 0x00, 0x00, 0x0e,
            0x1e, 0x3b, 0x78, 0x89, 0x03, 0x00}
        },
        {
            "empty",
            1,
            {},
            {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}
        }
  // clang-format on
    };

    for (const auto& tc : test_cases)
    {
        BOOST_TEST_CONTEXT(tc.name)
        {
            execute_stmt_command cmd{tc.stmt_id, tc.params};
            do_serialize_toplevel_test(cmd, tc.serialized);
        }
    }
}

//
// close statement
//
BOOST_AUTO_TEST_CASE(close_statement_serialization)
{
    close_stmt_command cmd{1};
    const std::uint8_t serialized[] = {0x19, 0x01, 0x00, 0x00, 0x00};
    do_serialize_toplevel_test(cmd, serialized);
}

BOOST_AUTO_TEST_SUITE_END()
