#include "lsp/URI.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("URI: Decode") {

    URI u("file:///x/%41%42%43/%20y.z");

#ifndef _WIN32
    CHECK(u.getPath() == "/x/ABC/ y.z");
#else
    CHECK(u.getPath() == R"(\x\ABC\ y.z)");
#endif
}

TEST_CASE("URI: GetPathWithEmptyInput") {
    URI u("");

#ifdef _WIN32
    CHECK(u.getPath() == "\\");
#else
    CHECK(u.getPath() == "/");
#endif
}

#ifdef _WIN32

TEST_CASE("URI: WindowsDriveLetterNormalization") {
    URI u("file:///c:/temp/file.txt");

    CHECK(u.getPath() == "C:\\temp\\file.txt");
}

TEST_CASE("URI: WindowsUNCPath") {
    URI u("file://server/share/file.txt");

    CHECK(u.getPath() == R"(\\server\share\file.txt)");
}

#endif
