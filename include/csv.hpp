#pragma once

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace hft {

inline std::vector<std::string_view> split_csv_line(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t comma = line.find(',', start);
        if (comma == std::string_view::npos) {
            out.emplace_back(line.substr(start));
            break;
        }
        out.emplace_back(line.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

inline double parse_double(std::string_view value) {
    double result = 0.0;
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto parsed = std::from_chars(first, last, result);
    if (parsed.ec != std::errc() || parsed.ptr != last) {
        throw std::runtime_error("invalid floating point value: " + std::string(value));
    }
    return result;
}

inline std::int64_t parse_i64(std::string_view value) {
    std::int64_t result = 0;
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto parsed = std::from_chars(first, last, result);
    if (parsed.ec != std::errc() || parsed.ptr != last) {
        throw std::runtime_error("invalid integer value: " + std::string(value));
    }
    return result;
}

}  // namespace hft
