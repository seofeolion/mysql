//
// Copyright (c) 2019-2023 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/mysql/metadata.hpp>

#include "protocol/protocol.hpp"

boost::mysql::metadata::metadata(const detail::coldef_view& msg, bool copy_strings)
    : schema_(copy_strings ? msg.database : string_view()),
      table_(copy_strings ? msg.table : string_view()),
      org_table_(copy_strings ? msg.org_table : string_view()),
      name_(copy_strings ? msg.column_name : string_view()),
      org_name_(copy_strings ? msg.org_column_name : string_view()),
      character_set_(msg.collation_id),
      column_length_(msg.column_length),
      type_(msg.type),
      flags_(msg.flags),
      decimals_(msg.decimals)
{
}