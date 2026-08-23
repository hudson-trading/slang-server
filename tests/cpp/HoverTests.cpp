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

static void normalizeHoverOutput(std::string& text, const DocumentHandle& doc) {
    const auto uri = doc.m_uri.str();
    for (size_t pos = 0; (pos = text.find(uri, pos)) != std::string::npos;) {
        text.replace(pos, uri.size(), "file:///test.sv");
        pos += std::string_view("file:///test.sv").size();
    }

    for (size_t newline = 0; (newline = text.find('\n', newline)) != std::string::npos;) {
        auto whitespaceStart = newline;
        while (whitespaceStart > 0 &&
               (text[whitespaceStart - 1] == ' ' || text[whitespaceStart - 1] == '\t')) {
            whitespaceStart--;
        }
        text.erase(whitespaceStart, newline - whitespaceStart);
        newline = whitespaceStart + 1;
    }
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

TEST_CASE("HoverMacroDefinedByMacroExpansion") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`define DEFINE_DEFAULT(NAME, VALUE) \
`ifndef NAME \
`define NAME VALUE \
`endif

`DEFINE_DEFAULT(FEATURE_ENABLE, 1)
module top;
    parameter bit feature_enable = `FEATURE_ENABLE;
endmodule
)");

    auto usage = doc.before("`FEATURE_ENABLE;");
    auto hover = doc.getHoverAt(usage.m_offset);
    REQUIRE(hover.has_value());
    auto content = rfl::get<lsp::MarkupContent>(hover->contents);
    CHECK(content.value.find("DefineDirective FEATURE_ENABLE") != std::string::npos);
    CHECK(content.value.find("Defined via command-line flags") == std::string::npos);

    auto defs = usage.getDefinitions();
    REQUIRE(defs.size() == 1);
    CHECK(defs[0].targetUri == doc.m_uri);
    CHECK(defs[0].targetSelectionRange.start ==
          doc.before("`DEFINE_DEFAULT(FEATURE_ENABLE").getPosition());
}

TEST_CASE("HoverFieldsOfInvalidStructType") {
    ServerHarness server;
    auto doc = server.openFile("invalid_struct_field.sv", R"(
module top;
    typedef struct packed {
        logic good;
        real invalid;
        logic other;
    } partial_t;

    partial_t value;
    initial $display(value.good, value.invalid, value.other);
endmodule
)");

    auto checkHover = [&](size_t offset, std::string_view name, std::string_view type) {
        auto hover = doc.getHoverAt(offset);
        REQUIRE(hover);
        auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
        CAPTURE(name, content);
        CHECK(content.find("**Field** `" + std::string(name) + "`") != std::string::npos);
        CHECK(content.find("Type: `" + std::string(type) + "`") != std::string::npos);
        CHECK(content.find("Declared Type:") == std::string::npos);
    };

    checkHover(doc.before("good;").m_offset, "good", "logic");
    checkHover(doc.before("invalid;").m_offset, "invalid", "real");
    checkHover(doc.before("other;").m_offset, "other", "logic");
    checkHover(doc.after("value.").m_offset, "good", "logic");
    checkHover(doc.after("value.good, value.").m_offset, "invalid", "real");
    checkHover(doc.after("value.invalid, value.").m_offset, "other", "logic");
}

