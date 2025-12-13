#include "lsp/URI.h"

#include <cctype>
#include <ctre.hpp>
#include <filesystem>
#include <string>
#include <utility>

// Pattern from RFC 3986
// https://www.rfc-editor.org/rfc/rfc3986#appendix-B
static constexpr auto uriPattern = ctll::fixed_string{
    R"(^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?$)"};

URI::URI(const std::string& input) {

    const auto m = ctre::match<uriPattern>(input);
    if (!m) {
        return; // empty URI
    }

    std::string scheme = m.get<2>().to_string();
    std::string authority = m.get<4>().to_string();
    std::string path = decode(m.get<5>().to_string());
    std::string query = decode(m.get<7>().to_string());
    std::string fragment = m.get<9>().to_string();

    init(std::move(scheme), std::move(authority), std::move(path), std::move(query),
         std::move(fragment));
}

URI::URI(std::string scheme, std::string authority, std::string path, std::string query,
         std::string fragment) {

    init(std::move(scheme), std::move(authority), std::move(path), std::move(query),
         std::move(fragment));
}

void URI::init(std::string scheme, std::string authority, std::string path, std::string query,
               std::string fragment) {

    // Ensure path has leading slash (which will be the end of the authority)
    if (path.empty())
        path = "/";
    else if (path[0] != '/')
        path = "/" + path;

#ifdef _WIN32
    // Normalize Windows drive letter to uppercase
    if (path.size() >= 3 && path[0] == '/' && std::isalpha(path[1]) && path[2] == ':') {
        path[1] = static_cast<char>(std::toupper(path[1]));
    }
#endif

    underlying_.reserve(scheme.size() + authority.size() + path.size() + query.size() +
                        fragment.size() + 10);

    std::size_t posScheme = std::string::npos;
    std::size_t posAuthority = std::string::npos;
    std::size_t posPath = std::string::npos;
    std::size_t posQuery = std::string::npos;
    std::size_t posFragment = std::string::npos;

    if (!scheme.empty()) {
        posScheme = underlying_.size();
        underlying_ += scheme;
        underlying_ += ':';
    }

    if (!authority.empty() || scheme == "file") {
        underlying_ += "//";
        posAuthority = underlying_.size();
        underlying_ += authority;
    }

    posPath = underlying_.size();
    underlying_ += path;

    if (!query.empty()) {
        underlying_ += '?';
        posQuery = underlying_.size();
        underlying_ += query;
    }

    if (!fragment.empty()) {
        underlying_ += '#';
        posFragment = underlying_.size();
        underlying_ += fragment;
    }

    scheme_ = (posScheme == std::string::npos)
                  ? std::string_view{}
                  : std::string_view(underlying_).substr(posScheme, scheme.size());

    authority_ = (posAuthority == std::string::npos)
                     ? std::string_view{}
                     : std::string_view(underlying_).substr(posAuthority, authority.size());

    path_ = std::string_view(underlying_).substr(posPath, path.size());

    query_ = (posQuery == std::string::npos)
                 ? std::string_view{}
                 : std::string_view(underlying_).substr(posQuery, query.size());

    fragment_ = (posFragment == std::string::npos)
                    ? std::string_view{}
                    : std::string_view(underlying_).substr(posFragment, fragment.size());
}

/// Necessary for the serialization to work.
URI::ReflectionType URI::reflection() const {
    return str();
}

/// Expresses the underlying URI as a string.
std::string URI::str() const {
    return underlying_;
}

URI URI::fromFile(const std::filesystem::path& file) {
    std::string path = file.generic_string();
    std::string authority;

#ifdef _WIN32
    // UNC: \\server\path\to\file.txt
    // URI: file://server/path/to/file.txt
    //   Authority: 'server'
    //   Path: path/to/file.txt'
    if (path.size() >= 2 && path[0] == '/' && path[1] == '/') {
        const std::size_t idx = path.find('/', 2);
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
    return URI("https", "", "/" + url, "", "");
}

std::string_view URI::getPath() const {
    // use cached decoded path if available
    if (!decodedPath_.empty())
        return decodedPath_;

    decodedPath_ = std::string(path_);

#ifdef _WIN32
    // Convert drive letter: /c:/ -> C:/
    if (decodedPath_.size() >= 3 && decodedPath_[0] == '/' && std::isalpha(decodedPath_[1]) &&
        decodedPath_[2] == ':') {
        decodedPath_[1] = static_cast<char>(std::toupper(decodedPath_[1]));
        decodedPath_.erase(0, 1); // remove leading slash
    }

    // Convert forward slashes to backslashes
    std::replace(decodedPath_.begin(), decodedPath_.end(), '/', '\\');

    // UNC path: file://server/share/file -> \\server\share\file
    if (!authority_.empty() && scheme_ == "file") {
        decodedPath_ = "\\\\" + std::string(authority_) + decodedPath_;
    }
#endif

    return decodedPath_;
}

bool URI::operator==(URI const& other) const {
    return scheme_ == other.scheme_ && authority_ == other.authority_ && path_ == other.path_ &&
           query_ == other.query_ && fragment_ == other.fragment_;
}

std::string URI::decode(const std::string& s) {
    std::string result;
    for (std::size_t i = 0; i < s.length(); ++i) {
        if (s[i] == '%' && i + 2 < s.length()) {
            const int val = std::stoi(s.substr(i + 1, 2), nullptr, 16);
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

std::string URI::to_hex(unsigned char c) {
    static const char* hex_chars = "0123456789ABCDEF";
    std::string hex;
    hex += hex_chars[(c >> 4) & 0xF];
    hex += hex_chars[c & 0xF];
    return hex;
}
