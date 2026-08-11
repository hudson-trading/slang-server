// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"
#include <cstdlib>

static size_t countSubstring(std::string_view text, std::string_view substring) {
    size_t count = 0;
    for (size_t pos = 0; (pos = text.find(substring, pos)) != std::string_view::npos;
         pos += substring.size()) {
        count++;
    }
    return count;
}

TEST_CASE("HoverMacroExpansion") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`define WIDTH 8
`define ADD(a, b) a + b
`define MAKE_SIG(name) sig_``name
module top;
    logic [`WIDTH-1:0] data;
    localparam int x = `ADD(3, 4);
    logic `MAKE_SIG(foo);
endmodule
)");

    // Hover on `WIDTH should show expansion
    auto widthCursor = doc.before("`WIDTH");
    auto widthHover = doc.getHoverAt(widthCursor.m_offset);
    REQUIRE(widthHover.has_value());
    auto widthContent = rfl::get<lsp::MarkupContent>(widthHover->contents);
    CHECK(widthContent.value.find("Expands to") != std::string::npos);
    CHECK(widthContent.value.find("8") != std::string::npos);

    // Hover on `ADD should show expansion
    auto addCursor = doc.before("`ADD");
    auto addHover = doc.getHoverAt(addCursor.m_offset);
    REQUIRE(addHover.has_value());
    auto addContent = rfl::get<lsp::MarkupContent>(addHover->contents);
    CHECK(addContent.value.find("Expands to") != std::string::npos);
    CHECK(addContent.value.find("3 + 4") != std::string::npos);

    // Hover on `MAKE_SIG should show concatenated expansion
    auto sigCursor = doc.before("`MAKE_SIG");
    auto sigHover = doc.getHoverAt(sigCursor.m_offset);
    REQUIRE(sigHover.has_value());
    auto sigContent = rfl::get<lsp::MarkupContent>(sigHover->contents);
    CHECK(sigContent.value.find("Expands to") != std::string::npos);
    CHECK(sigContent.value.find("sig_foo") != std::string::npos);
}

TEST_CASE("HoverNonAsciiString") {
    // Regression test: hovering on a string parameter with non-ASCII bytes should not crash
    // "a" + "b" in SV adds the character codes, producing 0xc3 which is invalid UTF-8
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module top;
    localparam string ab1 = "a" + "b";

    // Valid first char, invalid second char
    localparam string ab2 = {"a", ab1};
endmodule
)");

    {
        auto cursor = doc.before("ab1 =");
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover.has_value());

        // The hover should contain "Value:" for the parameter
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        CHECK(content.value.find("Value:") != std::string::npos);
        // The value should show escaped string and hex (0xc3 = 'a' + 'b' = 97 + 98 = 195)
        // Format: "\xc3"
        CHECK(content.value.find("\\xc3") != std::string::npos);

        // Verify json serialization works
        auto json = rfl::json::write(*hover);
        CHECK(!json.empty());
    }
    {
        auto cursor = doc.before("ab2 =");
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover.has_value());
        // The hover should contain "Value:" for the parameter
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        // Value should show valid utf string for first 'a' and escaped for second invalid char
        CHECK(content.value.find("a\\xc3") != std::string::npos);
    }
}

TEST_CASE("HoverValidString") {
    // Test that valid ASCII/UTF-8 strings display normally
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module top;
    localparam string greeting = "hello";
endmodule
)");

    auto cursor = doc.before("greeting =");
    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover.has_value());

    auto content = rfl::get<lsp::MarkupContent>(hover->contents);
    // Valid strings should display as quoted strings, not bit values
    CHECK(content.value.find("\"hello\"") != std::string::npos);

    auto json = rfl::json::write(*hover);
    CHECK(!json.empty());
}

TEST_CASE("HoverTypeParameterValue") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module leaf #(parameter type T = logic) ();
endmodule

module top;
    leaf #(.T(byte)) u();
endmodule
)");

    auto cursor = doc.before("T(byte)");
    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover.has_value());

    auto content = rfl::get<lsp::MarkupContent>(hover->contents);
    CAPTURE(content.value);
    CHECK(content.value.find("**TypeAlias** `T`") != std::string::npos);
    CHECK(content.value.find("Value: `byte`") != std::string::npos);
}

