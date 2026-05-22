#include "digit_source/FileDigitSource.hpp"
#include <stdexcept>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

FileDigitSource::FileDigitSource(const std::string& path)
    : path_(path), file_(path, std::ios::binary) {
    if (!file_) throw std::runtime_error("Cannot open digit file: " + path);

    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) throw std::runtime_error("Cannot open digit file (fd): " + path);

    off_t size = ::lseek(fd_, 0, SEEK_END);
    if (size < 0) throw std::runtime_error("Cannot seek digit file: " + path);
    digit_count_ = static_cast<uint64_t>(size);
}

FileDigitSource::~FileDigitSource() {
    if (fd_ >= 0) ::close(fd_);
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

std::size_t FileDigitSource::read_at(std::size_t digit_offset, uint8_t* buffer, std::size_t n) {
    if (n == 0) return 0;
    std::vector<char> raw(n);
    ssize_t got = ::pread(fd_, raw.data(), n, static_cast<off_t>(digit_offset));
    if (got <= 0) return 0;
    for (ssize_t i = 0; i < got; ++i)
        buffer[i] = static_cast<uint8_t>(raw[i] - '0');
    return static_cast<std::size_t>(got);
}
