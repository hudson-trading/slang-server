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

    ~URI() = default;

    // Equality operator (needed for unordered_map)
    bool operator==(const URI& other) const { return underlying_ == other.underlying_; }

    /// Necessary for the serialization to work.
    ReflectionType reflection() const { return underlying_; }

    /// Expresses the underlying URI as a string.
    std::string str() const { return reflection(); }

    std::string_view getPath() const {
        constexpr std::string_view prefix = "file://";
        if (!underlying_.starts_with(prefix)) {
            return underlying_;
        }

        std::string_view raw = std::string_view(underlying_).substr(prefix.size());

        decodedPath_ = decodePercentEncoding(raw);

#ifdef _WIN32
        normalizeDriveLetter(decodedPath_);
#endif

        return std::string_view(decodedPath_);
    }

    static URI fromFile(const std::filesystem::path& file) {
        std::string path = file.generic_string();
        std::string decoded = decodePercentEncoding(path);

#ifdef _WIN32
        normalizeDriveLetter(decoded);
#endif

        return URI("file", decoded);
    }

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

    /// this is created so that getPath() can return a `string_view`
    mutable std::string decodedPath_;

    static std::string decodePercentEncoding(std::string_view s) {
        std::string out;
        out.reserve(s.size());

        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '%' && i + 2 < s.size()) {
                char hex[3] = {(char)s[i + 1], (char)s[i + 2], 0};
                char* end = nullptr;
                long val = std::strtol(hex, &end, 16);
                if (end != hex + 2) {
                    out.push_back('%');
                }
                else {
                    out.push_back((char)val);
                    i += 2;
                }
            }
            else {
                out.push_back(s[i]);
            }
        }
        return out;
    }

#ifdef _WIN32
    /// Normalize Windows drive letter (C:, D:, etc, etc)
    static inline void normalizeDriveLetter(std::string& path) {
        size_t i = 0;

        if (path.size() >= 3 && path[1] == ':') {
            i = 0;
        }

        else if (path.size() >= 4 && path[0] == '/' && path[2] == ':') {
            i = 1;
        }

        else {
            return; // no drive letter pattern found
        }

        // Uppercase if necessary
        char& ch = path[i];
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
    }
#endif
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
