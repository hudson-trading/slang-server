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

TEST_CASE("GetScopeBranches") {
    ServerHarness server("comp_repo");
    JsonGoldenTest golden;

    auto hdl = server.openFile("generate_branch.sv");

    server.setTopLevel(std::string{hdl.m_uri.getPath()});

    // Get only instantiated branches
    auto children = server.getScope("top.the_sub");

    golden.record("children", children);
}

TEST_CASE("HierarchyExpansionMetadataIsSparse") {
    ServerHarness server;

    auto hdl = server.openFile("macro_hierarchy.sv", R"(
`define MAKE_CHILD child macro_child();

module child;
endmodule

module top;
    child direct_child();
    `MAKE_CHILD
endmodule
)");

    server.setTopLevel(std::string{hdl.m_uri.getPath()});
    auto children = server.getScope("top");

    auto findInstance = [&](std::string_view name) -> const hier::Instance& {
        auto it = std::ranges::find_if(children, [&](const hier::HierItem_t& item) {
            return rfl::holds_alternative<hier::Instance>(item) &&
                   rfl::get<hier::Instance>(item).instName == name;
        });
        REQUIRE(it != children.end());
        return rfl::get<hier::Instance>(*it);
    };

    CHECK_FALSE(findInstance("direct_child").fromExpansion.has_value());
    CHECK(findInstance("macro_child").fromExpansion == true);

    auto json = rfl::json::write(children);
    CHECK(json.find("\"fromExpansion\":true") != std::string::npos);
    CHECK(json.find("\"fromExpansion\":false") == std::string::npos);
}

TEST_CASE("GetScopeTypeParameterOverride") {
    ServerHarness server;

    auto hdl = server.openFile("type_param_override.sv", R"(
module child #(parameter type T = logic) ();
endmodule

module top;
    child #(.T(int)) u();
endmodule
)");

    server.setTopLevel(std::string{hdl.m_uri.getPath()});

    auto children = server.getScope("top.u");
    auto found = std::ranges::find_if(children, [](const hier::HierItem_t& item) {
        return rfl::holds_alternative<hier::Var>(item) && rfl::get<hier::Var>(item).instName == "T";
    });

    REQUIRE(found != children.end());
    auto& typeParam = rfl::get<hier::Var>(*found);
    CHECK(typeParam.kind == hier::SlangKind::Param);
    CHECK(typeParam.type == "type");
    REQUIRE(typeParam.value);
    CHECK(*typeParam.value == "int");
}

