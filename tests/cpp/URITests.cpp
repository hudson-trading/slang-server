#include "lsp/LspTypes.h"
#include "lsp/URI.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <rfl/json.hpp>
#include <stdexcept>
#include <unordered_set>

TEST_CASE("URI Decode") {
#ifndef _WIN32
    URI u("file:///x/%41%42%43/%20y.z");
    CHECK(u.getPath() == "/x/ABC/ y.z");
#else
    URI u("file:///C:/x/%41%42%43/%20y.z");
    CHECK(u.getPath() == R"(C:\x\ABC\ y.z)");
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

    CHECK(u.str().empty());
    CHECK(u.getPath().empty());
    CHECK(u == URI{});
    CHECK(u == URI::fromFile(""));
}

TEST_CASE("URI Long Watched File Notification") {
    const size_t length = GENERATE(256, 1024, 4096, 65536);
    const bool directories = GENERATE(false, true);
    std::string path(length, 'a');
    if (directories) {
        for (size_t i = 1; i < path.size(); i += 2)
            path[i] = '/';
    }
    path += ".sv";
#ifdef _WIN32
    const std::string input = "file:///C%3A/" + path;
    auto expectedPath = std::filesystem::path("C:/" + path);
#else
    const std::string input = "file:///" + path;
    auto expectedPath = std::filesystem::path("/" + path);
#endif
    expectedPath.make_preferred();
    CAPTURE(length, directories);

    const auto params = rfl::json::read<lsp::DidChangeWatchedFilesParams, rfl::UnderlyingEnums>(
        R"({"changes":[{"uri":")" + input + R"(","type":1}]})");
    REQUIRE(params);
    REQUIRE(params->changes.size() == 1);
    CHECK(params->changes[0].uri.str() == input);
    CHECK(params->changes[0].uri.getPath() == expectedPath.string());
    CHECK(params->changes[0].type == lsp::FileChangeType::Created);
}

TEST_CASE("URI File JSON Round Trip") {
    const std::string_view filename = GENERATE("with space.sv", "literal%23#.sv", "a+b;@=!.sv",
                                               "\xE4\xB8\xAD\xE6\x96\x87.sv");
    auto path = std::filesystem::current_path() /
                std::filesystem::path(std::u8string(filename.begin(), filename.end()));
    path.make_preferred();
    const auto uri = URI::fromFile(path);
    const auto utf8 = path.u8string();

    CHECK(uri.getPath() == std::string(utf8.begin(), utf8.end()));
    CHECK(uri.str() == uri.reflection());
    CHECK(uri.str().find(' ') == std::string::npos);
    CHECK(uri.str().find('#') == std::string::npos);
    const auto roundTrip = rfl::json::read<URI>(rfl::json::write(uri));
    REQUIRE(roundTrip);
    CHECK(*roundTrip == uri);
    CHECK(roundTrip->getPath() == uri.getPath());
    CHECK(URI(uri.str()) == uri);
}

TEST_CASE("URI File Identity") {
    const URI uri("file:///a%41%2bb%3bc.sv");
    const URI same("file:/aA+b;c.sv");
    CHECK(uri.str() == "file:///aA%2Bb%3Bc.sv");
    CHECK(uri == same);
    CHECK(std::unordered_set<URI>{uri}.contains(same));
    CHECK(URI::fromFile("a+b.sv") == URI::fromFile(std::filesystem::absolute("a+b.sv")));
    CHECK(URI("file:///a%23b.sv") != URI("file:///a#b.sv"));
    CHECK(URI("file:///a%3Fb.sv") != URI("file:///a?b.sv"));
}

TEST_CASE("URI File Path Excludes Query And Fragment") {
#ifdef _WIN32
    const URI uri("file:///C:/dir/link/../a%23b.sv?view=1#position");
    CHECK(uri.str() == "file:///C%3A/dir/link/../a%23b.sv?view=1#position");
    CHECK(uri.getPath() == R"(C:\dir\link\..\a#b.sv)");
#else
    const URI uri("file:///dir/link/../a%23b%3F.sv?view=1#position");
    CHECK(uri.str() == "file:///dir/link/../a%23b%3F.sv?view=1#position");
    CHECK(uri.getPath() == "/dir/link/../a#b?.sv");
#endif
}

