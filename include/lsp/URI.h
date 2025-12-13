//------------------------------------------------------------------------------
// URI.h
// URI class for handling file and web resource identifiers
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

/*  URI format
 *  From RFC 3986
 *  https://www.rfc-editor.org/rfc/rfc3986#section-3
 *
 *      foo://example.com:8042/over/there?name=ferret#nose
 *      \_/   \______________/\_________/ \_________/ \__/
 *       |           |            |            |        |
 *    scheme     authority       path        query   fragment
 *       |   _____________________|__
 *      / \ /                        \
 *      urn:example:animal:ferret:nose
 */

#pragma once
#include <filesystem>
#include <fmt/format.h>
#include <string>
#include <string_view>

class URI {
public:
    using ReflectionType = std::string;

    URI() = default;
    ~URI() = default;

    URI(const std::string& uri_str);

    // Constructor from components
    URI(std::string scheme, std::string authority, std::string path, std::string query = "",
        std::string fragment = "");

    /// Necessary for the serialization to work.
    ReflectionType reflection() const;

    /// Returns the underlying URI as a string.
    /// Note that this string is percent decoded.
    std::string str() const;

    static URI fromFile(const std::filesystem::path& file);

    static URI fromWeb(const std::string& url);

    /// Returns the path component, decoded and in platform format
    std::string_view getPath() const;

    bool operator==(URI const& other) const;

private:
    std::string_view scheme_;
    std::string_view authority_;
    std::string_view path_;
    std::string_view query_;
    std::string_view fragment_;

    mutable std::string decodedPath_;
    mutable std::string underlying_;

    /// Decode a percent-encoded string
    static std::string decode(const std::string& s);

    /// Encode a string into percent-encoding.
    /// This isn't used anywhere currently, but is provided for completeness.
    static std::string encode(const std::string& s);

    /// Converts a char to hexadecimal
    static std::string to_hex(unsigned char c);

    void init(std::string scheme, std::string authority, std::string path, std::string query,
              std::string fragment);
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
