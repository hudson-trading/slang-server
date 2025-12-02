// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/GoldenTest.h"
#include "utils/InlayHintScanner.h"
#include "utils/ServerHarness.h"

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
