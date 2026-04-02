# AI SLOP

#include "document/SyntaxIndexer.h"
#include <catch2/catch_test_macros.hpp>

#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

TEST_CASE("DisabledRegions_IfdefFalse") {
    using namespace slang;

    SourceManager sm;

    Bag options;

    auto tree = syntax::SyntaxTree::fromText(R"(
`ifdef FOO
logic a;
`else
logic b;
`endif
)",
                                             sm, "test", "", options);

    server::SyntaxIndexer indexer(*tree);
    auto& disabled = indexer.disabledRegions;

    REQUIRE(disabled.size() == 1);

    auto text = sm.getText(disabled[0]);
    INFO("Disabled text:\n" << text);

    CHECK(text.find("logic a;") != std::string::npos);
}

TEST_CASE("DisabledRegions_IfdefTrue") {
    using namespace slang;

    SourceManager sm;

    Bag options;

    auto tree = syntax::SyntaxTree::fromText(R"(
`define FOO
`ifdef FOO
logic a;
`else
logic b;
`endif
logic c;
)",
                                             sm, "test", "", options);

    server::SyntaxIndexer indexer(*tree);
    auto& disabled = indexer.disabledRegions;

    REQUIRE(disabled.size() == 1);

    auto text = sm.getText(disabled[0]);
    INFO("Disabled text:\n" << text);

    CHECK(text.find("logic b;") != std::string::npos);
}

TEST_CASE("DisabledRegions_Elsif") {
    using namespace slang;

    SourceManager sm;

    Bag options;

    auto tree = syntax::SyntaxTree::fromText(R"(
`define BAR
`ifdef FOO
logic a;
`elsif BAR
logic b;
`else
logic c;
`endif
logic d;
)",
                                             sm, "test", "", options);

    server::SyntaxIndexer indexer(*tree);
    auto& disabled = indexer.disabledRegions;

    REQUIRE(disabled.size() == 2);

    std::string combined;
    for (auto& r : disabled)
        combined += sm.getText(r);

    INFO(combined);

    CHECK(combined.find("logic a;") != std::string::npos);
    CHECK(combined.find("logic c;") != std::string::npos);
}