TEST_CASE("HoverAndGotoImplicitPortShowBothSides") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module leaf(input logic [7:0] shared);
endmodule

module top;
    logic [7:0] shared;
    leaf u(.shared);
endmodule
)");

    auto cursor = doc.after("u(.");
    auto info = doc.getDefinitionInfoAt(cursor.m_offset);
    REQUIRE(info);
    REQUIRE(info->targets.size() == 1);
    auto* portTarget = std::get_if<server::DefinitionInfo::PortConnectionTarget>(
        &info->targets.front());
    REQUIRE(portTarget);

    auto defs = cursor.getDefinitions();
    REQUIRE(defs.size() == 2);
    CHECK(defs[0].targetSelectionRange.start.line > defs[1].targetSelectionRange.start.line);

    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CHECK(content.find("input logic [7:0] shared") != std::string::npos);
    CHECK(content.find("logic [7:0] shared") != std::string::npos);
    CHECK(countSubstring(content, "Type: `logic[7:0]`") == 1);
    CHECK(countSubstring(content, "Width: `8`") == 1);
    auto outerLabel = content.find("**Outer**");
    auto innerLabel = content.find("**Inner**");
    CHECK(outerLabel != std::string::npos);
    CHECK(innerLabel != std::string::npos);
    CHECK(outerLabel < innerLabel);
}

TEST_CASE("HoverAndGotoForwardedInterfacePortShowBothSides") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
interface bus;
    logic data;
endinterface

module leaf(bus shared);
endmodule

module top(bus shared);
    leaf u(.shared);
endmodule
)");

    auto cursor = doc.after("u(.");
    auto info = doc.getDefinitionInfoAt(cursor.m_offset);
    REQUIRE(info);
    REQUIRE(info->targets.size() == 1);
    auto* portTarget = std::get_if<server::DefinitionInfo::PortConnectionTarget>(
        &info->targets.front());
    REQUIRE(portTarget);
    CHECK(portTarget->outer.symbol->kind == slang::ast::SymbolKind::InterfacePort);
    CHECK(portTarget->inner.symbol->kind == slang::ast::SymbolKind::InterfacePort);

    auto location = doc.getLocation(cursor.m_offset);
    REQUIRE(location);
    auto analysis = doc.doc->getAnalysis();
    auto* token = analysis->getWordTokenAt(*location);
    REQUIRE(token);
    CHECK(analysis->getSymbolAtToken(token) == portTarget->inner.symbol);

    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CHECK(countSubstring(content, "**Outer**") == 1);
    CHECK(countSubstring(content, "**Inner**") == 1);
}

TEST_CASE("HoverAndGotoModportShowTypeAndDirection") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
interface bus;
    logic [7:0] data;
    assign data = '0;
    modport initiator(output data);
endinterface

module top;
    bus b();
endmodule
)");

    auto cursor = doc.before("data);");
    auto info = doc.getDefinitionInfoAt(cursor.m_offset);
    REQUIRE(info);
    REQUIRE(info->targets.size() == 1);
    auto* symbolTarget = std::get_if<server::DefinitionInfo::SymbolTarget>(&info->primaryTarget());
    REQUIRE(symbolTarget);
    CHECK(symbolTarget->symbol->kind == slang::ast::SymbolKind::ModportPort);
    CHECK(symbolTarget->syntaxes.size() == 2);

    auto defs = cursor.getDefinitions();
    CHECK(defs.size() == 2);

    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CHECK(content.find("logic [7:0] data") != std::string::npos);
    CHECK(content.find("output data") != std::string::npos);
    CHECK(countSubstring(content, "**ModportPort** `data`") == 1);
    CHECK(countSubstring(content, "Type: `logic[7:0]`") == 1);
    CHECK(countSubstring(content, "Width: `8`") == 1);
    CHECK(countSubstring(content, "Driven by continuous assignment") == 1);
}

TEST_CASE("HoverAndGotoExplicitModportPrototypeRetainsDeclaration") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
interface bus;
    task transfer(input logic value);
    endtask
    modport matching(import task transfer(input logic value));
    modport mismatched(import task transfer(input int value));
