// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "lsp/LspTypes.h"
#include "util/Logging.h"
#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"
#include <optional>

using namespace server;

using namespace slang;

TEST_CASE("MacroCompletion") {
    ServerHarness server("repo1");

    auto doc = server.openFile("test1.svh", R"(
    `define TEST_MACRO(arg1, arg2) \
        $display("arg1: %s, arg2: %s", arg1, arg2);
    )");
    // For simplicity we add all defines in the current file
    CHECK(doc.begin().getCompletions("`").size() == 2);
    CHECK(doc.end().getCompletions("`").size() == 2);

    // Only return the indexed one
    auto doc2 = server.openFile("test2.sv", R"()");
    CHECK(doc2.begin().getCompletions("`").size() == 1);

    // Now that it's saved, it should be indexed
    doc.save();
    CHECK(doc2.begin().getCompletions("`").size() == 2);
}

TEST_CASE("ModuleCompletion") {

    ServerHarness server("repo1");

    auto doc = server.openFile("test1.sv", R"(
    module test1 #(
        parameter int PARAM = 42,
    )(
        input logic clk,
        input rst,
    );
        initial begin
            $display("Hello, World!");
        end
    endmodule
    )");

    auto doc2 = server.openFile("test2.sv", R"(
        module test2;
        //inmodule

        endmodule
    )");

    auto cursor = doc2.before("//inmodule");
    // CHECK(cursor.getCompletions().size() == 2);
    RFL_INFO(cursor.getResolvedCompletions());
    // Check that the module is indexed after saving
    doc.save();
    auto comps = cursor.getCompletions();
    // Other completions from the workspace

    auto it = std::find_if(comps.begin(), comps.end(),
                           [](const CompletionHandle& item) { return item.m_item.label == "Dut"; });

    REQUIRE(it != comps.end());
    auto comp = *it;
    comp.resolve();
    CHECK(comp.m_item.insertText == R"(Dut #(
    .a(${1:a /* default 0 */}),
    .b(${2:b /* default 1 */})
 ) ${3:dut} (
    .foo(${4:foo})
);)");
}

TEST_CASE("PackageCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("package_test.sv", R"(
    package test_pkg;
        parameter int PKG_PARAM = 10;

        typedef struct {
            int field1;
            logic field2;
        } my_struct_t;

        typedef enum {
            VALUE_A,
            VALUE_B
        } my_enum_t;

        function int get_value();
            return 42;
        endfunction

        int global_var = 5;

        logic [7:0] port_signal;

        // Generate block for Snippet completion kind
        generate
            genvar i;
            for (i = 0; i < 4; i++) begin : gen_block
                logic [7:0] gen_var;
            end
        endgenerate
    endpackage

    module test_module;
        import test_pkg::*;

        initial begin
            // Try to get completions after test_pkg::
            int x = test_pkg:
        end
    endmodule
    )");

    // Test completions after test_pkg:: - automatically resolves all completions
    auto completionItems = doc.after("test_pkg::").getResolvedCompletions(":");

    golden.record(completionItems);
}

TEST_CASE("WildcardImportCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("wildcard_test.sv", R"(
    package math_pkg;
        parameter int PI_VALUE = 314;
        parameter int E_VALUE = 271;

        typedef struct {
            real x;
            real y;
        } point_t;

        typedef enum {
            ADD,
            SUBTRACT,
            MULTIPLY
        } operation_t;

        function real calculate(real a, real b, operation_t op);
            case (op)
                ADD: return a + b;
                SUBTRACT: return a - b;
                MULTIPLY: return a * b;
                default: return 0.0;
            endcase
        endfunction

        task print_result(real value);
            $display("Result: %f", value);
        endtask
    endpackage

    package utils_pkg;
        parameter int MAX_SIZE = 1024;

        typedef logic [7:0] byte_t;

        function int find_max(int array[], int size);
            int max_val = array[0];
            for (int i = 1; i < size; i++) begin
                if (array[i] > max_val)
                    max_val = array[i];
            end
            return max_val;
        endfunction
    endpackage

    module test_wildcard_imports;
        import math_pkg::*;
        import utils_pkg::*;

        initial begin
            point_t my_point;
            operation_t op = ADD;
            byte_t data = 8'hFF;

            // Test completions with wildcard imports
            real result = calculate(PI_VALUE, E_VALUE, op);
            print_result(x, );
            int max_val = find_max();

            // Test block member completions with wildcard imports
        end
    endmodule
    )");

    // Test completions after wildcard imports
    auto afterPrintResult = doc.after("print_result(x, ").getResolvedCompletions();
    golden.record("after_print_result", afterPrintResult);

    auto lhsCompletion = doc.before("// Test block member completions").getResolvedCompletions();
    golden.record("block_completions", lhsCompletion);
}

