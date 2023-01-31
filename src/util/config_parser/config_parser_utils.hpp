#pragma once

#include "config_parser.hpp"

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace diffy {

// Load a file and construct a value tree based on the contents
bool
cfg_load_file(const std::string& file_path, ParseResult& result, Value& result_obj);

// Load a file and invoke the callback for each instruction
bool
cfg_load_file(const std::string& file_path,
              ParseResult& result,
              std::function<void(TbInstruction)> consume_instruction);

// Serialize all entries in the given Value. We output [section] headers
// for the keys in the root table. The input Value must hold a Value::Table.
std::string
cfg_serialize(Value& value, int depth = 0);

// Serialize any value. Does not output sections.
std::string
cfg_serialize_obj(Value& value);

//
// Debug / formatting utilities
//

std::string
repr(TbOperator s);

std::string
repr(TbValueType vt);

void
cfg_dump_instructions(std::vector<TbInstruction>& inst);

std::string
cfg_dump_value_object(Value& v, int depth = 0);

bool
cfg_parse_dump(const std::string& input_data, ParseResult& result);

}  // namespace diffy