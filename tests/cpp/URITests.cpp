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

TEST_CASE("URI Reflection Percent Encodes Components") {
    SECTION("Path") {
        URI u("file:///with%20space/%E4%B8%AD%E6%96%87/f%3F.sv");

        CHECK(u.reflection() == "file:///with%20space/%E4%B8%AD%E6%96%87/f%3F.sv");
    }

    SECTION("Authority, query, and fragment") {
        URI u("https://user%20name@example.com/a%23b?q=hello%20world%23value#part%20one%23two");

        CHECK(u.reflection() ==
              "https://user%20name@example.com/a%23b?q=hello%20world%23value#part%20one%23two");
    }

    SECTION("Reserved delimiters remain intact") {
        URI u("https://example.com:8042/a;b/c?x=1&next=/d?y=2#section/1?mode=full");

        CHECK(u.reflection() ==
              "https://example.com:8042/a;b/c?x=1&next=/d?y=2#section/1?mode=full");
    }
}

TEST_CASE("URI EmptyInput") {
    URI u("");

    // TODO: Right now it returns "/" as the URI string.
    // It should return ""
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
