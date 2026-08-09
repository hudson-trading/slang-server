// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "util/Converters.h"
#include "utils/ServerHarness.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

using namespace slang;
using namespace server;

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

lsp::uint countPhysicalLines(std::string_view text) {
    if (text.empty())
        return 0;
    std::vector<size_t> offsets;
    SourceManager::computeLineOffsets(text, offsets);
    return static_cast<lsp::uint>(offsets.size());
}

// Resolve an LSP position through the same converters production uses, then read the
// identifier spelling at that buffer offset.
std::string tokenAt(::DocumentHandle& doc, const lsp::Position& pos) {
    auto loc = toSourceLocation(doc.doc->getBuffer(), pos, doc.m_server.sourceManager());
    if (!loc)
        return {};

    auto text = doc.getText();
    size_t offset = loc->offset();
    if (offset >= text.size())
        return {};

    std::string token;
    for (size_t i = offset; i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) ||
                                                 text[i] == '_');
         ++i) {
        token += text[i];
    }
    return token;
}

} // namespace

TEST_CASE("DocumentSymbol_NoLineDirective_Control", "[line_directive]") {
    // Guard: without `line, physical positions match ordinary expectations.
    ServerHarness server;

    auto doc = server.openFile("noline.sv", R"(module noline(input logic clk);
  logic delta;
endmodule
)");

    auto tree = doc.getSymbolTree();
    auto* delta = findSymbol(tree, "delta");
    REQUIRE(delta != nullptr);
    CHECK(delta->selectionRange.start.line == 1);
    checkInDocument(delta->selectionRange, 3);
}

TEST_CASE("DocumentSymbol_LineDirectiveUsesPhysicalLines", "[line_directive]") {
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

    // Prove remapped reporting still differs — otherwise this test isn't covering the bug.
    auto loc = doc.getLocation(doc.before("delta").m_offset);
    REQUIRE(loc.has_value());
    CHECK(server.sourceManager().getLineNumber(*loc) == 500);
    auto lspPos = toPosition(*loc, server.sourceManager());
    CHECK(lspPos.line == 2);
}

TEST_CASE("DocumentSymbol_MultipleLineDirectives", "[line_directive]") {
    ServerHarness server;

    auto doc = server.openFile("multidir.sv", R"(module multidir;
`line 500 "multidir.sv" 0
  logic first;
`line 10 "multidir.sv" 0
  logic second;
endmodule
)");

    auto tree = doc.getSymbolTree();
    auto* first = findSymbol(tree, "first");
    auto* second = findSymbol(tree, "second");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    // Physical lines: first=2, second=4. Remapped would be 499 and 9.
    CHECK(first->selectionRange.start.line == 2);
    CHECK(second->selectionRange.start.line == 4);
    checkInDocument(first->selectionRange, 6);
    checkInDocument(second->selectionRange, 6);

    auto firstLoc = doc.getLocation(doc.before("first").m_offset);
    auto secondLoc = doc.getLocation(doc.before("second").m_offset);
    REQUIRE(firstLoc);
    REQUIRE(secondLoc);
    CHECK(server.sourceManager().getLineNumber(*firstLoc) == 500);
    CHECK(server.sourceManager().getLineNumber(*secondLoc) == 10);
}

TEST_CASE("DocumentSymbol_CRLF_LineDirectiveUsesPhysicalLines", "[line_directive]") {
    ServerHarness server;

    // Explicit CRLF document (common on Windows-generated RTL).
    std::string text = "module linedir_crlf;\r\n"
                       "`line 500 \"linedir_crlf.sv\" 0\r\n"
                       "  logic delta;\r\n"
                       "endmodule\r\n";

    auto doc = server.openFile("linedir_crlf.sv", text);
    auto tree = doc.getSymbolTree();
    auto* delta = findSymbol(tree, "delta");
    REQUIRE(delta != nullptr);

    CHECK(delta->selectionRange.start.line == 2);
    checkInDocument(delta->selectionRange, countPhysicalLines(text));

    auto loc = doc.getLocation(doc.before("delta").m_offset);
    REQUIRE(loc);
    CHECK(server.sourceManager().getLineNumber(*loc) == 500);
    CHECK(toPosition(*loc, server.sourceManager()).line == 2);
}