TEST_CASE("InterfaceModportPortsIncludeDirections") {
    ServerHarness server;

    auto hdl = server.openFile("interface_port.sv", R"(
interface bus;
    logic [7:0] request;
    logic ready;
    modport initiator(output request, input ready);
endinterface

module top(bus.initiator link, bus.initiator links[1:0]);
endmodule
)");

    server.setTopLevel(std::string{hdl.m_uri.getPath()});
    auto children = server.getScope("top");
    auto getScope = [](const std::vector<hier::HierItem_t>& items,
                       std::string_view name) -> const hier::Scope& {
        auto item = std::ranges::find_if(items, [&](const hier::HierItem_t& candidate) {
            return rfl::holds_alternative<hier::Scope>(candidate) &&
                   rfl::get<hier::Scope>(candidate).instName == name;
        });
        REQUIRE(item != items.end());
        return rfl::get<hier::Scope>(*item);
    };
    auto getType = [&](const hier::Scope& scope, std::string_view name) -> const std::string& {
        const auto& portChildren = scope.children;
        auto child = std::ranges::find_if(portChildren, [&](const hier::HierItem_t& item) {
            return rfl::holds_alternative<hier::Var>(item) &&
                   rfl::get<hier::Var>(item).instName == name;
        });
        REQUIRE(child != portChildren.end());
        return rfl::get<hier::Var>(*child).type;
    };

    const auto& interfacePort = getScope(children, "link");
    CHECK(getType(interfacePort, "request") == "output logic[7:0]");
    CHECK(getType(interfacePort, "ready") == "input logic");

    const auto& interfacePortArray = getScope(children, "links");
    CHECK(interfacePortArray.kind == hier::SlangKind::InterfacePortArray);
    REQUIRE(interfacePortArray.children.size() == 2);
    for (const auto& element : interfacePortArray.children) {
        REQUIRE(rfl::holds_alternative<hier::Scope>(element));
        const auto& elementScope = rfl::get<hier::Scope>(element);
        CHECK(getType(elementScope, "request") == "output logic[7:0]");
        CHECK(getType(elementScope, "ready") == "input logic");
    }
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

TEST_CASE("HierLocationRetargetsToShallowAfterEdit") {
    // When a file is edited but not saved, the full compilation (only refreshed on save)
    // still points at the pre-edit buffer. Resolving a hierarchy path must retarget onto the
    // per-edit shallow compilation so the source location reflects the current buffer.
    ServerHarness server("comp_repo");
    server.setBuildFile("cpu_design.f");

    // Baseline: alu_inst resolves inside cpu.sv before any edit.
    auto before = server.getHierLocation("cpu_testbench.dut.alu_inst");
    REQUIRE(before.has_value());
    CHECK(before->uri.getPath().ends_with("cpu.sv"));
    auto baselineLine = before->range.start.line;

    // Open cpu.sv and insert two lines above the alu_inst instantiation, published but not
    // saved. This shifts alu_inst down by two lines in the live buffer.
    auto cpu = server.openFile("cpu.sv");
    cpu.after("// Internal registers").write("\n    // touch\n");
    cpu.publishChanges();

    // The location must retarget onto the shallow compilation and reflect the shifted line,
    // rather than failing to resolve (the old behavior warned "definition has no fullPath").
    auto after = server.getHierLocation("cpu_testbench.dut.alu_inst");
    REQUIRE(after.has_value());
    CHECK(after->uri.getPath().ends_with("cpu.sv"));
    CHECK(after->range.start.line == baselineLine + 2);
}

TEST_CASE("HierLocationRetargetsIndexedPathsAfterEdit") {
    // Retargeting must keep generate-loop and instance-array indices, so paths like
    // gen_alu_array[1].gen_alu_inst or alu_inst_array[2] resolve in the shallow compilation
    // after an edit (plain getLexicalPath() drops the indices and fails to resolve).
    // counter_inst[0] additionally exercises a non-zero-based ([0:0]) array range.
    for (const std::string path :
         {"cpu_testbench.dut.gen_alu_array[1].gen_alu_inst", "cpu_testbench.dut.alu_inst_array[2]",
          "cpu_testbench.dut.counter_inst[0]"}) {
        CAPTURE(path);
        ServerHarness server("comp_repo");
        server.setBuildFile("cpu_design.f");

        auto before = server.getHierLocation(path);
        REQUIRE(before.has_value());
        CHECK(before->uri.getPath().ends_with("cpu.sv"));
        auto baselineLine = before->range.start.line;

        // Insert two lines above the instantiations (all live below "// Internal registers"),
        // published but not saved.
        auto cpu = server.openFile("cpu.sv");
        cpu.after("// Internal registers").write("\n    // touch\n");
        cpu.publishChanges();

        auto after = server.getHierLocation(path);
        REQUIRE(after.has_value());
        CHECK(after->uri.getPath().ends_with("cpu.sv"));
        CHECK(after->range.start.line == baselineLine + 2);
    }
}

TEST_CASE("HierLocationRetargetsNonTopModuleAfterEdit") {
    // Two modules in one file where the top (outer_tb) instantiates a non-top module
    // (inner_cell, which has a non-defaulted parameter so it is never a valid top). A path into
    // inner_cell can't be resolved from the shallow comp's root, so retargeting must descend
    // from the tops to the real instance and resolve the remainder within its body.
    ServerHarness server("comp_repo");
    auto hdl = server.openFile("two_mods.sv");
    server.setTopLevel(std::string{hdl.m_uri.getPath()});

    // dut (a leaf instance) lives inside inner_cell.
    auto path = "outer_tb.the_cell.dut";
    auto before = server.getHierLocation(path);
    REQUIRE(before.has_value());
    CHECK(before->uri.getPath().ends_with("two_mods.sv"));
    auto baselineLine = before->range.start.line;

    // Insert two lines above inner_cell's body (unsaved), shifting dut down by two.
    hdl.after("module inner_cell").write("\n// pad\n");
    hdl.publishChanges();

    auto after = server.getHierLocation(path);
    REQUIRE(after.has_value());
    CHECK(after->uri.getPath().ends_with("two_mods.sv"));
    CHECK(after->range.start.line == baselineLine + 2);
}
