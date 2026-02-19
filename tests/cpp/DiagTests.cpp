// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"
#include <cstdlib>

TEST_CASE("SingleFileDiag") {
    ServerHarness server;

    JsonGoldenTest golden;
    auto doc = server.openFile("blargh.sv", R"(
module top;
    localparam int x = 5;
    // trigger a warning that has a code
    localparam bit y = x;
    blargh
endmodule
)");

    golden.record(doc.getDiagnostics());
}

TEST_CASE("IncludedFileDiag") {
    ServerHarness server;

    JsonGoldenTest golden;
    auto header = server.openFile("blargh.svh", "`define BLARGH(foo) foo\n");
    auto doc = server.openFile("blargh.sv", R"(
`include "blargh.svh"
module top;
    `BLARGH(blargh)
    `BLARGH(blargh2)
endmodule
)");

    golden.record(doc.getDiagnostics());
}

TEST_CASE("SyntaxOnlyOnChange") {
    ServerHarness server;

    JsonGoldenTest golden;
    auto header = server.openFile("blargh.svh", "`define BLARGH(foo) foo\n");
    auto doc = server.openFile("blargh.sv", R"(
`include "blargh.svh"
module top;
    `BLARGH(blargh)
endmodule
)");
    doc.after("(blargh)").write("\n`BLARGH(blargh2)");
    doc.publishChanges();
    golden.record("afterChange", doc.getDiagnostics());
    doc.save();
    golden.record("afterSave", doc.getDiagnostics());
}

TEST_CASE("AllGenerateBranches") {
    ServerHarness server;

    JsonGoldenTest golden;

    // Check that we have diags on all the branches
    // The conditional blocks are named the same on purpose so that we can check that
    // there aren't diags for that
    auto doc = server.openFile("test.sv", R"(
        module x #(
            parameter int p = 1
        );

        endmodule

        module y;
            localparam int cond = 1;
            localparam int n_loops = 0;

            if (cond) begin : gen_cond
                x #(.p1(1)) x1(.port1(1));
            end else begin : gen_else
                x #(.p2(1)) x2(.port2(1));
            end

            for(genvar i = 0; i < n_loops; i++) begin : gen_loop
                x #(.p3(i)) x3(.port3(i));
            end

        endmodule
        )");
    auto diags = doc.getDiagnostics();
    golden.record(diags);
}

TEST_CASE("NoParamTop") {
    ServerHarness server;

    auto hdl = server.openFile("tests/data/hdl_test.sv");

    JsonGoldenTest golden(true);
    golden.record(hdl.getDiagnostics());
}

TEST_CASE("PartialElaboration") {
    ServerHarness server;

    JsonGoldenTest golden(true);

    // Check that we can reason about diagnostics without having to fully elaborate
    auto doc = server.openFile("test.sv", R"(
        module x #(
            parameter int x = 1,
            parameter int y = 2,
            parameter int z
        );
            $static_assert(y == x);
        endmodule


        )");
    auto diags = doc.getDiagnostics();
    golden.record(diags);
}

TEST_CASE("RecursiveModuleRegression") {
    ServerHarness server;

    auto doc = server.openFile("recursive.sv", R"(
module Nbitaddr #(parameter N = 8) (
    input  logic [N-1:0] a,
    input  logic [N-1:0] b,
    input  logic         cin,
    output logic [N-1:0] sum,
    output logic         cout
);
    logic carry_mid;
    Nbitaddr #(.N(N/2)) lo (
        .a(a[N/2-1:0]),
        .b(b[N/2-1:0]),
        .cin(cin),
        .sum(sum[N/2-1:0]),
        .cout(carry_mid)
    );
    Nbitaddr #(.N(N - N/2)) hi (
        .a(a[N-1:N/2]),
        .b(b[N-1:N/2]),
        .cin(carry_mid),
        .sum(sum[N-1:N/2]),
        .cout(cout)
    );
endmodule

module Top #()();
    Nbitaddr #(.N(8)) u_addr (
    .a(8'hFF),
    .b(8'h01),
    .cin(1'b0),
    .sum(),
    .cout()
);

)");

    CHECK(doc.getDiagnostics().size() > 0);
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

TEST_CASE("CompilationDiagnostics") {
    ServerHarness server("comp_repo");

    server.setBuildFile("cpu_design.f");

    JsonGoldenTest golden;

    // Get URIs for the files
    auto cpuUri = URI::fromFile(fs::current_path() / "cpu.sv");
    auto aluUri = URI::fromFile(fs::current_path() / "alu.sv");
    auto memUri = URI::fromFile(fs::current_path() / "memory_controller.sv");

    // Get diagnostics from the client (published diagnostics)
    auto cpuClientDiags = server.client.getDiagnostics(cpuUri);
    auto aluClientDiags = server.client.getDiagnostics(aluUri);
    auto memClientDiags = server.client.getDiagnostics(memUri);

    golden.record("cpu_client_diags", cpuClientDiags);

    // Assert that at least one file has diagnostics
    CHECK(cpuClientDiags.size() > 0);

    // Open the files
    auto cpu = server.openFile("cpu.sv");
    auto alu = server.openFile("alu.sv");
    auto mem = server.openFile("memory_controller.sv");

    // Get diagnostics from the document handles
    auto cpuDocDiags = cpu.getDiagnostics();
    auto aluDocDiags = alu.getDiagnostics();
    auto memDocDiags = mem.getDiagnostics();
    golden.record("cpuDocDiags", cpuDocDiags);

    // Verify that client diagnostics match original document diagnostics (no duplicates)
    CHECK(cpuClientDiags.size() == cpuDocDiags.size());
    CHECK(aluClientDiags.size() == aluDocDiags.size());
    CHECK(memClientDiags.size() == memDocDiags.size());
}
