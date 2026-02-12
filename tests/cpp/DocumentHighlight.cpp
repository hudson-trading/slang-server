// SPDX-FileCopyrightTxt: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"

TEST_CASE("DocumentHighlightSanity") {
    ServerHarness server("");
    auto doc = server.openFile("test1.sv", R"(module top; endmodule)");

    auto highlightsNone = doc.begin().getHighlights();
    REQUIRE(highlightsNone.empty());

    auto highlightsSome = doc.before("top").getHighlights();
    REQUIRE(highlightsSome.size() == 1);
}

TEST_CASE("DocumentHighlightScope") {
    ServerHarness server("");
    JsonGoldenTest golden;

    auto doc = server.openFile("test2.sv", R"(
module top;
    logic var_1;
    sub i_sub(.var_1(var_1));
endmodule

module sub(output logic var_1);
    assign var_1 = 1'b0;
endmodule
)");

    // Must only highlight instances in top module
    auto cursorTop = doc.before("var_1");
    golden.record("scope_top", cursorTop.getHighlights());

    // Must only highlight instances in sub module
    auto cursorSub = doc.before("var_1", cursorTop.m_offset + 1);
    golden.record("scope_sub", cursorSub.getHighlights());
}