TEST_CASE("HoverLinksUseFriendlyAnonymousTypeNames") {
    ServerHarness server;
    auto doc = server.openFile("anonymous_type.sv", R"(
module top;
    struct { logic member; } value;
    initial value.member = 1;
endmodule
)");

    auto hover = doc.getHoverAt(doc.before("value.member").m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CAPTURE(content);
    CHECK(content.find("Type: [`UnpackedStruct struct") != std::string::npos);
    CHECK(content.find("s$") == std::string::npos);
}

TEST_CASE("HoverDriversGeneratedByMacroUseExpansionLocation") {
    ServerHarness server;

    auto header = server.openFile("driver_macros.svh", R"(
`define DRIVE_PAIR(clk, value) \
    always_comb value = 1'b0; \
    always_ff @(posedge clk) value <= 1'b1;
)");
    auto doc = server.openFile("test.sv", R"(
`include "driver_macros.svh"
module top;
    logic clk;
    logic value;
    `DRIVE_PAIR(clk, value)
endmodule
)");

    auto hover = doc.getHoverAt(doc.before("value;").m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CAPTURE(content);
    CHECK(content.find("Driven by always_comb at [test.sv:6:5]") != std::string::npos);
    CHECK(content.find("Driven by always_ff at [test.sv:6:5]") != std::string::npos);
    CHECK(countSubstring(content, "Expanded from") == 2);
    CHECK(countSubstring(content, "`DRIVE_PAIR(clk, value)") == 2);
    CHECK(content.find("driver_macros.svh#L") == std::string::npos);

    auto combHeader = content.find("Driven by always_comb");
    auto combExpansion = content.find("Expanded from", combHeader);
    auto nextSection = content.find("\n\n---\n\n", combHeader);
    CHECK(combHeader < combExpansion);
    CHECK(combExpansion < nextSection);
}

TEST_CASE("HoverDriverSyntaxListSeparatesNodes") {
    ServerHarness server;

    auto header = server.openFile("driver_macros.svh", R"(
`define CONNECT_PAIR(value) producer u(.a(value), .b(value))
)");
    auto doc = server.openFile("test.sv", R"(
`include "driver_macros.svh"
module producer(output logic a, output logic b);
    assign a = 1'b0;
    assign b = 1'b1;
endmodule
module top;
    logic value;
    `CONNECT_PAIR(value)
endmodule
)");

    auto hover = doc.getHoverAt(doc.before("value;").m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CAPTURE(content);
    CHECK(content.find(".a(value)") != std::string::npos);
    CHECK(content.find(".b(value)") != std::string::npos);
    CHECK(countSubstring(content, "Expanded from") == 2);
    CHECK(countSubstring(content, "`CONNECT_PAIR(value)") == 2);

    auto driverHeader = content.find("Driven via port");
    auto expansion = content.find("Expanded from", driverHeader);
    auto nextSection = content.find("\n\n---\n\n", driverHeader);
    auto aNode = content.find(".a(value)");
    auto bNode = content.find(".b(value)");
    CHECK(driverHeader < expansion);
    CHECK(expansion < nextSection);
    bool nodesAreSeparated = (aNode < nextSection && nextSection < bNode) ||
                             (bNode < nextSection && nextSection < aNode);
    CHECK(nodesAreSeparated);
}

TEST_CASE("HoverDocCommentUsesSeparateSection") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module top;
    /// Value documentation.
    logic value;
endmodule
)");

    auto hover = doc.getHoverAt(doc.before("value;").m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CAPTURE(content);
    auto comment = content.find("Value documentation.");
    auto nextSection = content.find("\n\n---\n\n", comment);
    auto syntax = content.find("logic value;", comment);
    CHECK(comment < nextSection);
    CHECK(nextSection < syntax);
}

TEST_CASE("HoverDriverDocCommentStaysWithSyntax") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module top;
    logic value;
    /// Driver documentation.
    always_comb value = 1'b0;
endmodule
)");

    auto hover = doc.getHoverAt(doc.before("value;").m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CAPTURE(content);
    auto driverHeader = content.find("Driven by always_comb");
    auto comment = content.find("/// Driver documentation.", driverHeader);
    auto syntax = content.find("always_comb value = 1'b0;", comment);
    CHECK(driverHeader < comment);
    CHECK(comment < syntax);
    CHECK(content.find("\n\n---\n\n", driverHeader) == std::string::npos);
}

TEST_CASE("HoverCommandLineDefine") {
    ServerHarness server;
    Config config;
    config.flagsByFile.value().push_back({"test_flags.f", "-DFEATURE_ENABLE=1"});
    server.loadConfig(config);

    auto doc = server.openFile("test.sv", R"(
module top;
    parameter bit feature_enable = `FEATURE_ENABLE;
endmodule
)");

    auto usage = doc.before("`FEATURE_ENABLE;");
    auto info = doc.getDefinitionInfoAt(usage.m_offset);
    REQUIRE(info.has_value());
    REQUIRE(info->macro());
    CHECK(info->macro()->commandLineDefine());
}

TEST_CASE("HoverMacroNotFollowingTypedef") {
    // Regression for #425: define directives are trivia on the following token, so
    // selectDisplayNode must not promote to a trailing TypedefDeclaration.
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`define DATA_W 32

typedef struct packed {
  logic [7:0]  field_a;
  logic [15:0] field_b;
} big_struct_t;

`define A 8
`define B 4
typedef struct packed { logic x; } small_t;

module top;
    logic [`DATA_W-1:0] data;
    logic [`A-1:0] a;
    logic [`B-1:0] b;
endmodule
)");

    {
        auto hover = doc.getHoverAt(doc.before("`DATA_W").m_offset);
        REQUIRE(hover.has_value());
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        CAPTURE(content.value);
        CHECK(content.value.find("`define DATA_W") != std::string::npos);
        CHECK(content.value.find("big_struct_t") == std::string::npos);
        CHECK(content.value.find("Expands to") != std::string::npos);
        CHECK(content.value.find("32") != std::string::npos);
    }
    {
        auto hover = doc.getHoverAt(doc.before("`A").m_offset);
        REQUIRE(hover.has_value());
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        CAPTURE(content.value);
        CHECK(content.value.find("`define A") != std::string::npos);
        CHECK(content.value.find("small_t") == std::string::npos);
    }
    {
        auto hover = doc.getHoverAt(doc.before("`B").m_offset);
        REQUIRE(hover.has_value());
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        CAPTURE(content.value);
        CHECK(content.value.find("`define B") != std::string::npos);
        CHECK(content.value.find("small_t") == std::string::npos);
    }
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
    CHECK(content.find("Driven via port from `leaf u`") != std::string::npos);
}

