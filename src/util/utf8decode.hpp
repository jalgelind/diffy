#pragma once

// Björn Höhrmann's utf8-routines.

/*
Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in the
Software without restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <cstdint>
#include <string>

namespace diffy {

uint32_t inline utf8_decode(uint32_t* state, uint32_t* codep, uint32_t byte);

// Count code points inside given range.
// TODO: Report error if end is inside a codepoint?
int64_t
utf8_len(const std::string& s, std::string::size_type start, std::string::size_type end);

// Count code points contained in a std::string
int64_t
utf8_len(const std::string& s);

std::string::size_type
utf8_advance_by(const std::string& s, std::string::size_type start, std::size_t index);

}  // namespace diffy