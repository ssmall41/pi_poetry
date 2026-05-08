#include "digit_source/FileDigitSource.hpp"
#include <stdexcept>
#include <vector>

FileDigitSource::FileDigitSource(const std::string& path)
    : path_(path), file_(path, std::ios::binary) {
    if (!file_) throw std::runtime_error("Cannot open digit file: " + path);
    char c;
    while (file_.get(c))
        if (c >= '0' && c <= '9') ++digit_count_;
    file_.clear();
    file_.seekg(0, std::ios::beg);
}

std::size_t FileDigitSource::next_chunk(uint8_t* buffer, std::size_t n) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    char c;
    while (count < n && file_.get(c))
        if (c >= '0' && c <= '9')
            buffer[count++] = static_cast<uint8_t>(c - '0');
    return count;
}

void FileDigitSource::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    file_.clear();
    file_.seekg(0, std::ios::beg);
}

std::optional<uint64_t> FileDigitSource::estimated_length() const {
    return digit_count_;
}