TEST_CASE("ModuleMemberCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("module_test.sv", R"(
    module test_module (
        input  logic        clk,
        input  logic        rst,
        output logic [7:0]  data_out
    );
        // Local variables of different types
        logic internal_signal;
        logic [15:0] wide_signal;

        // Parameters
        parameter int PARAM_INT = 42;
        parameter logic [7:0] PARAM_LOGIC = 8'hAA;

        // Type definitions
        typedef struct {
            logic [7:0] addr;
            logic [31:0] data;
        } bus_transaction_t;

        typedef enum logic [1:0] {
            IDLE = 2'b00,
            ACTIVE = 2'b01,
            WAIT = 2'b10
        } state_t;

        // Local functions
        function logic [7:0] calc_parity(input logic [7:0] data);
            return ^data;
        endfunction

        // Task
        task reset_signals();
            internal_signal <= 1'b0;
            wide_signal <= 16'h0;
        endtask

        // Instance of another module
        sub_module u_sub (
            .clk(clk),
            .rst(rst),
            .enable(internal_signal)
        );

        // Generate blocks
        generate
            genvar i;
            for (i = 0; i < 4; i++) begin : gen_array
                logic [7:0] gen_signal;
            end
        endgenerate

        // Interface port example
        simple_interface intf();

        initial begin
            // Test member completions in module scope
            internal_signal =
            wide_signal =
        end
    endmodule

    // Sub-module for instantiation
    module sub_module (
        input logic clk,
        input logic rst,
        input logic enable
    );
    endmodule

    // Simple interface for interface port testing
    interface simple_interface;
        logic valid;
        logic ready;
    endinterface
    )");

    // Test completions for module members - automatically resolves all completions
    auto lhs = doc.before("sub_module u_sub (").getResolvedCompletions();
    auto rhs = doc.after("wide_signal =").getResolvedCompletions();

    golden.record("lhs", lhs);
    golden.record("rhs", rhs);

    // Test other RHS locations - they should all return the same completions
    auto rhsClk = doc.after(".clk(").getResolvedCompletions();
    auto rhsRst = doc.after(".rst(").getResolvedCompletions();
    auto rhsEnable = doc.after(".enable(").getResolvedCompletions();

    // All RHS completions should be identical
    CHECK(rhs.size() == rhsClk.size());
    CHECK(rhs.size() == rhsRst.size());
    CHECK(rhs.size() == rhsEnable.size());
}

TEST_CASE("HierarchicalInstanceCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("hierarchical_test.sv", R"(
    module sub_module (
        input logic clk,
        input logic rst,
        output logic [7:0] data_out,
        output logic valid
    );
        logic internal_state;

        always_ff @(posedge clk) begin
            if (rst) begin
                data_out <= 8'h0;
                valid <= 1'b0;
                internal_state <= 1'b0;
            end else begin
                data_out <= data_out + 1;
                valid <= ~valid;
                internal_state <= ~internal_state;
            end
        end
    endmodule

    module parent_module;
        logic clk, rst;
        logic [7:0] data;
        logic valid;

        sub_module inst (
            .clk(clk),
            .rst(rst),
            .data_out(data),
            .valid(valid)
        );

        initial begin
            // Test hierarchical instance completions
            inst.
        end
    endmodule
    )");

    // Test completions after "inst."
    auto instCompletions = doc.after("inst.").getResolvedCompletions(".");
    golden.record("instance_completions", instCompletions);
}

TEST_CASE("HierarchicalStructCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("struct_hierarchical_test.sv", R"(
    typedef struct {
        logic [7:0] addr;
        logic [31:0] data;
        logic valid;
    } simple_struct_t;

    typedef struct {
        simple_struct_t inner;
        logic [15:0] tag;
        logic ready;
    } nested_struct_t;

    typedef struct {
        nested_struct_t level1;
        logic [3:0] id;
        logic enable;
    } deep_nested_struct_t;

    module struct_test_module;
        simple_struct_t my_struct;
        nested_struct_t complex_struct;
        deep_nested_struct_t very_complex_struct;

        initial begin
            my_struct.;

            complex_struct.;

            very_complex_struct.;

            complex_struct.inner.;

            very_complex_struct.level1.;

            very_complex_struct.level1.inner.;
        end
    endmodule
    )");

    auto testCompletion = [&](std::string s) {
        auto completions = doc.after(s).getResolvedCompletions(".");
        CHECK(!completions.empty());
        golden.record(s, completions);
    };

    testCompletion("my_struct.");
    testCompletion("complex_struct.");
    testCompletion("very_complex_struct.");
    testCompletion("complex_struct.inner.");
    testCompletion("very_complex_struct.level1.");
    testCompletion("very_complex_struct.level1.inner.");
}

