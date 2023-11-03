#include "config_tokenizer.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace diffy;
using namespace diffy::config_tokenizer;

namespace {

struct TokenDescriptor {
    std::string name;
    TokenId id;
    std::optional<std::string> character_sequence;
    std::optional<TokenId> termination_id;
    std::optional<TokenId> captured_string_id_tag;
};

// clang-format off
    const std::vector<TokenDescriptor> kTokens = {{
        // Name           Identifier             Start symbol   End symbol          Extra type tag
        { "Doubleslash",  TokenId_Doubleslash,   "//",          TokenId_Newline     , TokenId_Comment },
        { "Space",        TokenId_Space,         " ",           std::nullopt        , std::nullopt    },
        { "Newline",      TokenId_Newline,       "\n",          std::nullopt        , std::nullopt    },
        { "OpenBracket",  TokenId_OpenBracket,   "[" ,          std::nullopt        , std::nullopt    },
        { "CloseBracket", TokenId_CloseBracket,  "]" ,          std::nullopt        , std::nullopt    },
        { "Assign",       TokenId_Assign,        "=" ,          std::nullopt        , std::nullopt    },
        { "OpenCurly",    TokenId_OpenCurly,     "{" ,          std::nullopt        , std::nullopt    },
        { "CloseCurly",   TokenId_CloseCurly,    "}" ,          std::nullopt        , std::nullopt    },
        { "DoubleQuote",  TokenId_DoubleQuote,   "\"",          TokenId_DoubleQuote , std::nullopt    },
        { "SingleQuote",  TokenId_SingleQuote,   "'" ,          TokenId_SingleQuote , std::nullopt    },
        { "Hashtag",      TokenId_Hashtag,       "#" ,          TokenId_Newline     , TokenId_Comment },
        { "Comma",        TokenId_Comma,         "," ,          std::nullopt        , std::nullopt    },
        { "Backslash",    TokenId_Backslash,     "\\",          std::nullopt        , std::nullopt    },
        // Extra token annotation IDs. An identifier can be e.g:
        //   "String | EmptyString" or "String | Integer"...
        { "Boolean",      TokenId_Boolean,       std::nullopt , std::nullopt        , std::nullopt    },
        { "Integer",      TokenId_Integer,       std::nullopt , std::nullopt        , std::nullopt    },
        { "Float",        TokenId_Float,         std::nullopt , std::nullopt        , std::nullopt    },
        { "String",       TokenId_String,        std::nullopt , std::nullopt        , std::nullopt    },
        { "Identifier",   TokenId_Identifier,    std::nullopt , std::nullopt        , std::nullopt    },
        { "Comment",      TokenId_Comment,       std::nullopt , std::nullopt        , std::nullopt    },
        { "Terminator",   TokenId_Terminator,    std::nullopt , std::nullopt        , std::nullopt    },
        { "FirstOnLine",  TokenId_FirstOnLine,   std::nullopt , std::nullopt        , std::nullopt    },
    }};
// clang-format on

std::optional<TokenDescriptor>
as_token_desc(TokenId id) {
    for (const auto& desc : kTokens) {
        if (desc.id == id) {
            return desc;
        }
    }
    return std::nullopt;
}

}  // namespace

const std::string
Token::str_display_from(const std::string& input_text) const {
    auto original = str_from(input_text);
    std::string sanitized;
    for (auto& c : original) {
        switch (c) {
            case '\"':
                sanitized += "\\\"";
                break;
            case '\'':
                sanitized += "\\\'";
                break;
            case '\\':
                sanitized += "\\\\";
                break;
            case '\a':
                sanitized += "\\a";
                break;
            case '\b':
                sanitized += "\\b";
                break;
            case '\n':
                sanitized += "\\n";
                break;
            case '\t':
                sanitized += "\\t";
                break;
            // and so on
            default:
                if (iscntrl(c)) {
                    sanitized += fmt::format("{:03o}", c);
                } else {
                    sanitized += c;
                }
        }
    }
    return sanitized;
}

bool
diffy::config_tokenizer::is_whitespace(char c) {
    const char whitespaces[] = " \t\r\n\f\v";
    for (const auto whitespace : whitespaces) {
        if (whitespace == c) {
            return true;
        }
    }
    return false;
}

