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

    JsonGoldenTest golden;
    golden.record(hdl.getDiagnostics());
}

TEST_CASE("PartialElaboration") {
    ServerHarness server;

    JsonGoldenTest golden;

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
