// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

#include <string_view>

using namespace slang;

namespace {

const lsp::DocumentSymbol* findSymbol(const std::vector<lsp::DocumentSymbol>& symbols,
                                      std::string_view name) {
    for (const auto& sym : symbols) {
        if (sym.name == name)
            return &sym;
        if (sym.children) {
            if (auto* found = findSymbol(*sym.children, name))
                return found;
        }
    }
    return nullptr;
}

void checkInDocument(const lsp::Range& range, lsp::uint docLines) {
    CHECK(range.start.line < docLines);
    CHECK(range.end.line < docLines);
    CHECK(range.start.line <= range.end.line);
}

} // namespace

TEST_CASE("DocumentSymbol_LineDirectiveUsesPhysicalLines") {
    // `line remaps diagnostic-style line numbers, but LSP Positions must index the
    // document text. Symbols after a `line directive must stay in-range.
    ServerHarness server;

    auto doc = server.openFile("linedir.sv", R"(module linedir(input logic clk);
`line 500 "linedir.sv" 0
  logic delta;
endmodule
)");

    auto tree = doc.getSymbolTree();
    REQUIRE_FALSE(tree.empty());

    auto* mod = findSymbol(tree, "linedir");
    REQUIRE(mod != nullptr);
    // Module name is before the `line directive; remapping must not move it.
    CHECK(mod->selectionRange.start.line == 0);

    auto* clk = findSymbol(tree, "clk");
    REQUIRE(clk != nullptr);
    CHECK(clk->selectionRange.start.line == 0);

    auto* delta = findSymbol(tree, "delta");
    REQUIRE(delta != nullptr);

    // Physical line of `logic delta;` is 2 (0-based). Remapped would be 499.
    CHECK(delta->selectionRange.start.line == 2);
    CHECK(delta->range.start.line == 2);
    checkInDocument(delta->selectionRange, 4);
    checkInDocument(delta->range, 4);
}

TEST_CASE("GotoDefinition_LineDirectiveUsesPhysicalLines") {
    ServerHarness server;

    auto doc = server.openFile("linedir_goto.sv", R"(module linedir_goto;
`line 500 "linedir_goto.sv" 0
  logic delta;
  assign delta = 1'b0;
endmodule
)");

    auto cursor = doc.after("assign ");
    auto defs = cursor.getDefinitions();
    REQUIRE_FALSE(defs.empty());

    // Definition of `delta` is on physical line 2, not remapped 499.
    CHECK(defs[0].targetSelectionRange.start.line == 2);
    checkInDocument(defs[0].targetSelectionRange, 5);
    checkInDocument(defs[0].targetRange, 5);
}

TEST_CASE("Diagnostics_LineDirectiveUsesPhysicalLines") {
    ServerHarness server;

    auto doc = server.openFile("linedir_diag.sv", R"(module linedir_diag;
`line 500 "linedir_diag.sv" 0
  logic delta = undeclared_id;
endmodule
)");

    auto diags = doc.getDiagnostics();
    REQUIRE_FALSE(diags.empty());

    bool foundInRangeDiag = false;
    for (const auto& diag : diags) {
        checkInDocument(diag.range, 4);
        // The undeclared identifier is on physical line 2.
        if (diag.range.start.line == 2)
            foundInRangeDiag = true;
    }
    CHECK(foundInRangeDiag);
}

TEST_CASE("DocumentHighlight_LineDirectiveUsesPhysicalLines") {
    ServerHarness server;

    auto doc = server.openFile("linedir_hl.sv", R"(module linedir_hl;
`line 500 "linedir_hl.sv" 0
  logic delta;
  assign delta = 1'b0;
endmodule
)");

    auto cursor = doc.after("assign ");
    auto highlights = cursor.getHighlights();
    REQUIRE_FALSE(highlights.empty());

    for (const auto& hl : highlights) {
        checkInDocument(hl.range, 5);
        // Both the declaration and use of `delta` are after the `line directive.
        CHECK(hl.range.start.line >= 2);
        CHECK(hl.range.start.line <= 3);
    }
}