endinterface
)");

    auto matching = doc.after("matching(import task ");
    auto matchingInfo = doc.getDefinitionInfoAt(matching.m_offset);
    REQUIRE(matchingInfo);
    REQUIRE(matchingInfo->targets.size() == 2);
    CHECK(std::get<server::DefinitionInfo::SymbolTarget>(matchingInfo->targets[0]).symbol->kind ==
          slang::ast::SymbolKind::MethodPrototype);
    CHECK(std::get<server::DefinitionInfo::SymbolTarget>(matchingInfo->targets[1]).symbol->kind ==
          slang::ast::SymbolKind::Subroutine);
    auto matchingLocation = doc.getLocation(matching.m_offset);
    REQUIRE(matchingLocation);
    auto analysis = doc.doc->getAnalysis();
    auto* matchingToken = analysis->getWordTokenAt(*matchingLocation);
    REQUIRE(matchingToken);
    CHECK(analysis->getSymbolAtToken(matchingToken) ==
          std::get<server::DefinitionInfo::SymbolTarget>(matchingInfo->targets[1]).symbol);

    auto matchingDefs = matching.getDefinitions();
    REQUIRE(matchingDefs.size() == 2);
    CHECK(matchingDefs[0].targetSelectionRange.start.line == 4);
    CHECK(matchingDefs[1].targetSelectionRange.start.line == 2);

    auto matchingHover = doc.getHoverAt(matching.m_offset);
    REQUIRE(matchingHover);
    auto matchingContent = rfl::get<lsp::MarkupContent>(matchingHover->contents).value;
    CHECK(matchingContent.find("**MethodPrototype** `transfer` in `bus.matching`") !=
          std::string::npos);
    CHECK(matchingContent.find("**Subroutine** `transfer` in `bus`") != std::string::npos);
    CHECK(matchingContent.find("task transfer(input logic value);") != std::string::npos);

    auto mismatched = doc.after("mismatched(import task ");
    auto mismatchedInfo = doc.getDefinitionInfoAt(mismatched.m_offset);
    REQUIRE(mismatchedInfo);
    REQUIRE(mismatchedInfo->targets.size() == 1);
    CHECK(std::get<server::DefinitionInfo::SymbolTarget>(mismatchedInfo->targets[0]).symbol->kind ==
          slang::ast::SymbolKind::MethodPrototype);
    auto mismatchedLocation = doc.getLocation(mismatched.m_offset);
    REQUIRE(mismatchedLocation);
    auto* mismatchedToken = analysis->getWordTokenAt(*mismatchedLocation);
    REQUIRE(mismatchedToken);
    CHECK(analysis->getSymbolAtToken(mismatchedToken) ==
          std::get<server::DefinitionInfo::SymbolTarget>(mismatchedInfo->targets[0]).symbol);

    auto mismatchedDefs = mismatched.getDefinitions();
    REQUIRE(mismatchedDefs.size() == 1);
    CHECK(mismatchedDefs[0].targetSelectionRange.start.line == 5);

    auto mismatchedHover = doc.getHoverAt(mismatched.m_offset);
    REQUIRE(mismatchedHover);
    auto mismatchedContent = rfl::get<lsp::MarkupContent>(mismatchedHover->contents).value;
    CHECK(mismatchedContent.find("**MethodPrototype** `transfer` in `bus.mismatched`") !=
          std::string::npos);
    CHECK(mismatchedContent.find("task transfer(input int value)") != std::string::npos);
}

TEST_CASE("HoverGeneratedSymbolsShowsEveryValue") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module top;
    for (genvar i = 0; i < 3; i++) begin : g
        localparam int VALUE = i;
        logic [VALUE:0] data;
    end
