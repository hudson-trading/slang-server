// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

TEST_CASE("Package completions respect re-export visibility", "[scan-gaps]") {
    ServerHarness server;
    auto doc = server.openFile("exports.sv", R"(
package origin;
    typedef enum { OFF, ON } state_t;
    parameter int CONFIG = 3;
    parameter int HIDDEN = 4;
endpackage
package facade;
    import origin::*;
    import origin::HIDDEN;
    export origin::state_t, origin::OFF, origin::ON, origin::CONFIG;
endpackage
package relay;
    import facade::*;
    export *::*;
    state_t imported_state = OFF;
endpackage
module top;
    import facade::*;
    state_t state = ON;
    int value = CONFIG;
    initial begin
        value = facade::CONFIG;
        state = relay::OFF;
        value = int'(std::randomize(value));
    end
endmodule
)");
    CHECK(std::ranges::none_of(doc.getDiagnostics(), [](const auto& diagnostic) {
        return diagnostic.severity == lsp::DiagnosticSeverity::Error;
    }));
    for (auto prefix : {"value = ", "value = facade::"}) {
        CAPTURE(prefix);
        auto cursor = doc.after(prefix);
        auto items = std::string_view(prefix).ends_with("::") ? cursor.getCompletions(":")
                                                              : cursor.getCompletions();
        for (auto name : {"CONFIG", "state_t", "ON", "OFF"}) {
            CAPTURE(name);
            CHECK(std::ranges::any_of(items,
                                      [&](const auto& item) { return item.m_item.label == name; }));
        }
        CHECK(std::ranges::none_of(items,
                                   [](const auto& item) { return item.m_item.label == "HIDDEN"; }));
    }
    auto relay = doc.after("relay::").getCompletions(":");
    CHECK(std::ranges::any_of(relay, [](const auto& item) { return item.m_item.label == "OFF"; }));
    CHECK(std::ranges::none_of(relay,
                               [](const auto& item) { return item.m_item.label == "CONFIG"; }));
    auto lexical = doc.before("std::").getCompletions();
    for (auto name : {"origin", "facade", "relay"}) {
        CAPTURE(name);
        auto item = std::ranges::find(lexical, name,
                                      [](const auto& item) { return item.m_item.label; });
        REQUIRE(item != lexical.end());
        item->resolve();
        REQUIRE(item->m_item.documentation);
        CHECK(rfl::get<lsp::MarkupContent>(*item->m_item.documentation)
                  .value.find(std::string("package ") + name + ";") != std::string::npos);
    }
    auto standard = std::ranges::find(lexical, "std",
                                      [](const auto& item) { return item.m_item.label; });
    REQUIRE(standard != lexical.end());
    CHECK(standard->m_item.kind == lsp::CompletionItemKind::Module);
    CHECK(std::ranges::none_of(doc.getDiagnostics(), [](const auto& diagnostic) {
        return diagnostic.severity == lsp::DiagnosticSeverity::Error;
    }));
}