std::string
diffy::config_tokenizer::repr(TokenId id) {
    std::string s = "\033[1;34m";
    for (const auto& token : kTokens) {
        if (id & token.id) {
            s += "TokenId_" + token.name;
            s += "|";
        }
    }
    return s.substr(0, s.size() - 1) + "\033[0m";  // drop trailing pipe
}

void
diffy::config_tokenizer::token_dump(std::vector<Token> tokens, const std::string& source_text) {
    fmt::print("input text:\n{}\n---\ntokens:\n", source_text);
    int j = 1;
    for (auto& r : tokens) {
        fmt::print("{:02} [line: {:02}, col: {:02}, off: {:03}, len: {:2}, seq: {:2}]: {:18}    {}\n", j++,
                   r.line, r.column, r.start, r.length, r.sequence_index,
                   "'" + r.str_display_from(source_text) + "'", repr(r.id));
    }
}

// TODO: use the same style of state machine as in the parser?

bool
diffy::config_tokenizer::tokenize(const std::string& input_text, ParseOptions& options, ParseResult& result) {
    result.ok = false;
    result.error = "";

    if (input_text.size() == 0) {
        result.ok = true;
        return result.ok;
    }

    auto maxlen = input_text.size() - 1;
    std::size_t cursor = 0;
    auto text = std::string_view(input_text);

    auto is_prepended_with = [](std::string_view pre, std::string_view text) {
        return text.substr(0, pre.size()) == pre;
    };

    auto find_token = [&](const std::size_t start_idx, std::string_view text) -> std::optional<Token> {
        for (const auto& token : kTokens) {
            if (!token.character_sequence)
                continue;
            if (is_prepended_with(*token.character_sequence, text.substr(start_idx))) {
                std::size_t len = (*token.character_sequence).size();
                return {{start_idx, len, 0, 0, result.tokens.size(), (TokenId) token.id}};
            }
        }
        return std::nullopt;
    };

    auto is_first_token_on_line = [&input_text](auto start) {
        auto seeker = start;
        while (seeker > 0) {
            if (!is_whitespace(input_text[seeker])) {
                return false;
            } else if (input_text[seeker] == '\n') {
                return true;
            }
            seeker--;
        }
        return true;
    };

    std::string::size_type last_new_line_offset = 0;
    std::string::size_type current_line_number = 0;

    bool capture_string = false;
    std::string::size_type capture_string_start_idx = 0;
    TokenId string_terminator = TokenId_String;
    TokenId captured_string_id_tag = TokenId_String;

    bool reject_token = false;

    do {
        // start where we previously left off
        auto start_idx = cursor;
        reject_token = false;

        // done?
        if (start_idx >= text.length())
            break;

        bool terminated_string_capture = false;

        // inside a quoted string?
        if (capture_string) {
            for (auto i = start_idx; i < text.size(); i++) {
                if (auto tokopt = find_token(i, text); tokopt) {
                    auto tok = *tokopt;

                    if (tok.id == string_terminator) {
                        cursor = i;
                        reject_token = options.strip_annotated_string_tokens;
                        terminated_string_capture = true;
                        capture_string = false;
                        break;
                    }

                    // For non-newline terminated strings
                    if (tok.id & TokenId_Newline) {
                        result.ok = false;
                        result.error =
                            fmt::format("Unterminated string encountered newline (line {}, col {})",
                                        current_line_number + 1, cursor - last_new_line_offset + 1);
                        return result.ok;
                    }
                }
            }

            if (terminated_string_capture) {
                // we terminated at a valid token, so record the current string
                // and proceed processing the next token.
                Token token{start_idx,
                            cursor - start_idx,
                            current_line_number,
                            cursor - last_new_line_offset,
                            result.tokens.size(),
                            captured_string_id_tag};

                // Annotate tokens that start on a new line
                if (capture_string_start_idx == 0 || is_first_token_on_line(capture_string_start_idx - 1)) {
                    token.id |= TokenId_FirstOnLine;
                }

                bool reject_token = false;
                if (options.strip_comments && (token.id & TokenId_Comment)) {
                    reject_token = true;
                }

                if (!reject_token) {
                    result.tokens.push_back(token);
                }

                start_idx = cursor;
            } else {
                result.ok = false;
                result.error = fmt::format("Unterminated string starting on (line {}, col {}, seq)",
                                           current_line_number + 1, cursor - last_new_line_offset + 1);
                return result.ok;
            }
        }

        // we might have a token where our current cursor is (start_idx)
        if (auto next_token = find_token(start_idx, text); next_token) {
            auto tok = *next_token;
            auto tok_desc_opt = as_token_desc(tok.id);
            auto tok_desc = *tok_desc_opt;

            // we found a symbol that wants string capturing
            if (!terminated_string_capture && tok_desc.termination_id) {
                capture_string = true;
                capture_string_start_idx = tok.start;
                string_terminator = *tok_desc.termination_id;
                reject_token = options.strip_annotated_string_tokens;
                if (tok_desc.captured_string_id_tag) {
                    captured_string_id_tag = *tok_desc.captured_string_id_tag;
                } else {
                    captured_string_id_tag = TokenId_String;
                }
            }

            if (tok.id & TokenId_Newline) {
                current_line_number++;
                last_new_line_offset = start_idx;
            }

            if (tok.start == 0 || is_first_token_on_line(tok.start - 1)) {
                tok.id |= TokenId_FirstOnLine;
            }

            if (options.strip_spaces && (tok.id & TokenId_Space)) {
                reject_token = true;
            } else if (options.strip_newlines && (tok.id & TokenId_Newline)) {
                reject_token = true;
            } else if (options.strip_quotes && (tok.id & (TokenId_DoubleQuote | TokenId_SingleQuote))) {
                reject_token = true;
            }

            if (!reject_token) {
                tok.line = current_line_number;
                tok.column = start_idx - last_new_line_offset;
                tok.sequence_index = result.tokens.size();
                result.tokens.push_back(tok);
            }

            if (((tok.id & TokenId_Doubleslash) || tok.id & TokenId_Hashtag)) {
                // Keep the comment tokens for '# comment' and '// comment'
            } else {
                cursor += tok.length;
            }
            continue;
        }

        // if we don't, we're at a non-static symbol. identifier/bool/number
        // try to find the next token offset and record this word/string we're currently seeking past
        for (auto i = start_idx; i < text.size(); i++) {
            if (auto tokopt = find_token(i, text); tokopt) {
                cursor = i;
                break;
            }
        }

        // Couldn't find anything, so we're probably done. Record the trailing
        // stuff as an identifier.
        if (start_idx == cursor) {
            cursor = text.size();
        }

        Token token{start_idx,
                    cursor - start_idx,
                    current_line_number,
                    start_idx - last_new_line_offset,
                    result.tokens.size(),
                    TokenId_Identifier};

        auto token_str = token.str_from(input_text);

        auto to_lower = [](std::string input) {
            auto s = input;
            std::transform(s.begin(), s.end(), s.begin(), tolower);
            return s;
        };

        // Differentiate between identifiers and values.
        const std::tuple<bool, std::string> booleanic_strings[] = {
            {true, "true"}, {false, "false"}, {true, "yes"}, {false, "no"}, {true, "on"}, {false, "off"}};
        bool is_booleanic = false;
        bool tmp_bool = false;
        for (auto& [booleanic_value, booleanic_string] : booleanic_strings) {
            if (to_lower(token_str) == booleanic_string) {
                is_booleanic = true;
                tmp_bool = booleanic_value;
            }
        }

        if (is_booleanic) {
            token.id = TokenId_Boolean;
            token.token_boolean_arg = tmp_bool;
        } else {
            int tmp_int = 0;
            auto result = std::from_chars(token_str.data(), token_str.data() + token_str.size(), tmp_int);
            if (result.ec != std::errc::invalid_argument) {
                token.id = TokenId_Integer;
                token.token_int_arg = tmp_int;
            } else {
                int tmp_float = 0.0f;
                auto result =
                    std::from_chars(token_str.data(), token_str.data() + token_str.size(), tmp_float);
                if (result.ec != std::errc::invalid_argument) {
                    token.id = TokenId_Float;
                    token.token_float_arg = tmp_float;
                } else {
                    token.id = TokenId_Identifier;
                }
            }
        }
        if (token.start == 0 || is_first_token_on_line(token.start - 1)) {
            token.id |= TokenId_FirstOnLine;
        }
        result.tokens.push_back(token);

    } while (cursor <= maxlen);

    result.ok = true;
    if (options.append_terminator) {
        result.tokens.push_back(Token{0, 0, 0, 0, result.tokens.size(), TokenId_Terminator});
    }
    return result.ok;
}
