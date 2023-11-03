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

//
// Debug / formatting utilities
//

void
cfg_dump_instructions(std::vector<TbInstruction>& inst);

std::string
cfg_dump_value_object(Value& v, int depth = 0);

bool
cfg_parse_dump(const std::string& input_data, ParseResult& result);

}  // namespace diffy