TEST_CASE("HoverImplicitPortInGenerateLoopDeduplicatesElaborations") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module leaf(input logic reset);
endmodule

module top(input logic reset);
    for (genvar i = 0; i < 2; i++) begin : instances
        leaf memory(.reset);
    end
endmodule
)");

    auto cursor = doc.after("memory(.");
    auto info = doc.getDefinitionInfoAt(cursor.m_offset);
    REQUIRE(info);
    REQUIRE(info->targets.size() == 1);

    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CAPTURE(content);
    CHECK(countSubstring(content, "**Outer**") == 1);
    CHECK(countSubstring(content, "**Inner**") == 1);
    CHECK(countSubstring(content, "Generated signals: `2`") == 1);
    CHECK(countSubstring(content, "Driven via port from `leaf memory`") == 1);
}

TEST_CASE("HoverExplicitPortShowsInputDriver") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module leaf(input logic [7:0] data);
endmodule

module top;
    logic [7:0] value;
    leaf u(.data(value));
endmodule
)");

    auto cursor = doc.after("u(.");
    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CAPTURE(content);
    CHECK(content.find("input logic [7:0] data") != std::string::npos);
    CHECK(content.find("Driven via port from `leaf u`") != std::string::npos);
}

TEST_CASE("HoverOutputConnectionsShowInnerDriver") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module implicit_leaf(output logic implicit_value);
    always_comb implicit_value = 1'b1;
endmodule

module explicit_leaf(output logic data);
    always_comb data = 1'b1;
endmodule

module top(
    output logic implicit_value,
    output logic explicit_value
);
    implicit_leaf implicit_u(.implicit_value);
    explicit_leaf explicit_u(.data(explicit_value));
endmodule
)");

    auto implicitCursor = doc.after("implicit_u(.");
    auto implicitHover = doc.getHoverAt(implicitCursor.m_offset);
    REQUIRE(implicitHover);
    auto implicitContent = rfl::get<lsp::MarkupContent>(implicitHover->contents).value;
    CAPTURE(implicitContent);
    CHECK(implicitContent.find("Driven via port from `implicit_leaf implicit_u`") !=
          std::string::npos);
    CHECK(implicitContent.find("Driven by always_comb") != std::string::npos);

    auto explicitCursor = doc.after(".data(");
    auto explicitHover = doc.getHoverAt(explicitCursor.m_offset);
    REQUIRE(explicitHover);
    auto explicitContent = rfl::get<lsp::MarkupContent>(explicitHover->contents).value;
    CAPTURE(explicitContent);
    CHECK(explicitContent.find("Driven via port from `explicit_leaf explicit_u`") !=
          std::string::npos);
    CHECK(explicitContent.find("Driven by always_comb") != std::string::npos);

    auto declarationCursor = doc.after("output logic explicit_");
    auto declarationHover = doc.getHoverAt(declarationCursor.m_offset);
    REQUIRE(declarationHover);
    auto declarationContent = rfl::get<lsp::MarkupContent>(declarationHover->contents).value;
    CAPTURE(declarationContent);
    CHECK(declarationContent.find("Driven via port from `explicit_leaf explicit_u`") !=
          std::string::npos);
    CHECK(declarationContent.find("Driven by always_comb") != std::string::npos);
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

TEST_CASE("Hover - Modport - Continuous Assignment") {
    ServerHarness server;
    GoldenTest golden;

    auto doc = server.openFile("test.sv", R"(
interface interrupt_if;
    logic hblank_req;
    modport PPU_side(output hblank_req);
endinterface

module PPU(interrupt_if.PPU_side interrupt_bus, interrupt_if.PPU_side interrupt_bus2);
    logic [8:0] scan_x;
    logic cycle;
    assign interrupt_bus.hblank_req = (scan_x == 240 && cycle == 0);
    assign interrupt_bus2.hblank_req = (scan_x == 240 && cycle == 0);
endmodule

module top;
    interrupt_if interrupt_bus();
    PPU ppu(interrupt_bus);
endmodule
)");

    auto cursor = doc.before("hblank_req);");
    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    normalizeHoverOutput(content, doc);
    golden.record(content);
    golden.record("\n");
}

