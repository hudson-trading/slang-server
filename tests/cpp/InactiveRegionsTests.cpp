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
