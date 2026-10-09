// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

TEST_CASE("Generate bodies retain feature scopes with a missing genvar", "[scan-gaps]") {
    ServerHarness server;
    auto doc = server.openFile("incomplete_generate.sv", R"(
module sample;
    for ( = 0; i < 2; i++) begin : items
        int field;
        initial field = 0;
    end
endmodule
)");
    auto cursor = doc.after("initial ");
    auto definitions = cursor.getDefinitions();
    REQUIRE(definitions.size() == 1);
    CHECK(definitions.front().targetSelectionRange.start == doc.before("field;").getPosition());
    REQUIRE(doc.getHoverAt(cursor.m_offset));
    CHECK(std::ranges::any_of(cursor.getCompletions(), [](const CompletionHandle& item) {
        return item.m_item.label == "field";
    }));
}

TEST_CASE("Unelaborated nested generate loops retain lexical scopes", "[scan-gaps]") {
    for (auto parameters : {"parameter int COUNT, parameter type T",
                            "parameter int COUNT = 0, parameter type T = bit",
                            "parameter int COUNT = 2, parameter type T = bit [1:0]"}) {
        CAPTURE(parameters);
        ServerHarness server;
        auto doc = server.openFile("generate_scopes.sv",
                                   std::string("module sample #(") + parameters + R"();
    for (genvar i = 0; i < $bits(T); i++) begin : outer_loop
        logic [COUNT-1:0] values;
        for (genvar j = 0; j < COUNT; j++) begin : inner_loop
            always_comb values[j] = i;
        end
    end
endmodule
)");
        for (auto [prefix, name, declaration] :
             {std::tuple{"j = 0; ", "j", "j = 0"}, std::tuple{"always_comb ", "values", "values;"},
              std::tuple{"values[", "j", "j = 0"}, std::tuple{"values[j] = ", "i", "i = 0"}}) {
            CAPTURE(prefix);
            auto cursor = doc.after(prefix);
            auto definitions = cursor.getDefinitions();
            REQUIRE_FALSE(definitions.empty());
            for (const auto& definition : definitions)
                CHECK(definition.targetSelectionRange.start ==
                      doc.before(declaration).getPosition());
            REQUIRE(doc.getHoverAt(cursor.m_offset));
            auto items = cursor.getCompletions();
            CHECK(std::ranges::any_of(items, [&](const CompletionHandle& item) {
                return item.m_item.label == name;
            }));
        }
    }
}