TEST_CASE("Hover - Modport - AlwaysFF Assignment") {
    ServerHarness server;
    GoldenTest golden;

    auto doc = server.openFile("test.sv", R"(
interface interrupt_if;
    logic hblank_req;
    modport PPU_side(output hblank_req);
endinterface

module PPU(interrupt_if.PPU_side interrupt_bus);
    logic [8:0] scan_x;
    logic cycle;
    always_ff @(posedge cycle) interrupt_bus.hblank_req <= (scan_x == 240 && cycle == 0);
endmodule

module top;
    interrupt_if interrupt_bus();
    PPU ppu(interrupt_bus);
endmodule
)");

    auto cursor = doc.before("hblank_req);");
    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    normalizeHoverOutput(content, doc);
    golden.record(content);
    golden.record("\n");
}

TEST_CASE("Hover - Plain Port Driver") {
    ServerHarness server;
    GoldenTest golden;

    auto doc = server.openFile("test.sv", R"(
module child(output logic data);
endmodule

module top;
    logic value;
    child u(.data(value));
endmodule
)");

    auto cursor = doc.before("value;");
    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    normalizeHoverOutput(content, doc);
    golden.record(content);
    golden.record("\n");
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

TEST_CASE("HoverGeneratedSymbolsSummarizesValues") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module top;
    for (genvar i = 0; i < 3; i++) begin : g
        localparam int VALUE = i;
        localparam int EVEN = i * 2 + 2;
        localparam int SAME = 1;
        logic [VALUE:0] data;
    end
endmodule
)");

    auto checkValueRange = [&](Cursor cursor) {
        auto info = doc.getDefinitionInfoAt(cursor.m_offset);
        REQUIRE(info);
        CHECK(info->targets.size() == 3);

        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
        CHECK(content.find("Value range: `0` through `2`") != std::string::npos);
        CHECK(content.find("Value: `0`") == std::string::npos);
        CHECK(countSubstring(content, "**Parameter** `VALUE`") == 1);
        CHECK(countSubstring(content, "Type: `int`") == 1);

        // All elaborated symbols share the same source declaration.
        CHECK(cursor.getDefinitions().size() == 1);
    };

    checkValueRange(doc.before("VALUE ="));
    checkValueRange(doc.before("VALUE:0"));

    auto evenHover = doc.getHoverAt(doc.before("EVEN =").m_offset);
    REQUIRE(evenHover);
    auto evenContent = rfl::get<lsp::MarkupContent>(evenHover->contents).value;
    CHECK(evenContent.find("Values: `2`, `4`, `6`") != std::string::npos);
    CHECK(evenContent.find("Value range:") == std::string::npos);
    CHECK(countSubstring(evenContent, "**Parameter** `EVEN`") == 1);

    auto sameHover = doc.getHoverAt(doc.before("SAME =").m_offset);
    REQUIRE(sameHover);
    auto sameContent = rfl::get<lsp::MarkupContent>(sameHover->contents).value;
    CHECK(countSubstring(sameContent, "**Parameter** `SAME`") == 1);
    CHECK(countSubstring(sameContent, "Value: `1`") == 1);

    auto checkGenvarRange = [&](Cursor cursor, size_t targetCount,
                                slang::ast::SymbolKind primaryKind) {
        auto info = doc.getDefinitionInfoAt(cursor.m_offset);
        REQUIRE(info);
        CHECK(info->targets.size() == targetCount);
        REQUIRE(info->symbol());
        CHECK(info->symbol()->kind == primaryKind);

        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
        CAPTURE(content);
        CHECK(content.find("Value range: `0` through `2`") != std::string::npos);
        CHECK(content.find("Value: `0`") == std::string::npos);
    };

    checkGenvarRange(doc.after("genvar "), 4, slang::ast::SymbolKind::Genvar);
    checkGenvarRange(doc.before("i < 3"), 4, slang::ast::SymbolKind::Genvar);
    checkGenvarRange(doc.before("i++)"), 4, slang::ast::SymbolKind::Genvar);
    checkGenvarRange(doc.after("VALUE = "), 3, slang::ast::SymbolKind::Parameter);
}

TEST_CASE("HoverNestedGeneratedSymbolsDeduplicatesEquivalentElaborations") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module producer(output logic [8:0] exponent_out);
    always_comb exponent_out = '0;
endmodule