TEST_CASE("ArrayOfStructsCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("array_struct_test.sv", R"(
    typedef struct {
        logic [7:0] addr;
        logic [31:0] data;
        logic valid;
    } transaction_t;

    typedef struct {
        transaction_t txn;
        logic [15:0] id;
    } nested_transaction_t;

    module array_struct_module;
        transaction_t transactions[4];
        transaction_t transactions_2d[2][3];
        nested_transaction_t nested_arr[8];

        initial begin
            // Test completion on array element
            transactions[0].;

            // Test completion on 2D array element
            transactions_2d[0][1].;

            // Test completion on nested struct in array
            nested_arr[3].;

            // Test nested field access in array element
            nested_arr[5].txn.;
        end
    endmodule
    )");

    auto testCompletion = [&](std::string s) {
        auto completions = doc.after(s).getResolvedCompletions(".");
        golden.record(s, completions);
    };

    testCompletion("transactions[0].");
    testCompletion("transactions_2d[0][1].");
    testCompletion("nested_arr[3].");
    testCompletion("nested_arr[5].txn.");
}

TEST_CASE("PortListCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    // Create and save an interface with modports so it gets indexed
    auto intfDoc = server.openFile("test_intf.sv", R"(
    interface test_intf;
        logic valid;
        logic ready;
        logic [7:0] data;

        modport leader(output valid, output data, input ready);
        modport follower(input valid, input data, output ready);
    endinterface
    )");
    intfDoc.save();

    auto doc = server.openFile("port_list_test.sv", R"(
    module test_port_completion (
        input logic clk,
        // cursor in port list
    );

    endmodule


    module test_modpor_comps (
        test_intf.
    );
    endmodule
    )");

    // Test completions in port list - should have interfaces but NOT modules
    auto portListCompletions = doc.before("// cursor in port list").getResolvedCompletions();

    // Test completions after "intf_inst." - should show interface members/modports
    auto modportCompletions = doc.after("test_intf.").getResolvedCompletions(".");

    // Helper to find completion by label
    auto findByLabel = [](const std::vector<lsp::CompletionItem>& items,
                          const std::string& label) -> const lsp::CompletionItem* {
        auto it = std::find_if(items.begin(), items.end(), [&](const lsp::CompletionItem& item) {
            return item.label == label;
        });
        return it != items.end() ? &(*it) : nullptr;
    };

    // Port list should NOT have Dut (module instantiation not valid in ports)
    CHECK(findByLabel(portListCompletions, "Dut") == nullptr);

    // Port list SHOULD have interface
    CHECK(findByLabel(portListCompletions, "test_intf") != nullptr);

    // Port list SHOULD have packages
    CHECK(findByLabel(portListCompletions, "base_pkg") != nullptr);

    // Interface member completions should have signals and modports

    golden.record("port_list", portListCompletions);
    golden.record("modports", modportCompletions);
}

TEST_CASE("LocalparamExcludedFromCompletion") {
    // IEEE-1800 23.2.3: localparams in module header cannot be overwritten,
    // so they should be excluded from parameter completions when instantiating a module

    ServerHarness server("repo1");

    // Create a module with both parameter and localparam in header
    auto moduleDoc = server.openFile("module_with_localparam.sv", R"(
    module module_with_localparam #(
        parameter int normal_param = 0,
        localparam int local_param = 1,
        parameter int another_param = 2
    ) (
        input logic clk
    );
    endmodule
    )");
    moduleDoc.save();

    auto doc = server.openFile("test_localparam.sv", R"(
    module test_localparam;
        //inmodule

    endmodule
    )");

    auto cursor = doc.before("//inmodule");
    auto comps = cursor.getCompletions();

    // Find the module_with_localparam completion
    auto it = std::find_if(comps.begin(), comps.end(), [](const CompletionHandle& item) {
        return item.m_item.label == "module_with_localparam";
    });

    REQUIRE(it != comps.end());
    auto comp = *it;
    comp.resolve();

    // The completion should include normal_param and another_param, but NOT local_param
    auto insertText = comp.m_item.insertText.value_or("");

    CHECK(insertText.find("normal_param") != std::string::npos);
    CHECK(insertText.find("another_param") != std::string::npos);
    CHECK(insertText.find("local_param") == std::string::npos);
}

