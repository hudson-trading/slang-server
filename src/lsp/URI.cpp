#include "lsp/URI.h"

#include <cctype>
#include <ctre.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <string>
#include <utility>

// From RFC 3986
static constexpr auto uriPattern = ctll::fixed_string{
    R"(^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?$)"};

URI::URI(const std::string& input) {
    auto m = ctre::match<uriPattern>(input);
    if (!m) {
        return; // empty URI
    }

    scheme = m.get<2>().to_string();
    authority = decode(m.get<4>().to_string());
    path = decode(m.get<5>().to_string());

    // Normalize Windows drive letter to uppercase
    if (path.size() >= 3 && path[0] == '/' && std::isalpha(path[1]) && path[2] == ':') {
        path[1] = std::toupper(path[1]);
    }

    query = decode(m.get<7>().to_string());
    fragment = decode(m.get<9>().to_string());

    // ensure path has leading slash
    if (path.empty())
        path = "/";
    else if (path[0] != '/')
        path = "/" + path;
}

/// Necessary for the serialization to work.
URI::ReflectionType URI::reflection() const {
    return str();
}

/// Expresses the underlying URI as a string.
std::string URI::str() const {
    // Return cached value if we already found it
    if (!underlying_.empty()) {
        return underlying_;
    }

    std::ostringstream out;

    if (!scheme.empty()) {
        out << scheme << ":";
    }

    if (!authority.empty() || scheme == "file") {
        out << "//";
    }

    if (!authority.empty())
        out << authority;

    // lowercase drive letter
    std::string normalizedPath = path;
    if (normalizedPath.size() >= 3 && normalizedPath[0] == '/' && normalizedPath[2] == ':' &&
        std::isupper(normalizedPath[1])) {
        normalizedPath[1] = std::toupper(normalizedPath[1]);
    }

    out << normalizedPath; // encode(path);

    if (!query.empty()) {
        out << "?";
        out << encode(query);
    }

    if (!fragment.empty()) {
        out << "#";
        out << fragment;
    }

    underlying_ = out.str();
    return underlying_;
}

// Constructor from components
URI::URI(std::string scheme, std::string authority, std::string path, std::string query,
         std::string fragment) :
    scheme(std::move(scheme)), authority(std::move(authority)), path(std::move(path)),
    query(std::move(query)), fragment(std::move(fragment)) {
}

URI URI::fromFile(const std::filesystem::path& file) {
    std::string path = file.generic_string();
    std::string authority = "";

#ifdef _WIN32
    if (path[0] != '/')
        path = "/" + path;

    if (path.size() >= 3 && path[0] == '/' && std::isalpha(path[1]) && path[2] == ':') {
        path[1] = std::toupper(path[1]);
    }

    // UNC path: //server/share/file
    if (path.size() >= 2 && path[0] == '/' && path[1] == '/') {
        size_t idx = path.find('/', 2);
        if (idx == std::string::npos) {
            authority = path.substr(2);
            path = "/";
        }
        else {
            authority = path.substr(2, idx - 2);
            path = path.substr(idx);
            if (path.empty())
                path = "/";
        }
    }
#endif

    return URI("file", authority, path, "", "");
}

URI URI::fromWeb(const std::string& url) {
    // encode not needed since URLs are valid URIs
    return URI("https://" + url);
}

std::string_view URI::getPath() const {
    if (!decodedPath_.empty())
        return decodedPath_;

    std::string p = decode(path);

#ifdef _WIN32
    // Convert drive letter: /c:/ -> C:/
    if (p.size() >= 3 && p[0] == '/' && std::isalpha(p[1]) && p[2] == ':') {
        p[1] = std::toupper(p[1]);
        p.erase(0, 1); // remove leading slash
    }

    std::replace(p.begin(), p.end(), '/', '\\');

    if (!authority.empty() && scheme == "file") {
        return "\\\\" + authority + p;
    }

#endif

    decodedPath_ = p;
    return decodedPath_;
}

// Equality operator (needed for unordered_map)
bool URI::operator==(URI const& other) const {
    return scheme == other.scheme && authority == other.authority && path == other.path &&
           query == other.query && fragment == other.fragment;
}

// Decode a percent-encoded string
std::string URI::decode(const std::string& s) {
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

std::string URI::encode(const std::string& s) {
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
std::string URI::to_hex(unsigned char c) {
    static const char* hex_chars = "0123456789ABCDEF";
    std::string hex;
    hex += hex_chars[(c >> 4) & 0xF];
    hex += hex_chars[c & 0xF];
    return hex;
}