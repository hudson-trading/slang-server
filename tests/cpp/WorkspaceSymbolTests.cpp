// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"
#include <set>
#include <string>

using namespace server;

static std::set<std::string> getSymbolNames(ServerHarness& server, const std::string& query) {
    auto result = server.getWorkspaceSymbol(lsp::WorkspaceSymbolParams{.query = query});
    auto symbols = rfl::get<std::vector<lsp::WorkspaceSymbol>>(result);
    std::set<std::string> names;
    for (const auto& sym : symbols) {
        names.insert(sym.name);
    }
    return names;
}

TEST_CASE("Workspace symbol - empty query returns all") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module Alpha;
    logic a;
endmodule

module Beta;
    logic b;
endmodule
)");
    doc.save();

    auto names = getSymbolNames(server, "");
    CHECK(names.count("Alpha") == 1);
    CHECK(names.count("Beta") == 1);

    doc.close();
}

TEST_CASE("Workspace symbol - exact match") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module Alpha;
    logic a;
endmodule

module Beta;
    logic b;
endmodule
)");
    doc.save();

    auto names = getSymbolNames(server, "Alpha");
    CHECK(names.count("Alpha") == 1);
    CHECK(names.count("Beta") == 0);

    doc.close();
}

TEST_CASE("Workspace symbol - case insensitive") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module TestModule;
    logic a;
endmodule
)");
    doc.save();

    auto names = getSymbolNames(server, "testmodule");
    CHECK(names.count("TestModule") == 1);

    names = getSymbolNames(server, "TESTMODULE");
    CHECK(names.count("TestModule") == 1);

    doc.close();
}

TEST_CASE("Workspace symbol - subsequence match") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module TestModule;
    logic a;
endmodule

module FooBar;
    logic b;
endmodule
)");
    doc.save();

    auto names = getSymbolNames(server, "tml");
    CHECK(names.count("TestModule") == 1);
    CHECK(names.count("FooBar") == 0);

    names = getSymbolNames(server, "fb");
    CHECK(names.count("FooBar") == 1);
    CHECK(names.count("TestModule") == 0);

    doc.close();
}

TEST_CASE("Workspace symbol - no match returns empty") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module Alpha;
    logic a;
endmodule
)");
    doc.save();

    auto names = getSymbolNames(server, "xyz");
    CHECK(names.empty());

    doc.close();
}

TEST_CASE("Workspace symbol - wrong order does not match") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module TestModule;
    logic a;
endmodule
)");
    doc.save();

    auto names = getSymbolNames(server, "lmt");
    CHECK(names.count("TestModule") == 0);

    doc.close();
}
