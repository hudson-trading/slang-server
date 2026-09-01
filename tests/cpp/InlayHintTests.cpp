// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/GoldenTest.h"
#include "utils/InlayHintScanner.h"
#include "utils/ServerHarness.h"
#include <algorithm>
#include <array>

using namespace slang;

TEST_CASE("InlayHintsAll") {
    /// Test inlay hints on the comprehensive all.sv test file
    ServerHarness server("");
    auto hdl = server.openFile("all.sv");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsFunction") {
    /// Test inlay hints for function call arguments
    ServerHarness server("");
    auto hdl = server.openFile("inlay_function.sv", R"(
module test;
    function int add(int a, int b);
        return a + b;
    endfunction

    initial begin
        int x = add(5, 10);
    end
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsFunctionMacroArg") {
    /// Test function argument hints use the original source location for macro arguments
    ServerHarness server("");
    auto hdl = server.openFile("inlay_function_macro_arg.sv", R"(
`define ID(x) x

module test;
    function int add(int a, int b);
        return a + b;
    endfunction

    initial begin
        int x = add(`ID(5), 10);
    end
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsModuleOrdered") {
    /// Test inlay hints for module instantiation with ordered ports
    ServerHarness server("");
    auto hdl = server.openFile("inlay_module_ordered.sv", R"(
module adder(
    input logic clk,
    input logic [7:0] a,
    input logic [7:0] b,
    output logic [8:0] sum
);
endmodule

module top;
    logic clk, a, b, sum;
    adder u_adder(clk, a, b, sum);
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsModuleNamed") {
    /// Test inlay hints for module instantiation with named ports
    ServerHarness server("");
    auto hdl = server.openFile("inlay_module_named.sv", R"(
module counter(
    input logic clk,
    input logic rst,
    output logic [7:0] count
);
endmodule

module top;
    logic clk, rst;
    logic [7:0] cnt;
    counter u_cnt(.clk(clk), .rst(rst), .count(cnt));

    counter x_cnt(
        .clk  (clk),
        .rst  (rst),
        .count(cnt)
    );
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsWildcard") {
    /// Test inlay hints for wildcard port connections
    ServerHarness server("");
    auto hdl = server.openFile("inlay_wildcard.sv", R"(
module receiver(
    input logic clk,
    input logic [7:0] data
);
endmodule

module top;
    logic clk;
    logic [7:0] data;
    receiver u_rx(.*);
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsLineDirectivesUseBufferPositions") {
    ServerHarness server("");
    auto hdl = server.openFile("inlay_line_directive.sv", R"(module receiver(
    input logic clk
);
endmodule

module top;
    logic clk;
    receiver u_rx(
`line 8 "mapped.sv" 0
        .*
    );
endmodule
)");

    auto hints = hdl.getAllInlayHints();
    auto hint = std::ranges::find_if(hints, [](const auto& item) {
        return item.textEdits && !item.textEdits->empty();
    });
    REQUIRE(hint != hints.end());
    CHECK(hint->position.line == 9);

    const auto& edit = hint->textEdits->front();
    CHECK(edit.range.start.line == 9);
    CHECK_FALSE(edit.newText.starts_with('\n'));
}

TEST_CASE("InlayHintsAssignmentPatternTypes") {
    ServerHarness server("");
    auto hdl = server.openFile("inlay_assignment_patterns.sv", R"(
package Pkg;
    typedef logic [missing_width - 1:0] incomplete_t;
    typedef struct packed {
        incomplete_t payload;
        logic valid;
    } packet_t;
    typedef struct packed {
        packet_t packet;
        logic ready;
    } wrapper_t;
endpackage

module top;
    Pkg::packet_t packet;
    Pkg::wrapper_t wrapper;
    wire Pkg::packet_t packet_wire;
    logic source;
    Pkg::packet_t initialized = '{payload: source, valid: source};
    assign packet_wire = '{payload: source, valid: source};

    function automatic void consume(Pkg::packet_t value);
    endfunction

    initial begin
        packet = '{payload: source, valid: source};
        wrapper = '{packet: '{payload: source, valid: source}, ready: source};
        packet = Pkg::packet_t'{payload: source, valid: source};
        consume('{payload: source, valid: source});
    end
endmodule
)");

    auto hints = hdl.getAllInlayHints();
    REQUIRE(hints.size() == 6);

    std::array expectedLabels{"Pkg::packet_t",  "Pkg::packet_t", "Pkg::packet_t",
                              "Pkg::wrapper_t", "Pkg::packet_t", "Pkg::packet_t"};
    auto packetType = hdl.before("packet_t;");
    auto wrapperType = hdl.before("wrapper_t;");
    std::array expectedTypePositions{packetType.getPosition(), packetType.getPosition(),
                                     packetType.getPosition(), wrapperType.getPosition(),
                                     packetType.getPosition(), packetType.getPosition()};
    for (size_t i = 0; i < hints.size(); i++) {
        REQUIRE(rfl::holds_alternative<std::vector<lsp::InlayHintLabelPart>>(hints[i].label));
        const auto& labelParts = rfl::get<std::vector<lsp::InlayHintLabelPart>>(hints[i].label);
        REQUIRE(labelParts.size() == 1);
        CHECK(labelParts[0].value == expectedLabels[i]);
        REQUIRE(labelParts[0].location);
        CHECK(labelParts[0].location->uri == hdl.doc->getURI());
        CHECK(labelParts[0].location->range.start == expectedTypePositions[i]);
        auto expectedEnd = expectedTypePositions[i];
        expectedEnd.character += i == 3 ? std::string_view("wrapper_t").size()
                                        : std::string_view("packet_t").size();
        CHECK(labelParts[0].location->range.end == expectedEnd);
        CHECK(hints[i].kind == lsp::InlayHintKind::Type);
        REQUIRE(hints[i].textEdits);
        REQUIRE(hints[i].textEdits->size() == 1);
        auto& edit = hints[i].textEdits->front();
        CHECK(edit.range.start == hints[i].position);
        CHECK(edit.range.end == hints[i].position);
        CHECK(edit.newText == expectedLabels[i]);
    }

    auto typeHover = hdl.getHoverAt(packetType.m_offset);
    REQUIRE(typeHover);
    auto hoverContent = rfl::get<lsp::MarkupContent>(typeHover->contents).value;
    CHECK(hoverContent.find("**TypeAlias** `packet_t`") != std::string::npos);

    auto typeDefinitions = packetType.getDefinitions();
    REQUIRE(typeDefinitions.size() == 1);
    CHECK(typeDefinitions[0].targetSelectionRange.start == packetType.getPosition());

    auto initializedPattern = hdl.after("initialized = ").getPosition();
    auto continuousPattern = hdl.after("packet_wire = ").getPosition();
    auto assignedPattern = hdl.after("packet = ").getPosition();
    auto outerPattern = hdl.after("wrapper = ").getPosition();
    auto nestedPattern = hdl.after("wrapper = '").after("packet: ").getPosition();
    auto argumentPattern = hdl.after("initial begin").after("consume(").getPosition();
    CHECK(hints[0].position == initializedPattern);
    CHECK(hints[1].position == continuousPattern);
    CHECK(hints[2].position == assignedPattern);
    CHECK(hints[3].position == outerPattern);
    CHECK(hints[4].position == nestedPattern);
    CHECK(hints[5].position == argumentPattern);
}

TEST_CASE("InlayHintsAndHoverUseResolvedInterfaceType") {
    ServerHarness server("");
    auto hdl = server.openFile("inlay_interface_assignment_pattern.sv", R"(
typedef struct packed {
    logic [7:0] first;
    logic [7:0] second;
} item_t;

interface test_if #(
    parameter type T = item_t
);
    T value;
endinterface

module top;
    test_if #(.T(item_t)) a();
    test_if b();

    initial begin
        a.value = '{first: '0, second: '0};
        b.value = '{first: '0, second: '0};
    end
endmodule
)");

    auto hints = hdl.getAllInlayHints();
    REQUIRE(hints.size() == 2);
    for (const auto& hint : hints) {
        REQUIRE(rfl::holds_alternative<std::vector<lsp::InlayHintLabelPart>>(hint.label));
        const auto& labelParts = rfl::get<std::vector<lsp::InlayHintLabelPart>>(hint.label);
        REQUIRE(labelParts.size() == 1);
        CHECK(labelParts[0].value == "item_t");
        REQUIRE(labelParts[0].location);
        CHECK(labelParts[0].location->range.start == hdl.before("item_t;").getPosition());
        REQUIRE(hint.textEdits);
        REQUIRE(hint.textEdits->size() == 1);
        CHECK(hint.textEdits->front().newText == "item_t");
    }

    for (auto cursor : {hdl.after("a."), hdl.after("b.")}) {
        auto hover = hdl.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
        CAPTURE(content);
        CHECK(content.find("Type: [`PackedStruct T (aka item_t) (logic[15:0])`]") !=
              std::string::npos);
        CHECK(content.find("#L5,3") != std::string::npos);
        CHECK(content.find("#L8,20") == std::string::npos);
    }
}

TEST_CASE("InlayHintsParameters") {
    /// Test inlay hints for parameter assignments
    ServerHarness server("");
    auto hdl = server.openFile("inlay_params.sv", R"(
module fifo #(
    parameter int DEPTH = 16,
    parameter int WIDTH = 8
)(
    input logic clk
);
endmodule

module top;
    logic clk;
    fifo #(32, 16) u_fifo(clk);
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsWildcardExpansion") {
    /// Test applying text edits from wildcard expansion
    ServerHarness server("");
    auto hdl = server.openFile("inlay_wildcard_expand.sv", R"(
module receiver(
    input logic clk,
    input logic [7:0] data
);
endmodule

module top;
    logic clk;
    logic [7:0] data;
    receiver u_rx(.*);
endmodule
)");

    auto hints = hdl.getAllInlayHints();

    // Collect all text edits from hints
    std::vector<lsp::TextEdit> edits;
    for (const auto& hint : hints) {
        if (hint.textEdits) {
            for (const auto& edit : *hint.textEdits) {
                edits.push_back(edit);
            }
        }
    }

    // Apply edits and check result
    auto result = hdl.withTextEdits(edits);

    GoldenTest test;
    test.record(result);
}

TEST_CASE("InlayHintsWildcardMultiple") {
    /// Test applying text edits from multiple wildcard expansions
    ServerHarness server("");
    auto hdl = server.openFile("inlay_wildcard_multi.sv", R"(
module dut(
    input logic clk,
    input logic rst,
    input logic [7:0] data_in,
    output logic [7:0] data_out
);
endmodule

module top;
    logic clk, rst;
    logic [7:0] data_in, data_out;

    dut u_dut1(.*);

    dut u_dut2(.*);

    dut u_dut3(
        .*
    );
endmodule
)");

    auto hints = hdl.getAllInlayHints();

    // Collect all text edits from hints
    std::vector<lsp::TextEdit> edits;
    for (const auto& hint : hints) {
        if (hint.textEdits) {
            for (const auto& edit : *hint.textEdits) {
                edits.push_back(edit);
            }
        }
    }

    // Apply edits and check result
    auto result = hdl.withTextEdits(edits);

    GoldenTest test;
    test.record(result);
}

TEST_CASE("InlayHintsInstanceArray") {
    /// Test inlay hints for module instance arrays with ordered ports
    ServerHarness server("");
    auto hdl = server.openFile("inlay_instance_array.sv", R"(
module adder(
    input logic clk,
    input logic [7:0] a,
    input logic [7:0] b,
    output logic [8:0] sum
);
endmodule

module top;
    logic clk;
    logic [7:0] a[0:3], b[0:3];
    logic [8:0] sum[0:3];
    adder u_adder[0:3](clk, a, b, sum);
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsClassTypedefOrdered") {
    /// Test inlay hints for typedef'd class with parameter overrides and ordered constructor
    /// parameters
    ServerHarness server("");
    auto hdl = server.openFile("inlay_class_typedef.sv", R"(
class packet #(int WIDTH = 8, int MAX_SIZE = 512);
    function new(int id, int size, bit[WIDTH-1:0] data);
    endfunction
endclass

typedef packet #(16, 1024) my_packet_t;

module top;
    initial begin
        my_packet_t pkt = new(42, 256, 16'hABCD);
    end
endmodule
)");
    // TODO: inlay hints and gotos on 'super' and 'new' keywords

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsTooManyArgs") {
    /// Test inlay hints for function calls and macro calls with too many arguments
    ServerHarness server("");
    auto hdl = server.openFile("inlay_too_many_args.sv", R"(
`define MY_MACRO(a, b) (a + b)

module test;
    function int add(int a, int b);
        return a + b;
    endfunction

    initial begin
        // Function call with too many arguments
        int x = add(5, 10, 15);

        // Macro call with too many arguments
        int y = `MY_MACRO(3, 4, 5);
    end
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsNullPtrEdgeCases") {
    /// Test inlay hints with various edge cases that could trigger null pointer issues
    ServerHarness server("");
    auto hdl = server.openFile("inlay_null_ptr_edge_cases.sv", R"(
module adder #(
    parameter int WIDTH = 8,
    parameter int DEPTH = 16
)(
    input logic clk,
    input logic [WIDTH-1:0] a,
    input logic [WIDTH-1:0] b,
    output logic [WIDTH:0] sum
);
endmodule

module top;
    logic clk, a, b, sum;

    // Too many parameters - tests bounds checking
    adder #(8, 16, 32, 64) u_adder1(clk, a, b, sum);

    // Too many ports - tests bounds checking
    adder u_adder2(clk, a, b, sum, 1'b0, 1'b1);

    // Named ports with potential null syntax
    adder u_adder3(
        .clk(clk),
        .a(a),
        .b(b),
        .sum(sum)
    );
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsMalformedSyntax") {
    /// Test inlay hints with malformed syntax that has parse errors
    ServerHarness server("");
    auto hdl = server.openFile("inlay_malformed.sv", R"(
module broken;
    // This will have parse errors but shouldn't crash
    logic clk;

    // Missing module definition
    undefined_mod inst();

    // Function with missing args
    function int broken_func();
        return 0;
    endfunction

    initial begin
        // Function call - tests null left expression handling
        int x = broken_func();
    end
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsClassParameterEdgeCases") {
    /// Test inlay hints for classes with edge cases
    ServerHarness server("");
    auto hdl = server.openFile("inlay_class_edge_cases.sv", R"(
class packet #(int WIDTH = 8);
    function new(int id);
    endfunction
endclass

// Non-generic class (no parameters)
class simple_packet;
    function new(int id);
    endfunction
endclass

module top;
    initial begin
        // Too many parameters - tests bounds checking
        packet #(8, 16, 32) pkt1 = new(1);

        // Simple class without parameters - tests null parameters check
        simple_packet pkt2 = new(2);
    end
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsEmptyPorts") {
    /// Test inlay hints with modules that have no ports
    ServerHarness server("");
    auto hdl = server.openFile("inlay_empty_ports.sv", R"(
module no_ports;
    // Module with no ports
endmodule

module has_ports(input logic clk);
endmodule

module top;
    logic clk;

    // Instance with no ports - should handle empty port list
    no_ports u_empty();

    // Instance with too many connections to a 1-port module
    has_ports u_ports(clk, clk, clk);
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsFunction_Same") {
    /// Test inlay hints for function call arguments
    ServerHarness server("");
    auto hdl = server.openFile("inlay_function_same.sv", R"(
module test;
    function int add(int a, int b);
        return a + b;
    endfunction

    initial begin
        int a = 5;
        int b = 10;
        int x = add(a, b);
    end
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("InlayHintsParameters_Same") {
    /// Test inlay hints for parameter assignments
    ServerHarness server("");
    auto hdl = server.openFile("inlay_params.sv", R"(
module fifo #(
    parameter int DEPTH = 16,
    parameter int WIDTH = 8
)(
    input logic clk
);
endmodule

module top;
    logic clk;
    localparam int DEPTH = 32;
    localparam int WIDTH = 16;
    fifo #(DEPTH, WIDTH) u_fifo(clk);
endmodule
)");

    InlayHintScanner scanner;
    scanner.scanDocument(hdl);
}
