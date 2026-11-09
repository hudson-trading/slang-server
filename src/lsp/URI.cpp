#include "lsp/URI.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <uriparser/Uri.h>

#include "slang/util/ScopeGuard.h"

namespace {

void checkUri(int result) {
    if (result == URI_ERROR_MALLOC)
        throw std::bad_alloc();
    if (result == URI_ERROR_OUTPUT_TOO_LARGE)
        throw std::length_error("URI is too long");
    if (result != URI_SUCCESS)
        throw std::invalid_argument("Invalid URI");
}

} // namespace

URI::URI(const std::string& input) {
    UriUriA parsed{};
    slang::ScopeGuard cleanup([&] { uriFreeUriMembersA(&parsed); });
    checkUri(uriParseSingleUriExA(&parsed, input.data(), input.data() + input.size(), nullptr));
    checkUri(uriNormalizeSyntaxExA(&parsed, URI_NORMALIZE_SCHEME | URI_NORMALIZE_HOST));

    if (parsed.absolutePath || (parsed.hostText.first && parsed.pathHead))
        fsPath_ += '/';
    for (auto* segment = parsed.pathHead; segment; segment = segment->next) {
        if (segment->text.first != segment->text.afterLast)
            fsPath_.append(segment->text.first, segment->text.afterLast);
        if (segment->next)
            fsPath_ += '/';
    }
    fsPath_.resize(uriUnescapeInPlaceA(fsPath_.data()) - fsPath_.data());

    const bool isFile = parsed.scheme.first &&
                        std::string_view(parsed.scheme.first,
                                         parsed.scheme.afterLast - parsed.scheme.first) == "file";
    if (isFile) {
        if (fsPath_.find('\0') != std::string::npos)
            throw std::invalid_argument("File URI contains a null byte");

#ifdef _WIN32
        const size_t drive = fsPath_.starts_with('/') ? 1 : 0;
        if (fsPath_.size() > drive + 1 && fsPath_[drive + 1] == ':' && fsPath_[drive] >= 'a' &&
            fsPath_[drive] <= 'z') {
            fsPath_[drive] -= 'a' - 'A';
        }
#endif

        // Canonicalize escapes without normalizing dot segments: resolving ".." across a
        // symlink can change which file is opened.
        std::string encodedPath(3 * fsPath_.size() + 8, '\0');
        checkUri(uriUnixFilenameToUriStringA(fsPath_.c_str(), encodedPath.data()));
        encodedPath.resize(std::char_traits<char>::length(encodedPath.c_str()));
        if (fsPath_.starts_with('/')) {
            encodedPath.erase(0, 7); // The filename helper prepends "file://".
            if (!parsed.hostText.first)
                checkUri(uriSetHostRegNameA(&parsed, encodedPath.data(), encodedPath.data()));
        }
        if (!encodedPath.empty())
            checkUri(
                uriSetPathA(&parsed, encodedPath.data(), encodedPath.data() + encodedPath.size()));
    }

    int length = 0;
    // uriparser checks length overflow; its count excludes the null terminator.
    checkUri(uriToStringCharsRequiredA(&parsed, &length));
    if (length == std::numeric_limits<int>::max())
        throw std::length_error("URI is too long");
    underlying_.resize(static_cast<size_t>(length) + 1);
    checkUri(uriToStringA(underlying_.data(), &parsed, length + 1, nullptr));
    underlying_.resize(length);

#ifdef _WIN32
    if (isFile) {
        // Filename conversion consumes the whole string, so omit query and fragment.
        const auto end = underlying_.find_first_of("?#");
        const auto fileUri = underlying_.substr(0, end);
        fsPath_.resize(fileUri.size() + 1);
        checkUri(uriUriStringToWindowsFilenameA(fileUri.c_str(), fsPath_.data()));
        fsPath_.resize(std::char_traits<char>::length(fsPath_.c_str()));
    }
#endif
}

URI URI::fromFile(const std::filesystem::path& file) {
    if (file.empty())
        return URI();

#ifdef _WIN32
    auto path = file;
    path.make_preferred();
    // MinGW's absolute() can replace a UNC root with the current drive.
    if (!path.native().starts_with(L"\\\\"))
        path = std::filesystem::absolute(path);
#else
    auto path = std::filesystem::absolute(file);
#endif
    const auto utf8 = path.u8string();
    const std::string filename(utf8.begin(), utf8.end());
    if (filename.find('\0') != std::string::npos)
        throw std::invalid_argument("Filename contains a null byte");

    std::string encoded(3 * filename.size() + 9, '\0');
#ifdef _WIN32
    checkUri(uriWindowsFilenameToUriStringA(filename.c_str(), encoded.data()));
#else
    checkUri(uriUnixFilenameToUriStringA(filename.c_str(), encoded.data()));
#endif
    encoded.resize(std::char_traits<char>::length(encoded.c_str()));
    return URI(encoded);
}
