#include "digit_source/FileDigitSource.hpp"
#include <stdexcept>
#include <vector>

FileDigitSource::FileDigitSource(const std::string& path)
    : path_(path), file_(path, std::ios::binary) {
    if (!file_) throw std::runtime_error("Cannot open digit file: " + path);
    file_.seekg(0, std::ios::end);
    digit_count_ = static_cast<uint64_t>(file_.tellg());
    file_.seekg(0, std::ios::beg);
}

std::size_t FileDigitSource::next_chunk(uint8_t* buffer, std::size_t n) {
    std::lock_guard<std::mutex> lock(mutex_);
    thread_local std::vector<char> tmp;
    tmp.resize(n);
    file_.read(tmp.data(), static_cast<std::streamsize>(n));
    auto count = static_cast<std::size_t>(file_.gcount());
    for (std::size_t i = 0; i < count; ++i)
        buffer[i] = static_cast<uint8_t>(tmp[i] - '0');
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
