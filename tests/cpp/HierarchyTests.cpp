// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"

using namespace server;

TEST_CASE("GetScopeUnit") {
    ServerHarness server("comp_repo");
    JsonGoldenTest golden;

    server.setBuildFile("cpu_design.f");

    // Get the unit level scope (top modules)
    auto unitScope = server.getScope("");

    golden.record("unit_scope", unitScope);
}

TEST_CASE("GetScopeChildren") {
    ServerHarness server("comp_repo");
    JsonGoldenTest golden;

    server.setBuildFile("cpu_design.f");

    // Get the unit level first to find top module
    auto unitScope = server.getScope("");
    REQUIRE(unitScope.size() > 0);

    // Get children of the cpu_testbench module
    auto testbenchChildren = server.getScope("cpu_testbench");

    golden.record("testbench_children", testbenchChildren);

    // Get children of the cpu instance inside testbench
    auto cpuChildren = server.getScope("cpu_testbench.dut");

    golden.record("cpu_children", cpuChildren);
}

TEST_CASE("GetScopeNested") {
    ServerHarness server("comp_repo");
    JsonGoldenTest golden;

    server.setBuildFile("cpu_design.f");

    // Get nested scope - ALU instance inside CPU (via testbench.dut)
    auto aluScope = server.getScope("cpu_testbench.dut.alu_inst");

    golden.record("alu_scope", aluScope);

    // Get memory controller instance inside CPU
    auto memCtrlScope = server.getScope("cpu_testbench.dut.mem_ctrl");

    golden.record("mem_ctrl_scope", memCtrlScope);
}

TEST_CASE("GetScopesByModule") {
    ServerHarness server("comp_repo");
    JsonGoldenTest golden;

    server.setBuildFile("cpu_design.f");

    // Get all modules grouped by their instances
    auto scopesByModule = server.getScopesByModule({});

    golden.record("scopes_by_module", scopesByModule);
}

TEST_CASE("GetInstancesOfModule") {
    ServerHarness server("comp_repo");
    JsonGoldenTest golden;

    server.setBuildFile("cpu_design.f");

    // Get all instances of the ALU module
    auto aluInstances = server.getInstancesOfModule("alu");

    golden.record("alu_instances", aluInstances);

    // Get all instances of the memory_controller module
    auto memCtrlInstances = server.getInstancesOfModule("memory_controller");

    golden.record("memory_controller_instances", memCtrlInstances);

    // Get all instances of the CPU module (should be 1 - it's the top)
    auto cpuInstances = server.getInstancesOfModule("cpu");

    golden.record("cpu_instances", cpuInstances);
}

TEST_CASE("GetModulesInFile") {
    ServerHarness server("comp_repo");
    JsonGoldenTest golden;

    server.setBuildFile("cpu_design.f");

    // Get modules defined in cpu.sv
    auto cpuModules = server.getModulesInFile("cpu.sv");
    CHECK(!cpuModules.empty());
    golden.record("cpu_modules", cpuModules);

    // Get modules defined in alu.sv
    auto aluModules = server.getModulesInFile("alu.sv");

    golden.record("alu_modules", aluModules);

    // Get modules defined in memory_controller.sv
    auto memCtrlModules = server.getModulesInFile("memory_controller.sv");

    golden.record("memory_controller_modules", memCtrlModules);
}

TEST_CASE("GetFilesContainingModule") {
    ServerHarness server("comp_repo");

    server.setBuildFile("cpu_design.f");

    // Get files containing the CPU module
    auto cpuFiles = server.getFilesContainingModule("cpu");
    REQUIRE(!cpuFiles.empty());
    CHECK(cpuFiles[0].ends_with("cpu.sv"));

    // Get files containing the ALU module
    auto aluFiles = server.getFilesContainingModule("alu");
    REQUIRE(!aluFiles.empty());
    CHECK(aluFiles[0].ends_with("alu.sv"));

    // Get files containing the memory_controller module
    auto memCtrlFiles = server.getFilesContainingModule("memory_controller");
    REQUIRE(!memCtrlFiles.empty());
    CHECK(memCtrlFiles[0].ends_with("memory_controller.sv"));
}

TEST_CASE("HierarchicalViewIntegration") {
    ServerHarness server("comp_repo");
    JsonGoldenTest golden;

    server.setBuildFile("cpu_design.f");

    // Test a complete workflow similar to what the VSCode extension would do

    // 1. Get all modules grouped by instances
    auto modules = server.getScopesByModule({});
    golden.record("integration_modules", modules);

    // 2. Get the unit scope to see top-level modules
    auto unit = server.getScope("");
    golden.record("integration_unit", unit);

    // 3. Navigate into the CPU testbench module
    auto testbenchScope = server.getScope("cpu_testbench");
    golden.record("integration_testbench_scope", testbenchScope);

    // 4. Get instances of the ALU module
    auto aluInstances = server.getInstancesOfModule("alu");
    golden.record("integration_alu_instances", aluInstances);

    // 5. Navigate into a specific ALU instance
    auto aluInstanceScope = server.getScope("cpu_testbench.dut.alu_inst");
    golden.record("integration_alu_instance_scope", aluInstanceScope);
}

TEST_CASE("HierarchicalViewWithTopLevel") {
    ServerHarness server("comp_repo");
    JsonGoldenTest golden;

    // Test using setTopLevel instead of setBuildFile
    auto testbenchDoc = server.openFile("cpu_testbench.sv");
    server.setTopLevel(std::string{testbenchDoc.m_uri.getPath()});

    // Get the unit scope
    auto unitScope = server.getScope("");
    golden.record("toplevel_unit_scope", unitScope);

    // Get children of testbench
    auto testbenchChildren = server.getScope("cpu_testbench");
    golden.record("toplevel_testbench_children", testbenchChildren);

    // Get modules by file
    auto testbenchModules = server.getModulesInFile("cpu_testbench.sv");
    golden.record("toplevel_testbench_modules", testbenchModules);
}

TEST_CASE("HierarchicalViewEmptyResults") {
    ServerHarness server("comp_repo");

    server.setBuildFile("cpu_design.f");

    // Test querying non-existent paths and modules

    // Non-existent hierarchical path
    auto nonExistentPath = server.getScope("nonexistent.path.here");
    CHECK(nonExistentPath.empty());

    // Non-existent module name (expect error message)
    auto nonExistentModule = server.getInstancesOfModule("nonexistent_module");
    server.expectError("Module nonexistent_module not found");
    CHECK(nonExistentModule.empty());

    // Non-existent file
    auto nonExistentFile = server.getModulesInFile("nonexistent.sv");
    CHECK(nonExistentFile.empty());

    // Module not in workspace
    auto noFiles = server.getFilesContainingModule("nonexistent_module");
    CHECK(noFiles.empty());
}