endmodule
)");

    auto checkAllValues = [&](Cursor cursor) {
        auto info = doc.getDefinitionInfoAt(cursor.m_offset);
        REQUIRE(info);
        CHECK(info->targets.size() == 3);

        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
        CHECK(content.find("Value: `0`") != std::string::npos);
        CHECK(content.find("Value: `1`") != std::string::npos);
        CHECK(content.find("Value: `2`") != std::string::npos);

        // All elaborated symbols share the same source declaration.
        CHECK(cursor.getDefinitions().size() == 1);
    };

    checkAllValues(doc.before("VALUE ="));
    checkAllValues(doc.before("VALUE:0"));

    auto genvarHover = doc.getHoverAt(doc.after("VALUE = ").m_offset);
    REQUIRE(genvarHover);
    auto genvarContent = rfl::get<lsp::MarkupContent>(genvarHover->contents).value;
    CHECK(genvarContent.find("Value range: `0` through `2`") != std::string::npos);
    CHECK(genvarContent.find("Value: `0`") == std::string::npos);
}

TEST_CASE("HoverPlaintextDocComments") {
    ServerHarness server;

    Config config;
    config.hovers.value().docCommentFormat = Config::HoverConfig::DocCommentFormat::plaintext;
    server.loadConfig(config);

    auto doc = server.openFile("test.sv", R"(
module top;
    /// 1. not a list
    /// <br />
    logic foo;
endmodule
)");

    auto cursor = doc.before("foo;");
    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover.has_value());

    auto content = rfl::get<lsp::MarkupContent>(hover->contents);
    CHECK(content.value.find("1\\. not a list") != std::string::npos);
    CHECK(content.value.find("\\<br />") != std::string::npos);
    // Plaintext mode strips comment markers
    CHECK(content.value.find("///") == std::string::npos);
}

TEST_CASE("HoverRawDocComments") {
    ServerHarness server;

    Config config;
    config.hovers.value().docCommentFormat = Config::HoverConfig::DocCommentFormat::raw;
    server.loadConfig(config);

    auto doc = server.openFile("test.sv", R"(
module top;
    /// a doc line
    /// another line
    logic foo;
endmodule
)");

    auto cursor = doc.before("foo;");
    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover.has_value());

    auto content = rfl::get<lsp::MarkupContent>(hover->contents);
    // Raw mode preserves the comment markers verbatim
    CHECK(content.value.find("/// a doc line") != std::string::npos);
    CHECK(content.value.find("/// another line") != std::string::npos);
}

TEST_CASE("HoverSystemTask") {
    ServerHarness server;
    GoldenTest golden;

    auto doc = server.openFile("test.sv", R"(
`define USE_WIDTH(name, val) localparam int name = val
module top;
    int q[$];
    initial begin
        $display("hello");
        $finish;
        q.push_back(42);
        if (q[$] == 0) $display("first");
        if ($root.top.q.size()) $display("ok");
    end
    localparam int W = $bits(int);
    localparam int L = $clog2(64);
    `USE_WIDTH(M, $clog2(128));
endmodule
)");

    auto recordHover = [&](const std::string& label, const std::string& target) {
        auto cursor = doc.before(target);
        auto hover = doc.getHoverAt(cursor.m_offset);
        golden.record(label);
        golden.record(":\n");
        if (!hover.has_value()) {
            golden.record("<no hover>\n\n");
            return;
        }
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        golden.record(content.value);
        golden.record("\n\n");
    };

    auto recordNoSystemHover = [&](const std::string& label, const std::string& target,
                                   bool trailingBlank = true) {
        auto cursor = doc.before(target);
        auto hover = doc.getHoverAt(cursor.m_offset);
        golden.record(label);
        golden.record(":\n");
        if (!hover.has_value()) {
            golden.record(trailingBlank ? "<no hover>\n\n" : "<no hover>\n");
            return;
        }
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        if (content.value.find("**System ") == std::string::npos) {
            golden.record(trailingBlank ? "<no system hover>\n\n" : "<no system hover>\n");
            return;
        }
        golden.record(content.value);
        golden.record(trailingBlank ? "\n\n" : "\n");
    };

    recordHover("$display", "$display(\"hello\")");
    recordHover("$finish", "$finish");
    recordHover("$bits", "$bits(int)");
    recordHover("$clog2", "$clog2");
    recordHover("macro argument $clog2", "$clog2(128)");
    recordNoSystemHover("queue declaration $", "$];");
    recordNoSystemHover("queue selector $", "$] ==");
    recordNoSystemHover("$root", "$root", false);
}
