#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gsl/span>

namespace diffy {

// Read-only access to a file's bytes. Large regular files are memory-mapped so a
// multi-GB input isn't copied into (or held resident in) the heap; small files,
// non-regular inputs (FIFOs, /dev/null from git difftool), Windows, and any mmap
// failure fall back to a plain read into an owned buffer.
//
// Move-only. The mapping/buffer lives until the object is destroyed, so keep the
// FileBytes alive for as long as the span it hands out is in use.
class FileBytes {
   public:
    FileBytes() = default;
    ~FileBytes();
    FileBytes(FileBytes&& other) noexcept;
    FileBytes& operator=(FileBytes&& other) noexcept;
    FileBytes(const FileBytes&) = delete;
    FileBytes& operator=(const FileBytes&) = delete;

    // Load the bytes of `path`. Returns false on I/O error (state stays empty).
    bool
    load(const std::string& path);

    gsl::span<const uint8_t>
    bytes() const {
        return gsl::span<const uint8_t>(data_, static_cast<std::ptrdiff_t>(size_));
    }

    const uint8_t*
    data() const {
        return data_;
    }
    size_t
    size() const {
        return size_;
    }

   private:
    void
    reset();

    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    void* map_base_ = nullptr;  // non-null => munmap in the destructor
    size_t map_len_ = 0;
    std::vector<uint8_t> owned_;  // backing when not memory-mapped
};

// Regular files at least this large are memory-mapped instead of read.
constexpr size_t kMmapThreshold = 64 * 1024;

}  // namespace diffy
