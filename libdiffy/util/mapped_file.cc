#include "util/mapped_file.hpp"

#include "util/read_bytes.hpp"

#include <utility>

#include <sys/stat.h>

#if !defined(DIFFY_PLATFORM_WINDOWS)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace diffy {

FileBytes::~FileBytes() {
    reset();
}

void
FileBytes::reset() {
#if !defined(DIFFY_PLATFORM_WINDOWS)
    if (map_base_ != nullptr) {
        munmap(map_base_, map_len_);
    }
#endif
    map_base_ = nullptr;
    map_len_ = 0;
    data_ = nullptr;
    size_ = 0;
    owned_.clear();
    owned_.shrink_to_fit();
}

FileBytes::FileBytes(FileBytes&& other) noexcept {
    *this = std::move(other);
}

FileBytes&
FileBytes::operator=(FileBytes&& other) noexcept {
    if (this != &other) {
        reset();
        map_base_ = other.map_base_;
        map_len_ = other.map_len_;
        size_ = other.size_;
        owned_ = std::move(other.owned_);
        // If the bytes were owned (not mapped), our moved-into vector now holds
        // them; re-point at it. Otherwise keep the mmap pointer verbatim.
        data_ = (map_base_ == nullptr) ? (owned_.empty() ? nullptr : owned_.data()) : other.data_;

        other.data_ = nullptr;
        other.size_ = 0;
        other.map_base_ = nullptr;
        other.map_len_ = 0;
    }
    return *this;
}

bool
FileBytes::load(const std::string& path) {
    reset();

#if !defined(DIFFY_PLATFORM_WINDOWS)
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
        static_cast<size_t>(st.st_size) >= kMmapThreshold) {
        const int fd = open(path.c_str(), O_RDONLY);
        if (fd >= 0) {
            const size_t len = static_cast<size_t>(st.st_size);
            void* p = mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0);
            close(fd);
            if (p != MAP_FAILED) {
                map_base_ = p;
                map_len_ = len;
                data_ = static_cast<const uint8_t*>(p);
                size_ = len;
                return true;
            }
            // mmap failed (e.g. filesystem quirk); fall through to a plain read.
        }
    }
#endif

    if (!read_file_bytes(path, owned_)) {
        reset();
        return false;
    }
    data_ = owned_.empty() ? nullptr : owned_.data();
    size_ = owned_.size();
    return true;
}

}  // namespace diffy
