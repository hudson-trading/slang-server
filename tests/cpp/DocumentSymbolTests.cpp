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

    auto* delta = findSymbol(tree, "delta");
    REQUIRE(delta != nullptr);

    // Physical line of `logic delta;` is 2 (0-based). Remapped would be 499.
    CHECK(delta->selectionRange.start.line == 2);
    CHECK(delta->range.start.line == 2);
    CHECK(delta->selectionRange.start.line < 4);
    CHECK(delta->range.start.line < 4);
}
