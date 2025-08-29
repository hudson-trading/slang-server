// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "lsp/LspTypes.h"
#include "utils/ServerHarness.h"
#include <cstdlib>

TEST_CASE("SetBuildFile") {
    ServerHarness server("comp_repo");

    server.setBuildFile("cpu_design.f");

    server.openFile("cpu.sv");

    auto scope = server.getScope("");

    CHECK(scope.size() > 0);
}

TEST_CASE("SetTopLevel") {
    ServerHarness server("comp_repo");

    auto hdl = server.openFile("cpu.sv");

    server.setTopLevel(std::string{hdl.m_uri.getPath()});

    auto scope = server.getScope("");

    CHECK(scope.size() > 0);
}

TEST_CASE("ServerStateSwitching") {
    ServerHarness server("comp_repo");

    auto hdl = server.openFile("cpu.sv");

    server.setTopLevel(std::string{hdl.m_uri.getPath()});
    CHECK(server.getScope("").size() > 0);

    server.setBuildFile("cpu_design.f");
    CHECK(server.getScope("").size() > 0);

    server.setBuildFile("");
    CHECK(server.getScope("").size() == 0);
}

TEST_CASE("ModifyOutOfCompilation") {
    ServerHarness server("comp_repo");
    server.setBuildFile("cpu_design.f");

    auto scope = server.getScope("");

    CHECK(scope.size() > 0);

    auto hdl = server.openFile("alu.sv");

    scope = server.getScope("");

    CHECK(scope.size() > 0);

    hdl.append("   ");

    auto tree = hdl.getSymbolTree();

    CHECK(tree.size() > 0);
}

TEST_CASE("SpamModifyCompilation") {
    ServerHarness server("comp_repo");
    server.loadConfig(Config{.indexGlobs = {std::vector<std::string>{"*.sv"}}});
    server.setBuildFile("cpu_design.f");

    auto scope = server.getScope("");
    CHECK(scope.size() > 0);

    // If file doesn't end in newline, buffer validation will fail
    auto hdl = server.openFile("memory_controller.sv");

    for (int i = 0; i < 10; i++) {
        hdl.insert(10, "   ");
        hdl.save();
    }
}

TEST_CASE("CompilationGotos") {
    ServerHarness server("comp_repo");

    // Open the CPU module and test goto definition for ALU instantiation
    auto hdl = server.openFile("cpu.sv");

    // Test goto definition in explore mode - look for ALU module reference
    auto cursor = hdl.after("alu_inst");
    lsp::DefinitionParams params{.textDocument = {.uri = hdl.m_uri},
                                 .position = cursor.getPosition()};

    // Should find the definition in explore mode
    CHECK(server.hasDefinition(params));

    // Set buildfile to get full compilation context with all modules
    server.setBuildFile("cpu_design.f");

    // Should still find the definition with buildfile (Open docs should be copied over, reparsed
    // with new options)
    CHECK(server.hasDefinition(params));

    // Unset buildfile (go back to explore mode)
    server.setBuildFile("");

    // Should still work in explore mode
    CHECK(server.hasDefinition(params));
}

TEST_CASE("CompilationDiags") {
    ServerHarness server("");

    // Create a test file with some intentional syntax/semantic errors
    auto testContent = R"(
module test_diag;
    logic clk;
    undeclared_type signal; // This should cause a diagnostic

    // Missing semicolon should cause syntax error
    logic reset

    // Using undeclared signal
    assign signal = unknown_signal;
endmodule
)";

    auto hdl = server.openFile("test_diag.sv", testContent);
    hdl.save();

    // Test diagnostics in explore mode
    auto exploreDiags = hdl.getDiagnostics();
    CHECK(exploreDiags.size() > 0); // Should have some diagnostics

    // Set buildfile to get full compilation context
    server.setBuildFile("test1.f");

    // Buildfile mode might clear syntax errors (this is expected behavior)
    // The important thing is that the diagnostic client transitions modes properly

    // Unset buildfile (go back to explore mode)
    server.setBuildFile("");

    // Test diagnostics after unsetting buildfile - should restore explore mode diagnostics
    auto postBuildDiags = hdl.getDiagnostics();
    CHECK(postBuildDiags.size() > 0); // Should have diagnostics restored in explore mode
}
