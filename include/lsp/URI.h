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
#include <string>

class URI {
    /// File URIs- assumes file:// prefix

public:
    using ReflectionType = std::string;

    URI() = default;
    ~URI() = default;

    URI(const std::string& uri_str);

    /// Necessary for the serialization to work.
    ReflectionType reflection() const;

    /// Expresses the underlying URI as a string.
    std::string str() const;

    // Constructor from components
    URI(std::string scheme, std::string authority, std::string path, std::string query = "",
        std::string fragment = "");

    static URI fromFile(const std::filesystem::path& file);

    static URI fromWeb(const std::string& url);

    std::string_view getPath() const;

    // Equality operator (needed for unordered_map)
    bool operator==(URI const& other) const;

private:
    std::string scheme;
    std::string authority;
    std::string path;
    std::string query;
    std::string fragment;

    mutable std::string decodedPath_;
    mutable std::string underlying_;

    void parse(const std::string& uri_str);

    // Decode a percent-encoded string
    static std::string decode(const std::string& s);

    static std::string encode(const std::string& s);

    /// Converts a char to hexadecimal
    static std::string to_hex(unsigned char c);

    std::string to_path(std::string decodedPath) const;
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
