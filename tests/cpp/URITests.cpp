#include "lsp/URI.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("URI Decode") {

    URI u("file:///x/%41%42%43/%20y.z");

#ifndef _WIN32
    CHECK(u.getPath() == "/x/ABC/ y.z");
#else
    CHECK(u.getPath() == R"(\x\ABC\ y.z)");
#endif
}

TEST_CASE("URI EmptyInput") {
    URI u("");

    // TODO: Right now it returns "/" as the URI string.
    // It should return "/"
    // CHECK(u.str() == "");

#ifdef _WIN32
    CHECK(u.getPath() == "\\");
#else
    CHECK(u.getPath() == "/");
#endif
}

#ifdef _WIN32

TEST_CASE("URI WindowsDriveLetter") {
    SECTION("HexDecoded") {
        URI u("file:///c:/temp/file.txt");

        CHECK(u.str() == "file:///C:/temp/file.txt");
        CHECK(u.getPath() == "C:\\temp\\file.txt");
    }

    SECTION("HexEncoded") {
        URI u("file:///c%3A/temp/file.txt");

        CHECK(u.str() == "file:///C:/temp/file.txt");
        CHECK(u.getPath() == "C:\\temp\\file.txt");
    }
}

TEST_CASE("URI WindowsUNCPath") {
    SECTION("UNC Basic") {
        URI u("file://server/share/file.txt");

        CHECK(u.str() == "file://server/share/file.txt");
        CHECK(u.getPath() == R"(\\server\share\file.txt)");
    }

    SECTION("UNC FromFile") {
        std::filesystem::path p(R"(\\server\share\file.txt)");
        URI u = URI::fromFile(p);

        CHECK(u.str() == "file://server/share/file.txt");
        CHECK(u.getPath() == R"(\\server\share\file.txt)");
    }
}

#endif
