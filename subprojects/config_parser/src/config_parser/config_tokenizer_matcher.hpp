#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config_tokenizer.hpp"

namespace diffy {
namespace config_tokenizer {

struct SequencePoint {
    TokenId id;
    std::string ident_match;
};

struct SequenceMatch {
    int start;
    int end;
};

std::string
repr(SequencePoint p);

bool
token_match(const Token& token, const std::string& token_text, const SequencePoint& point);

bool
reverse_find_sequence(std::vector<Token>& input, const std::string& input_text, std::vector<SequencePoint> sequence, SequenceMatch* result);

} // namespace onfig_tokenizer
} // namespace diffy