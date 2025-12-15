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
    std::string authority = decode(m.get<4>().to_string());
    std::string path = decode(m.get<5>().to_string());
    std::string query = decode(m.get<7>().to_string());
    std::string fragment = decode(m.get<9>().to_string());

    init(std::move(scheme), std::move(authority), std::move(path), std::move(query),
         std::move(fragment));
}

std::string_view URI::scheme() const {
    return std::string_view(underlying_.data() + scheme_.first, scheme_.second);
}

std::string_view URI::authority() const {
    return std::string_view(underlying_.data() + authority_.first, authority_.second);
}

std::string_view URI::path() const {
    return std::string_view(underlying_.data() + path_.first, path_.second);
}

std::string_view URI::query() const {
    return std::string_view(underlying_.data() + query_.first, query_.second);
}

std::string_view URI::fragment() const {
    return std::string_view(underlying_.data() + fragment_.first, fragment_.second);
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

    std::size_t pos = 0;

    // scheme
    if (!scheme.empty()) {
        underlying_ += scheme;
        scheme_ = {pos, scheme.size()};
        pos += scheme.size();

        underlying_ += ':';
        ++pos;
    }
    else {
        scheme_ = {};
    }

    // authority
    if (!authority.empty() || scheme == "file") {
        underlying_ += "//";
        pos += 2;
    }

    if (!authority.empty()) {
        underlying_ += authority;
        authority_ = {pos, authority.size()};
        pos += authority.size();
    }
    else {
        authority_ = {};
    }

    // path
    underlying_ += path;
    path_ = {pos, path.size()};
    pos += path.size();

    // query
    if (!query.empty()) {
        underlying_ += '?';
        ++pos;

        underlying_ += query;
        query_ = {pos, query.size()};
        pos += query.size();
    }
    else {
        query_ = {};
    }

    // fragment
    if (!fragment.empty()) {
        underlying_ += '#';
        ++pos;

        underlying_ += fragment;
        fragment_ = {pos, fragment.size()};
        pos += fragment.size();
    }
    else {
        fragment_ = {};
    }
}

/// Necessary for the serialization to work.
URI::ReflectionType URI::reflection() const {
    return str();
}

/// Expresses the underlying URI as a string.
std::string URI::str() const {
    return underlying_;
}

// Constructor from components
URI::URI(std::string scheme, std::string authority, std::string path, std::string query,
         std::string fragment) {
    init(scheme, authority, path, query, fragment);
}

URI URI::fromFile(const std::filesystem::path& file) {
    if (file.empty())
        return URI();
    
    std::string path = file.generic_string();
    std::string authority = "";

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
    // URLs are valid URIs
    return URI("https://" + url);
}

std::string_view URI::getPath() const {
    // Use cached decoded path if available
    if (!fsPath_.empty())
        return fsPath_;

    fsPath_ = std::string(path());

#ifdef _WIN32
    // Convert drive letter: /c:/ -> C:/
    if (fsPath_.size() >= 3 && fsPath_[0] == '/' && std::isalpha(fsPath_[1]) &&
        fsPath_[2] == ':') {
        fsPath_[1] = static_cast<char>(std::toupper(fsPath_[1]));
        fsPath_.erase(0, 1); // remove leading slash
    }

    // Convert forward slashes to backslashes
    std::replace(fsPath_.begin(), fsPath_.end(), '/', '\\');

    // UNC path: file://server/share/file -> \\server\share\file
    if (!authority().empty() && scheme() == "file") {
        fsPath_ = "\\\\" + std::string(authority()) + fsPath_;
    }
#endif

    return fsPath_;
}

bool URI::operator==(URI const& other) const {
    return underlying_ == other.underlying_;
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