TEST_CASE("URI Preserves Nonfile Components") {
    const auto input = GENERATE("urn:example:animal:ferret:nose", "../a%20b?x=1#part",
                                "https://example.com/?#",
                                "https://[0000:0000:0000:0000:0000:0000:0000:0001]:8042/a%2Fb",
                                "https://user%40name@example.com/a?x=a%26b%3Dc");
    const URI uri(input);
    CHECK(uri.str() == input);
    CHECK(URI(uri.str()) == uri);
}

TEST_CASE("URI IPv6 Host") {
    const URI uri("https://[::1]:8042/a%2Fb");
    CHECK(uri == URI("https://[0000:0000:0000:0000:0000:0000:0000:0001]:8042/a%2Fb"));
    CHECK(uri != URI("https://[::1]:8042/a/b"));
    CHECK(URI(uri.str()) == uri);
}

TEST_CASE("URI Nonfile Paths Preserve Slashes") {
    const auto scheme = GENERATE("http", "https", "untitled");
    const std::string input = std::string(scheme) + "://example.com/a%20b/c?query=/d#fragment/e";
    const URI uri(input);

    CHECK(uri.str() == input);
    CHECK(uri.getPath() == "/a b/c");
    CHECK(URI("../a%20b/c?query=/d#fragment/e").getPath() == "../a b/c");
}

TEST_CASE("URI Rejects Malformed Input") {
    const auto input = GENERATE("file:///bad%", "file:///bad%2", "file:///bad%GG",
                                "file:///with space.sv", "https://[invalid]/a",
                                "file:///null%00byte.sv");
    CHECK_THROWS_AS(URI(input), std::invalid_argument);
    CHECK_FALSE(rfl::json::read<URI>(rfl::json::write(std::string(input))));
}

TEST_CASE("URI Copy And Move") {
    auto source = URI::fromFile("a %23#.sv");
    auto copied = source;
    auto moved = std::move(source);
    source = URI{};
    CHECK(copied == moved);
    CHECK(copied.getPath() == moved.getPath());
    CHECK(URI(copied.str()) == moved);
}

#ifdef _WIN32

TEST_CASE("URI WindowsDriveLetter") {
    SECTION("HexDecoded") {
        URI u("file:///c:/temp/file.txt");

        CHECK(u.str() == "file:///C%3A/temp/file.txt");
        CHECK(u.getPath() == "C:\\temp\\file.txt");
    }

    SECTION("HexEncoded") {
        URI u("file:///c%3A/temp/file.txt");

        CHECK(u.str() == "file:///C%3A/temp/file.txt");
        CHECK(u.getPath() == "C:\\temp\\file.txt");
    }

    SECTION("FromFile") {
        const auto path = GENERATE(R"(c:\temp\file.txt)", "c:/temp/file.txt");
        CAPTURE(path);
        URI u = URI::fromFile(path);

        CHECK(u.str() == "file:///C%3A/temp/file.txt");
        CHECK(u.getPath() == R"(C:\temp\file.txt)");
    }
}

TEST_CASE("URI WindowsUNCPath") {
    SECTION("UNC Basic") {
        URI u("file://server/share/file.txt");

        CHECK(u.str() == "file://server/share/file.txt");
        CHECK(u.getPath() == R"(\\server\share\file.txt)");
    }

    SECTION("UNC FromFile") {
        const auto path = GENERATE(R"(\\server\share\file.txt)", "//server/share/file.txt",
                                   R"(\\server/share\file.txt)");
        CAPTURE(path);
        URI u = URI::fromFile(path);

        CHECK(u.str() == "file://server/share/file.txt");
        CHECK(u.getPath() == R"(\\server\share\file.txt)");
    }

    SECTION("UNC Reserved Characters") {
        const auto path = std::filesystem::path(R"(\\server\share\a %23#.sv)");
        const auto uri = URI::fromFile(path);
        CHECK(uri.str() == "file://server/share/a%20%2523%23.sv");
        CHECK(uri.getPath() == path.string());
        CHECK(URI(uri.str()) == uri);
    }

    SECTION("UNC Share Root") {
        const auto path = GENERATE(R"(\\server\share\)", "//server/share/");
        CAPTURE(path);
        URI u = URI::fromFile(path);

        CHECK(u.str() == "file://server/share/");
        CHECK(u.getPath() == R"(\\server\share\)");
    }
}

#endif
