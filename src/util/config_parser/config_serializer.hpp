#pragma once

#include "config_parser.hpp"

namespace diffy {

// Serialize all entries in the given Value. We output [section] headers
// for the keys in the root table. The input Value must hold a Value::Table.
std::string
cfg_serialize(Value& value, int depth = 0);

// Serialize any value. Does not output sections.
std::string
cfg_serialize_obj(Value& value);

}  // namespace diffy