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

TEST_CASE("DiagsPublishedOnOpenCachedDoc") {
    ServerHarness server("cached_dep");

    // Open top.sv first — this causes pkg.sv to be loaded as a dependency
    auto top = server.openFile("top.sv");
    auto topDiags = top.getDiagnostics();
    CHECK(!topDiags.empty());

    // Now open pkg.sv — it's already cached from being loaded as a dep,
    // so the fast path is taken, but diagnostics should still be published
    auto pkg = server.openFile("pkg.sv");
    auto pkgDiags = server.client.getDiagnostics(pkg.m_uri);
    CHECK(!pkgDiags.empty());
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

TEST_CASE("UntakenGenerateChecks") {
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

TEST_CASE("OpenBuildFileDoesNotOverwriteCompilationDiags") {
    ServerHarness server("comp_repo");
    server.setBuildFile("cpu_design.f");

    // Compilation diags are published before any file is opened
    auto cpuUri = URI::fromFile(fs::current_path() / "cpu.sv");
    auto compDiags = server.client.getDiagnostics(cpuUri);

    // Opening a file that's already in the compilation should not change diags
    auto cpu = server.openFile("cpu.sv");
    auto afterOpenDiags = server.client.getDiagnostics(cpuUri);

    CHECK(compDiags.size() == afterOpenDiags.size());

    // Diags should be identical, not just same count
    for (size_t i = 0; i < compDiags.size() && i < afterOpenDiags.size(); i++) {
        CHECK(compDiags[i].message == afterOpenDiags[i].message);
        CHECK(compDiags[i].range.start.line == afterOpenDiags[i].range.start.line);
    }
}

TEST_CASE("OpenNonBuildFileGetsShallowDiags") {
    ServerHarness server("comp_repo");
    server.setBuildFile("cpu_design.f");

    // Open a file that is NOT in the build file — should still get shallow diags
    auto doc = server.openFile("external.sv", R"(
module ext;
    logic [7:0] a;
    assign a = "bad";  // type mismatch
endmodule
)");

    auto diags = doc.getDiagnostics();
    // Should have some diagnostics from the shallow compilation
    CHECK(!diags.empty());
}

TEST_CASE("LintOffPragma") {
    ServerHarness server;

    auto hasCode = [](const std::vector<lsp::Diagnostic>& diags, std::string_view code) {
        for (auto& d : diags) {
            if (d.code && rfl::holds_alternative<std::string>(*d.code) &&
                rfl::get<std::string>(*d.code) == code)
                return true;
        }
        return false;
    };

    // Without lint_off: should have constant-conversion warning
    auto docWithWarning = server.openFile("with_warning.sv", R"(
module top;
    localparam int x = 5;
    localparam bit y = x;
endmodule
)");
    CHECK(hasCode(docWithWarning.getDiagnostics(), "constant-conversion"));

    // With lint_off: constant-conversion should be suppressed
    auto docSuppressed = server.openFile("suppressed.sv", R"(
module top2;
    localparam int x = 5;
    // slang lint_off constant-conversion
    localparam bit y = x;
    // slang lint_on constant-conversion
endmodule
)");
    CHECK_FALSE(hasCode(docSuppressed.getDiagnostics(), "constant-conversion"));

    // With setTopLevel (full compilation path): lint_off should still work
    auto docCompilation = server.openFile("comp_suppressed.sv", R"(
module top3;
    localparam int x = 5;
    // slang lint_off constant-conversion
    localparam bit y = x;
    // slang lint_on constant-conversion
endmodule
)");
    server.setTopLevel(std::string{docCompilation.m_uri.getPath()});
    CHECK_FALSE(hasCode(docCompilation.getDiagnostics(), "constant-conversion"));
}

TEST_CASE("RangeOOBSuppressedInUntakenGenerate") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module test;
  parameter int WIDTH = 4;
  logic [WIDTH-1:0] data_i;
  logic data_o[WIDTH][WIDTH];

  generate
    for (genvar i = 0; i < WIDTH; i++) begin : g_i
      for (genvar j = 0; j < WIDTH; j++) begin : g_j
        if (i == 0) begin : g_i_0
          assign data_o[i][j] = 1'b1;
        end
        else if (i + j < WIDTH) begin : g_st_width
          assign data_o[i][j] = data_i[i+j:j] == {i{1'b0}};
        end
        else begin : g_ge_width
          assign data_o[i][j] = 1'b0;
        end
      end
    end
  endgenerate
endmodule
)");

    auto diags = doc.getDiagnostics();
    for (auto& d : diags) {
        if (d.code) {
            auto& code = rfl::get<std::string>(*d.code);
            CHECK((code == "unused-def" || code == "unassigned-variable" ||
                   code == "unused-but-set-variable"));
        }
    }
}
