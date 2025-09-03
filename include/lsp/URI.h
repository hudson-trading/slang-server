//------------------------------------------------------------------------------
// URI.h
// URI class for handling file and web resource identifiers
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once
#include <filesystem>
#include <fmt/format.h>
#include <iostream>
#include <string>

class URI {
    /// File URIs- assumes file:// prefix

public:
    using ReflectionType = std::string;

    URI() = default;

    URI(const char* _str) : URI("file", std::string(_str)) {}

    URI(const std::string& _str) { underlying_ = _str; }

    URI(const std::string& protocol, const std::string& path) {
        underlying_ = protocol + "://" + path;
    }

    URI(const std::string_view _str) : URI(std::string(_str)) {}

    ~URI() = default;

    // Equality operator (needed for unordered_map)
    bool operator==(const URI& other) const { return underlying_ == other.underlying_; }

    /// Necessary for the serialization to work.
    ReflectionType reflection() const { return underlying_; }

    /// Expresses the underlying URI as a string.
    std::string str() const { return reflection(); }

    std::string_view getPath() const {
        if (underlying_.size() < 7) {
            return {};
        }
        return std::string_view(underlying_).substr(7);
    }

    static URI fromFile(const std::filesystem::path& file) { return URI("file", file.string()); }

    static URI fromWeb(std::string_view path) { return URI("https", std::string(path)); }

    bool empty() const { return underlying_.empty(); }

    friend std::ostream& operator<<(std::ostream& os, const URI& uri) {
        os << uri.str();
        return os;
    }

    // allow appending to strings
    friend std::string operator+(const std::string& str, const URI& uri) { return str + uri.str(); }
    friend std::string operator+(const URI& uri, const std::string& str) { return uri.str() + str; }

private:
    /// The underlying string
    std::string underlying_;
};
template<>
struct fmt::formatter<URI> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    constexpr auto format(const URI& uri, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", uri.str());
    }
};

namespace std {
template<>
struct hash<URI> {
    std::size_t operator()(const URI& uri) const noexcept {
        return std::hash<std::string>{}(uri.str());
    }
};

} // namespace std