TEST_CASE("LocalparamKeywordInheritance") {
    // IEEE-1800: When the keyword is omitted in a parameter port list,
    // it inherits from the previous entry. This tests that behavior.

    ServerHarness server("repo1");

    // Create a module where localparams inherit the keyword from previous entry
    auto moduleDoc = server.openFile("module_inherited_localparam.sv", R"(
    module module_inherited_localparam #(
        parameter int p1 = 0,
        int p2 = 1,              // inherits 'parameter' from p1
        localparam int lp1 = 2,
        int lp2 = 3,             // inherits 'localparam' from lp1
        parameter int p3 = 4     // explicit parameter again
    ) (
        input logic clk
    );
    endmodule
    )");
    moduleDoc.save();

    auto doc = server.openFile("test_inherited_localparam.sv", R"(
    module test_inherited_localparam;
        //inmodule

    endmodule
    )");

    auto cursor = doc.before("//inmodule");
    auto comps = cursor.getCompletions();

    auto it = std::find_if(comps.begin(), comps.end(), [](const CompletionHandle& item) {
        return item.m_item.label == "module_inherited_localparam";
    });

    REQUIRE(it != comps.end());
    auto comp = *it;
    comp.resolve();

    auto insertText = comp.m_item.insertText.value_or("");

    // p1, p2, and p3 should be included (they're parameters)
    CHECK(insertText.find("p1") != std::string::npos);
    CHECK(insertText.find("p2") != std::string::npos);
    CHECK(insertText.find("p3") != std::string::npos);

    // lp1 and lp2 should NOT be included (they're localparams)
    CHECK(insertText.find("lp1") == std::string::npos);
    CHECK(insertText.find("lp2") == std::string::npos);
}

TEST_CASE("ModuleInstanceNameAllCapsToCamelCase") {
    ServerHarness server("repo1");

    auto moduleDoc = server.openFile("camel_module.sv", R"(
    module THEUPPERCASEMODULE (
        input logic clk
    );
    endmodule
    )");
    moduleDoc.save();

    auto doc = server.openFile("use_camel_module.sv", R"(
    module top;
        //cursor
    endmodule
    )");

    auto cursor = doc.before("//cursor");
    auto comps = cursor.getCompletions();

    auto it = std::find_if(comps.begin(), comps.end(), [](const CompletionHandle& item) {
        return item.m_item.label == "THEUPPERCASEMODULE";
    });

    REQUIRE(it != comps.end());

    auto comp = *it;
    comp.resolve();

    auto insertText = comp.m_item.insertText.value_or("");

    CHECK(insertText.find("theuppercasemodule") != std::string::npos);
}

TEST_CASE("ModuleInstanceNameUpperUpperToCamelCase") {
    ServerHarness server("repo1");

    auto moduleDoc = server.openFile("camel_module.sv", R"(
    module UpperThenMoreUpper (
        input logic clk
    );
    endmodule
    )");
    moduleDoc.save();

    auto doc = server.openFile("use_camel_module.sv", R"(
    module top;
        //cursor
    endmodule
    )");

    auto cursor = doc.before("//cursor");
    auto comps = cursor.getCompletions();

    auto it = std::find_if(comps.begin(), comps.end(), [](const CompletionHandle& item) {
        return item.m_item.label == "UpperThenMoreUpper";
    });

    REQUIRE(it != comps.end());

    auto comp = *it;
    comp.resolve();

    auto insertText = comp.m_item.insertText.value_or("");

    std::cout << "Insert text: " << insertText << "\n";

    CHECK(insertText.find("upperThenMoreUpper") != std::string::npos);
}

TEST_CASE("ModuleInstanceNameSomeUppercaseToCamelCase") {
    ServerHarness server("repo1");

    auto moduleDoc = server.openFile("camel_module.sv", R"(
    module SOMEUpperCase (
        input logic clk
    );
    endmodule
    )");
    moduleDoc.save();

    auto doc = server.openFile("use_camel_module.sv", R"(
    module top;
        //cursor
    endmodule
    )");

    auto cursor = doc.before("//cursor");
    auto comps = cursor.getCompletions();

    auto it = std::find_if(comps.begin(), comps.end(), [](const CompletionHandle& item) {
        return item.m_item.label == "SOMEUpperCase";
    });

    REQUIRE(it != comps.end());

    auto comp = *it;
    comp.resolve();

    auto insertText = comp.m_item.insertText.value_or("");

    CHECK(insertText.find("someUpperCase") != std::string::npos);
}
