#include "readlines.hpp"

#include "util/hash.hpp"

#include <string>
#include <vector>
#include <limits.h>
#include <cstdlib>

namespace internal {

enum class LineParserSource {
    File,
    Memory,
};

struct LineParserState {

    LineParserState(FILE* file)
        : source_type(LineParserSource::File)
        , source_fp(file) {
    }

    LineParserState(const char* source_data)
        : source_type(LineParserSource::Memory)
        , source_str(source_data) {
    }

    int indentation_level = 0;

    bool scope_delay = false;
    int scope_level = 0;

    bool done = false;

    LineParserSource source_type;
    union {
        FILE* source_fp;
        const char* source_str;
    };

    int pos = 0;
    bool getc(char* c) {
        switch (source_type) {
            case LineParserSource::File: {
                *c = ::getc(source_fp);
                if (ferror(source_fp) || *c == EOF)
                    return false;
                return true;
            }
            case LineParserSource::Memory: {
                *c = source_str[pos++];
                if (*c == 0)
                    return false;
                return true;
            }
        }
    }

};



bool
getdelim(LineParserState& s, std::string& line, char delim) {
    if (s.done) {
        return false;
    }

    line.reserve(128);

    s.indentation_level = 0;
    bool count_whitespace = true;

    while (!s.done) {
        char c;
        if (!s.getc(&c)) {
            s.done = true;
        } else {
            line.push_back(c);
        }

        if (count_whitespace && (c == ' ' || c == '\t')) {
            s.indentation_level += (c == ' ') ? 1 : 4;
        } else {
            count_whitespace = false;
        }

        switch (c) {
            case '{':
                s.scope_level++;
                // Open scope on next line
                s.scope_delay = true;
                break;
            case '}':
                s.scope_level--;
                s.scope_delay = false;
                break;
            default:
                break;
        }

        if (c == delim) {
            break;
        }
    }

    return true;
}

bool
getline(LineParserState& state, std::string& line) {
    line.clear();
    return internal::getdelim(state, line, '\n');
}

std::string
right_trim(const std::string& s) {
    auto end = s.find_last_not_of(" \n\r\t\f\v");
    return (end == std::string::npos) ? s : s.substr(0, end + 1);
}

bool
parse(internal::LineParserState& state, std::vector<diffy::Line>& lines, bool ignore_line_endings) {
    std::string line;
    uint32_t i = 1;
    int prev_scope = 0;
    while (internal::getline(state, line)) {
        if (ignore_line_endings) {
            line = internal::right_trim(line);
        }

        int scope = 0;
        if (state.scope_delay) {
            scope = prev_scope;
            state.scope_delay = true;
        } else {
            scope = state.scope_level;
        }
        
        uint32_t hash = hash::hash(line.c_str(), static_cast<uint32_t>(line.size()));
        lines.push_back({i, hash, std::move(line), state.indentation_level, scope});
        i++;
        prev_scope = state.scope_level;
    }

    return true;
}

};  // namespace

bool
diffy::parselines(const std::string& input_text, std::vector<diffy::Line>& lines, bool ignore_line_endings) {
    lines.clear();
    internal::LineParserState state{input_text.data()};

    if (!internal::parse(state, lines, ignore_line_endings)) {
        return false;
    }

    return true;
}

bool
diffy::readlines(const std::string& path, std::vector<diffy::Line>& lines, bool ignore_line_endings) {
    lines.clear();

    FILE* stream = fopen(path.c_str(), "rb");
    if (!stream) {
        // NOTE: We check that the file exists as part of argument parsing, so this should
        //       never happen (tm).
        fprintf(stderr, "Failed to open file '%s'\n", path.c_str());
        return false;
    }

    internal::LineParserState state { stream };
    if (!internal::parse(state, lines, ignore_line_endings)) {
        fclose(stream);
        return false;
    }

    fclose(stream);
    return true;
}