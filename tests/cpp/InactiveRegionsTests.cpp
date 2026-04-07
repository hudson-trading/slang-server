// AI SLOP

#include "document/SyntaxIndexer.h"
#include "utils/ServerHarness.h"
#include <catch2/catch_test_macros.hpp>

#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

TEST_CASE("InactiveRegions_SyntaxIndexer") {
    using namespace slang;

    SourceManager sm;

    Bag options;

    auto tree = syntax::SyntaxTree::fromText(R"(
`ifdef FOO
logic a;
`else
logic b;
`endif

`undefineall

`define FOO

`ifdef FOO
logic c;
`else
logic d;
`endif

`undefineall

`define BAR
`ifdef FOO
logic e;
`elsif BAR
logic f;
`else
logic g;
`endif
logic h;
)",
                                             sm, "test", "", options);

    server::SyntaxIndexer indexer(*tree);
    auto& disabled = indexer.disabledRegions;

    REQUIRE(disabled.size() >= 1);

    JsonGoldenTest golden;

    std::vector<std::string> texts;
    texts.reserve(disabled.size());
    for (auto& r : disabled) {
        texts.push_back(std::string(sm.getText(r)));
    }

    golden.record(texts);
}

TEST_CASE("InactiveRegions_ParseError") {
    using namespace slang;

    SourceManager sm;
    Bag options;

    auto tree = syntax::SyntaxTree::fromText(R"(
`ifdef ASDF
  `ASDF(width)
`endif

`
)",
                                             sm, "test", "", options);

    server::SyntaxIndexer indexer(*tree);
    auto& disabled = indexer.disabledRegions;

    REQUIRE(disabled.size() == 1);
    auto text = std::string(sm.getText(disabled[0]));
    CHECK(text.find("`ASDF") != std::string::npos);
}

TEST_CASE("InactiveRegions_MacroInDisabledBranch") {
    using namespace slang;

    SourceManager sm;

    Bag options;

    auto tree = syntax::SyntaxTree::fromText(R"(
`define WIDTH 8
`ifdef FOO
    logic [`WIDTH-1:0] a;
`else
    logic b;
`endif
)",
                                             sm, "test", "", options);

    server::SyntaxIndexer indexer(*tree);
    auto& disabled = indexer.disabledRegions;

    // The macro usage `WIDTH should not split the disabled region
    REQUIRE(disabled.size() == 1);
    CHECK(sm.getText(disabled[0]) == "logic [`WIDTH-1:0] a;");
}

TEST_CASE("InactiveRegions_NestedDirectivesMerged") {
    using namespace slang;

    SourceManager sm;
    Bag options;

    auto tree = syntax::SyntaxTree::fromText(R"(
`ifdef TOOL_A
    `ifndef FLAG_X
       `define RESULT
    `endif
`elsif TOOL_B
    `ifndef FLAG_X
       `define RESULT
    `endif
`elsif TOOL_C
    `ifndef FLAG_X
       `define RESULT
    `endif
`endif
)",
                                             sm, "test", "", options);

    server::SyntaxIndexer indexer(*tree);
    auto& disabled = indexer.disabledRegions;

    // Each disabled branch is its own region (the `elsif lines between
    // them are evaluated condition checks and should not be greyed out).
    // Each region should include the full inner `ifndef/`endif block.
    REQUIRE(disabled.size() == 3);
    for (auto& region : disabled) {
        auto text = std::string(sm.getText(region));
        CHECK(text.find("`ifndef FLAG_X") != std::string::npos);
        CHECK(text.find("`endif") != std::string::npos);
    }
}

TEST_CASE("InactiveRegions_Document") {
    ServerHarness server;
    JsonGoldenTest golden;

    auto header = server.openFile("foo.svh", "`define FOO\n");
    auto doc = server.openFile("test.sv", R"(
module top;
`ifdef FOO
    logic[2:0] a;
`else
    logic b;
`endif

`define BAR
`define BAZ

`ifndef BAR
    bit[2:0] c[13];
`elsif BAZ
    int d;
`else
    int e;
`endif
endmodule

`ifdef A logic foo; `else logic bar; `endif

`include "foo.svh"

`ifdef FOO
    logic[7:0] foot;
`else
    logic[7:0] bart;
`endif
)");

    auto regions = doc.getInactiveRegions();

    golden.record(regions);
}
