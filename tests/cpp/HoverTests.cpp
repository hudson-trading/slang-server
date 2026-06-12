// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"
#include <cstdlib>

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
    // Built-in system tasks ($display, $bits, etc.) should render their
    // documentation from the SystemTaskDocs table.
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module top;
    initial begin
        $display("hello");
        $finish;
    end
    localparam int W = $bits(int);
    localparam int L = $clog2(64);
endmodule
)");

    {
        auto cursor = doc.before("$display");
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover.has_value());
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        CHECK(content.value.find("System task") != std::string::npos);
        CHECK(content.value.find("$display") != std::string::npos);
        CHECK(content.value.find("§21.2.1") != std::string::npos);
    }

    {
        auto cursor = doc.before("$finish");
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover.has_value());
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        CHECK(content.value.find("$finish") != std::string::npos);
        CHECK(content.value.find("Halts") != std::string::npos);
    }

    {
        auto cursor = doc.before("$bits(int)");
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover.has_value());
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        CHECK(content.value.find("$bits") != std::string::npos);
        CHECK(content.value.find("§20.6.2") != std::string::npos);
        CHECK(content.value.find("number of bits") != std::string::npos);
    }

    {
        auto cursor = doc.before("$clog2");
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover.has_value());
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        CHECK(content.value.find("$clog2") != std::string::npos);
        CHECK(content.value.find("ceiling of the base-2 logarithm") != std::string::npos);
    }
}

TEST_CASE("HoverSystemTask_FunctionVsTaskLabel") {
    // The hover header distinguishes "System task" from "System function" based
    // on the SystemSubroutine kind, so users see accurate vocabulary.
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module top;
    localparam int W = $bits(int);
    initial begin
        $finish;
    end
endmodule
)");

    {
        // $bits is a function
        auto cursor = doc.before("$bits");
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover.has_value());
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        CHECK(content.value.find("System function") != std::string::npos);
        CHECK(content.value.find("System task") == std::string::npos);
    }

    {
        // $finish is a task
        auto cursor = doc.before("$finish");
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover.has_value());
        auto content = rfl::get<lsp::MarkupContent>(hover->contents);
        CHECK(content.value.find("System task") != std::string::npos);
        // Don't search for "System function" because the substring would be
        // present in "System functions" inside any longer description text.
    }
}

TEST_CASE("HoverSystemTask_NoFalsePositiveOnNonRegisteredDollarTokens") {
    // SystemVerilog has $-prefixed tokens that are not system subroutines:
    // `$root`, `$unit`, and the unbounded literal `$` (used as a queue size
    // `q[$]`, as an unbounded array index, or as a queue back-element
    // selector). These must not trigger system-task hover output — the
    // resolver path keys off `getSystemSubroutine(name)` returning non-null,
    // and these tokens correctly are not registered there.
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module top;
    int q[$];
    initial begin
        q.push_back(42);
        if (q[$] == 0) $display("first");
        if ($root.top.q.size()) $display("ok");
    end
endmodule
)");

    auto check = [&](const std::string& target) {
        auto cursor = doc.before(target);
        auto hover = doc.getHoverAt(cursor.m_offset);
        if (hover.has_value()) {
            auto content = rfl::get<lsp::MarkupContent>(hover->contents);
            CHECK(content.value.find("System task") == std::string::npos);
            CHECK(content.value.find("System function") == std::string::npos);
        }
    };

    // Unbounded `$` in queue declaration `int q[$]`.
    check("$];");
    // Unbounded `$` as queue back-element selector `q[$]`.
    check("$] ==");
    // `$root` hierarchical reference root.
    check("$root");
}
