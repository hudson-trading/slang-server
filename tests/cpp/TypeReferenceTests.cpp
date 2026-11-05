// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

TEST_CASE("Type reference operands complete values", "[scan-gaps]") {
    ServerHarness server;
    auto doc = server.openFile("type_operands.sv", R"(
module consumer #(parameter type T = int); endmodule
module sample;
    int source_value;
    typedef type(source_value) captured_t;
    consumer #(.T(type(source_value))) instance_name();
    type(source_value) another_value;
    typedef int ordinary_t;
endmodule
)");
    for (auto prefix : {"typedef type(", "consumer #(.T(type(", "    type("}) {
        CAPTURE(prefix);
        auto cursor = doc.after(prefix);
        auto items = cursor.getCompletions();
        CHECK(std::ranges::any_of(items, [](const CompletionHandle& item) {
            return item.m_item.label == "source_value";
        }));
    }
    auto types = doc.after("typedef ").before("int ordinary_t").getCompletions();
    CHECK(std::ranges::none_of(types, [](const CompletionHandle& item) {
        return item.m_item.label == "source_value";
    }));
}