TEST_CASE("Converters_RoundTrip_LineDirectivePhysicalPosition", "[line_directive]") {
    ServerHarness server;

    auto doc = server.openFile("roundtrip.sv", R"(module roundtrip;
`line 500 "roundtrip.sv" 0
  logic delta;
endmodule
)");

    auto loc = doc.getLocation(doc.before("delta").m_offset);
    REQUIRE(loc);

    auto pos = toPosition(*loc, server.sourceManager());
    CHECK(pos.line == 2);

    auto back = toSourceLocation(loc->buffer(), pos, server.sourceManager());
    REQUIRE(back);
    // Round-trip must land on the same buffer offset (column preserved).
    CHECK(back->offset() == loc->offset());
    CHECK(back->buffer() == loc->buffer());

    // Client using the LSP position as a cursor must still see the token text.
    CHECK(tokenAt(doc, pos) == "delta");
}

TEST_CASE("GotoDefinition_LineDirectiveUsesPhysicalLines", "[line_directive]") {
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
    CHECK(tokenAt(doc, defs[0].targetSelectionRange.start) == "delta");
}

TEST_CASE("Diagnostics_LineDirectiveUsesPhysicalLines", "[line_directive]") {
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

TEST_CASE("DocumentHighlight_LineDirectiveUsesPhysicalLines", "[line_directive]") {
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
        CHECK(tokenAt(doc, hl.range.start) == "delta");
    }
}

TEST_CASE("FindReferences_LineDirectiveUsesPhysicalLines", "[line_directive]") {
    ServerHarness server;

    auto doc = server.openFile("linedir_refs.sv", R"(module linedir_refs;
`line 500 "linedir_refs.sv" 0
  logic delta;
  assign delta = 1'b0;
endmodule
)");
    doc.ensureSynced();

    auto cursor = doc.before("delta");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = doc.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    REQUIRE(refs->size() >= 2);

    for (const auto& ref : *refs) {
        checkInDocument(ref.range, 5);
        CHECK(ref.range.start.line >= 2);
        CHECK(ref.range.start.line <= 3);
        CHECK(tokenAt(doc, ref.range.start) == "delta");
    }
}

TEST_CASE("Hover_LineDirectiveUsesPhysicalLines", "[line_directive]") {
    ServerHarness server;

    auto doc = server.openFile("linedir_hover.sv", R"(module linedir_hover;
`line 500 "linedir_hover.sv" 0
  logic delta;
  assign delta = 1'b0;
endmodule
)");

    auto useCursor = doc.after("assign ");
    auto hover = doc.getHoverAt(useCursor.m_offset);
    REQUIRE(hover.has_value());
    // Hover range (when present) must be in-document physical coordinates.
    if (hover->range) {
        checkInDocument(*hover->range, 5);
        CHECK(hover->range->start.line >= 2);
        CHECK(hover->range->start.line <= 3);
    }
}

TEST_CASE("DocumentSymbol_MacroAfterLineDirectiveUsesPhysicalLines", "[line_directive]") {
    // Macro-expanded declarations still report LSP positions at the physical
    // expansion site, not the `line-remapped line.
    ServerHarness server;

    auto doc = server.openFile("linedir_macro.sv", R"(`define DECL logic delta
module linedir_macro;
`line 500 "linedir_macro.sv" 0
  `DECL;
endmodule
)");

    auto tree = doc.getSymbolTree();
    auto* delta = findSymbol(tree, "delta");
    REQUIRE(delta != nullptr);

    // `DECL; is on physical line 3.
    CHECK(delta->selectionRange.start.line == 3);
    checkInDocument(delta->selectionRange, 5);

    auto loc = doc.getLocation(doc.before("`DECL").m_offset);
    REQUIRE(loc);
    // Remapped line at the expansion site should still be huge.
    CHECK(server.sourceManager().getLineNumber(*loc) >= 500);
    CHECK(toPosition(*loc, server.sourceManager()).line == 3);
}
