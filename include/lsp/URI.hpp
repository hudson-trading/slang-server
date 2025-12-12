#pragma once

// This gives a good overview of how URI works:
// https://github.com/microsoft/vscode-uri/blob/48d2d95cf1f793b66f349401050c48b8fc73637e/src/uri.ts#L81-L96

/** The following diagram comes from the vscode-uri project
 *    Copyright (c) Microsoft Corporation.
 *    Licensed under the MIT License.
 *    https://github.com/microsoft/vscode-uri
 *
 * Uniform Resource Identifier (URI) http://tools.ietf.org/html/rfc3986.
 * This class is a simple parser which creates the basic component parts
 * (http://tools.ietf.org/html/rfc3986#section-3) with minimal validation
 * and encoding.
 *
 * ```txt
 *       foo://example.com:8042/over/there?name=ferret#nose
 *       \_/   \______________/\_________/ \_________/ \__/
 *        |           |            |            |        |
 *     scheme     authority       path        query   fragment
 *        |   _____________________|__
 *       / \ /                        \
 *       urn:example:animal:ferret:nose
 * ```
 */

#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

class URI {
public:
    using ReflectionType = std::string;

    URI() = default;
    ~URI() = default;

    URI(const std::string& uri_str) { parse(uri_str); }

    /// Necessary for the serialization to work.
    ReflectionType reflection() const { return str(); }

    /// Expresses the underlying URI as a string.
    std::string str() const {
        // Return cached value if we already found it
        if (!underlying_.empty()) {
            return underlying_;
        }

        underlying_.reserve(scheme.size() + 3 + authority.size() + path.size() +
                            (query.empty() ? 0 : 1 + query.size()) +
                            (fragment.empty() ? 0 : 1 + fragment.size()));

        underlying_ += scheme;
        underlying_ += "://";
        underlying_ += authority;
        underlying_ += encode(path);

        if (!query.empty()) {
            underlying_ += '?';
            underlying_ += query;
        }
        if (!fragment.empty()) {
            underlying_ += '#';
            underlying_ += fragment;
        }

        return underlying_;
    }

    // Constructor from components
    URI(std::string scheme, std::string authority, std::string path, std::string query = "",
        std::string fragment = "") :
        scheme(std::move(scheme)), authority(std::move(authority)), path(std::move(path)),
        query(std::move(query)), fragment(std::move(fragment)) {}

    static URI fromFile(const std::filesystem::path& file) {
        std::string p = file.generic_string();
        std::string encoded = encode(p);
        if (encoded[0] != '/')
            encoded = "/" + encoded;
        return URI("file", "", std::move(encoded));
    }

    static URI fromWeb(const std::string& url) {
        // encode not needed since URLs are a valid URIs
        return URI(url);
    }

    std::string_view getPath() const {
        if (!decodedPath_.empty())
            return decodedPath_;

        decodedPath_ = to_path(decode(path));
        return decodedPath_;
    }

    // Equality operator (needed for unordered_map)
    bool operator==(const URI& other) const { return underlying_ == other.underlying_; }

private:
    std::string scheme;
    std::string authority;
    std::string path;
    std::string query;
    std::string fragment;

    mutable std::string decodedPath_;
    mutable std::string underlying_;

    void parse(const std::string& uri_str) {
        size_t pos = 0;

        // Parse scheme
        size_t scheme_end = uri_str.find("://", pos);
        if (scheme_end != std::string::npos) {
            scheme = uri_str.substr(pos, scheme_end);
            pos = scheme_end + 3; // Skip "://"
        }
        else {
            throw std::invalid_argument("Invalid URI: Missing scheme.");
        }

        // Parse authority
        size_t authority_end = uri_str.find_first_of("/?#", pos);
        if (authority_end != std::string::npos) {
            authority = uri_str.substr(pos, authority_end - pos);
            pos = authority_end;
        }
        else {
            // If no path, it's the entire URI
            authority = uri_str.substr(pos);
            return;
        }

        // Parse path
        if (uri_str[pos] == '/') {
            size_t path_end = uri_str.find_first_of("?#", pos);
            if (path_end != std::string::npos) {
                path = uri_str.substr(pos, path_end - pos);
                pos = path_end;
            }
            else {
                path = uri_str.substr(pos);
                return; // End parsing, no further components
            }
        }

        // Parse query
        if (pos < uri_str.size() && uri_str[pos] == '?') {
            size_t query_end = uri_str.find('#', pos);
            query = uri_str.substr(pos + 1, query_end - pos - 1);
            pos = query_end != std::string::npos ? query_end : uri_str.size();
        }

        // Parse fragment
        if (pos < uri_str.size() && uri_str[pos] == '#') {
            fragment = uri_str.substr(pos + 1);
        }
    }

    // Decode a percent-encoded string
    static std::string decode(const std::string& s) {
        std::string result;
        for (size_t i = 0; i < s.length(); ++i) {
            if (s[i] == '%' && i + 2 < s.length()) {
                int val = std::stoi(s.substr(i + 1, 2), nullptr, 16);
                result += static_cast<char>(val);
                i += 2; // Skip XX in %XX
            }
            else {
                result += s[i];
            }
        }
        return result;
    }

    static std::string encode(const std::string& s) {
        std::string result;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/') {
                result += static_cast<char>(c);
            }
            else {
                result += '%';
                result += to_hex(c);
            }
        }
        return result;
    }

    /// Converts a char to hexadecimal
    static std::string to_hex(unsigned char c) {
        static const char* hex_chars = "0123456789ABCDEF";
        std::string hex;
        hex += hex_chars[(c >> 4) & 0xF];
        hex += hex_chars[c & 0xF];
        return hex;
    }

    std::string to_path(std::string decodedPath) const {
        std::string ret;
        // Check for drive letter
        if (decodedPath.size() >= 3 && decodedPath[0] == '/' &&
            std::isalpha(static_cast<unsigned char>(decodedPath[1])) && decodedPath[2] == ':') {
            char drive = static_cast<char>(std::tolower(decodedPath[1]));
            ret = drive + decodedPath.substr(2);
        }
        else {
            ret = decodedPath;
        }

#ifdef _WIN32
        // Convert forward slashes to backslashes
        std::replace(ret.begin(), ret.end(), '/', '\\');
#endif

        return ret;
    }
};