module top;
    for (genvar unit_index = 0; unit_index < 2; unit_index++) begin : units
        for (genvar row = 0; row < 2; row++) begin : rows
            localparam int ROW_INDEX = row;
            logic [8:0] math_out_exponent;
            logic [row:0] varying_width;
            producer math(.exponent_out(math_out_exponent));
        end
    end
endmodule
)");

    auto rowHeader = doc.before("row = 0; row < 2");
    auto rowHeaderInfo = doc.getDefinitionInfoAt(rowHeader.m_offset);
    REQUIRE(rowHeaderInfo);
    REQUIRE(rowHeaderInfo->targets.size() == 3);
    REQUIRE(rowHeaderInfo->symbol());
    CHECK(rowHeaderInfo->symbol()->kind == slang::ast::SymbolKind::Genvar);
    auto rowHeaderHover = doc.getHoverAt(rowHeader.m_offset);
    REQUIRE(rowHeaderHover);
    auto rowHeaderContent = rfl::get<lsp::MarkupContent>(rowHeaderHover->contents).value;
    CAPTURE(rowHeaderContent);
    CHECK(countSubstring(rowHeaderContent, "**Genvar** `row`") == 1);
    CHECK(countSubstring(rowHeaderContent, "Value range: `0` through `1`") == 1);

    auto row = doc.after("ROW_INDEX = ");
    auto rowInfo = doc.getDefinitionInfoAt(row.m_offset);
    REQUIRE(rowInfo);
    REQUIRE(rowInfo->targets.size() == 2);
    auto rowHover = doc.getHoverAt(row.m_offset);
    REQUIRE(rowHover);
    auto rowContent = rfl::get<lsp::MarkupContent>(rowHover->contents).value;
    CAPTURE(rowContent);
    CHECK(countSubstring(rowContent, "````systemverilog\nrow\n````") == 1);

    auto rowIndexHover = doc.getHoverAt(doc.before("ROW_INDEX =").m_offset);
    REQUIRE(rowIndexHover);
    auto rowIndexContent = rfl::get<lsp::MarkupContent>(rowIndexHover->contents).value;
    CAPTURE(rowIndexContent);
    CHECK(rowIndexContent.find("Value range: `0` through `1`") != std::string::npos);
    CHECK(countSubstring(rowIndexContent, "**Parameter** `ROW_INDEX`") == 1);

    auto value = doc.before("math_out_exponent;");
    auto valueInfo = doc.getDefinitionInfoAt(value.m_offset);
    REQUIRE(valueInfo);
    REQUIRE(valueInfo->targets.size() == 1);
    auto valueHover = doc.getHoverAt(value.m_offset);
    REQUIRE(valueHover);
    auto valueContent = rfl::get<lsp::MarkupContent>(valueHover->contents).value;
    CAPTURE(valueContent);
    CHECK(countSubstring(valueContent, "**Variable** `math_out_exponent`") == 1);
    CHECK(countSubstring(valueContent, "Generated signals: `4`") == 1);
    CHECK(countSubstring(valueContent, "Driven via port from `producer math`") == 1);
    CHECK(countSubstring(valueContent, "Driven by always_comb") == 1);

    auto varying = doc.before("varying_width;");
    auto varyingInfo = doc.getDefinitionInfoAt(varying.m_offset);
    REQUIRE(varyingInfo);
    REQUIRE(varyingInfo->targets.size() == 2);
    auto varyingHover = doc.getHoverAt(varying.m_offset);
    REQUIRE(varyingHover);
    auto varyingContent = rfl::get<lsp::MarkupContent>(varyingHover->contents).value;
    CAPTURE(varyingContent);
    CHECK(countSubstring(varyingContent, "**Variable** `varying_width`") == 2);
    CHECK(countSubstring(varyingContent, "Generated signals: `2`") == 2);
}

TEST_CASE("HoverReusedGenvarKeepsLoopValuesSeparate") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module top;
    genvar i;
    for (i = 0; i < 2; i++) begin : first
    end
    for (i = 4; i < 6; i++) begin : second
    end
endmodule
)");

    auto checkRange = [&](Cursor cursor, std::string_view expected) {
        auto info = doc.getDefinitionInfoAt(cursor.m_offset);
        REQUIRE(info);
        REQUIRE(info->targets.size() == 3);
        REQUIRE(info->symbol());
        CHECK(info->symbol()->kind == slang::ast::SymbolKind::Genvar);

        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
        CAPTURE(content);
        CHECK(content.find(expected) != std::string::npos);
    };

    checkRange(doc.before("i = 0"), "Value range: `0` through `1`");
    checkRange(doc.before("i = 4"), "Value range: `4` through `5`");
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
