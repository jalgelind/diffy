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

// Utilities for finding values given a search path; does not support arrays.
// TODO: For arrays we'd need to support returning an iterator of results or something.
std::optional<std::reference_wrapper<Value>>
cfg_lookup_value_by_path(std::initializer_list<std::string> path_components, Value& root);

std::optional<std::reference_wrapper<Value>>
cfg_lookup_value_by_path(const std::string_view path, Value& root);

// TODO: initializer_list variant too?
bool
cfg_set_value_at(const std::string_view path, Value& root, Value& value);

//
// Debug / formatting utilities
//

std::string
repr(TbOperator s);

std::string
repr(TbValueType vt);

void
cfg_dump_instructions(std::vector<TbInstruction>& inst);

void
cfg_dump_value_object(Value& v, int depth = 0);

bool
cfg_parse_dump(const std::string& input_data, ParseResult& result);

}  // namespace diffy