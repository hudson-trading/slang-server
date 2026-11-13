// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"

using namespace server;

TEST_CASE("SystemTimingChecks", "[timing-checks]") {
    ServerHarness server;
    GoldenTest golden;
    auto doc = server.openFile("timing.sv", R"(
module top(input logic clk, data);
    specify
        $setup(data, posedge clk, 1);
        $hold(posedge clk, data, 1);
        $setuphold(posedge clk, data, 1, 1);
        $recovery(posedge clk, data, 1);
        $removal(posedge clk, data, 1);
        $recrem(posedge clk, data, 1, 1);
        $skew(posedge clk, data, 1);
        $timeskew(posedge clk, data, 1);
        $fullskew(posedge clk, data, 1, 1);
        $period(posedge clk, 1);
        $width(posedge clk, 1);
        $nochange(posedge clk, data, 1, 1);
    endspecify
endmodule
)");
    CHECK(doc.getDiagnostics().empty());
    for (auto name : {"$setup", "$hold", "$setuphold", "$recovery", "$removal", "$recrem", "$skew",
                      "$timeskew", "$fullskew", "$period", "$width", "$nochange"}) {
        CAPTURE(name);
        auto hover = doc.getHoverAt(doc.before(std::string(name) + "(").m_offset);
        REQUIRE(hover);
        golden.record(rfl::get<lsp::MarkupContent>(hover->contents).value);
        golden.record(std::string_view(name) == "$nochange" ? "\n" : "\n\n");
        auto cursor = doc.after(name);
        CHECK(cursor.getDefinitions().empty());
        auto items = cursor.getCompletions("$");
        auto item = std::ranges::find(items, name,
                                      [](const auto& item) { return item.m_item.label; });
        REQUIRE(item != items.end());
        REQUIRE(item->m_item.documentation);
    }
}

TEST_CASE("Timing checks complete only inside specify blocks", "[timing-checks]") {
    ServerHarness server;
    for (bool specify : {true, false}) {
        CAPTURE(specify);
        auto doc = server.openFile("prefix.sv", specify ? R"(
module top;
    specify
        $set
    endspecify
endmodule
)"
                                                        : R"(
module top;
    initial $set
endmodule
)");
        auto items = doc.after("$set").getCompletions("$");
        auto item = std::ranges::find(items, "$setuphold",
                                      [](const auto& item) { return item.m_item.label; });
        if (specify) {
            REQUIRE(item != items.end());
            CHECK(item->m_item.insertText == "\\$setuphold(${1:reference_event}, ${2:data_event}, "
                                             "${3:setup_limit}, ${4:hold_limit})");
        }
        else {
            CHECK(item == items.end());
        }
        doc.close();
    }
}
