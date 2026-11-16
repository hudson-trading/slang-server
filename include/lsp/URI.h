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
#include <string_view>

/// An encoded URI with a decoded path for filesystem access.
class URI {
public:
    /// JSON represents a URI as an encoded string.
    using ReflectionType = std::string;

    /// Constructs an empty identifier.
    URI() = default;

    /// Parses an encoded URI; malformed input throws std::invalid_argument.
    URI(const std::string& input);

    /// Returns the encoded URI for JSON serialization.
    ReflectionType reflection() const { return underlying_; }

    /// Returns the encoded URI, suitable for protocol strings and document keys.
    const std::string& str() const { return underlying_; }

    /// Converts a filesystem path to an absolute file URI; an empty path stays empty.
    static URI fromFile(const std::filesystem::path& file);

    /// Returns the decoded path, excluding query and fragment; file URIs use platform format.
    std::string_view getPath() const { return fsPath_; }

    /// Compares serialized identifiers, including any query and fragment.
    bool operator==(const URI& other) const { return underlying_ == other.underlying_; }

private:
    /// Encoded URI; file paths use a consistent encoding for document identity.
    std::string underlying_;

    /// Decoded path; only file URIs use platform separators.
    std::string fsPath_;
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
