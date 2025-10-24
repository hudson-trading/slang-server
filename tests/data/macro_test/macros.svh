// Comprehensive macro test file covering various macro scenarios
// Based on test cases from tests/unittests/parsing/DiagnosticTests.cpp

// Basic macro definitions
`define SIMPLE_MACRO 42
`define FUNC_MACRO(x) (x + 1)
`define TWO_ARG_MACRO(a, b) (a + b)

// Use macros from common_macros.svh
`define BUS_WIDTH `WIDTH
`define MEM_SIZE `DEPTH
`define ADDR_BITS `ADDR_WIDTH

// Nested macro definitions
`define FOO(blah) blah
`define BAR(blah) `FOO(blah)
`define BAZ(xy) xy

// More complex nested macros
`define COMPLEX_FOO(blah, flurb) blah+`BAZ(flurb)
`define COMPLEX_BAR(blah, flurb) `COMPLEX_FOO(blah, flurb)

// Macros that create syntax issues
`define INCOMPLETE_PAREN (i
`define CLOSING_PAREN 1)
`define COMBINED_MACRO `INCOMPLETE_PAREN + `CLOSING_PAREN ()

// Macro with concatenation and pass-through
`define PASS(asdf, barr) asdf barr
`define PASS_SINGLE(asdf) asdf

`define DECLARE_INT(name, val) localparam int name = val;

`define INFO(msg) $display("Info: %s", msg);

// Line directive testing
// TODO: fix the test handling of this, although this is typically for generated code
// `line 100 "macro_test.svh" 0

module test;

    // Basic struct for testing member access
    typedef struct {
        int i;
        logic [7:0] data;
    } test_struct;

    // Variables for testing - use macros from common_macros.svh
    int test_var;
    logic [`BUS_WIDTH-1:0] bus_signal;
    logic [`ADDR_BITS-1:0] address;

    // Use CREATE_REG macro from common_macros.svh
    `CREATE_REG(status_reg, 8)
    `CREATE_REG(control_reg, 16)

    // Test simple macro usage
    localparam int SIMPLE_VAL = `SIMPLE_MACRO;

    // Test function macro
    localparam int FUNC_VAL = `FUNC_MACRO(10);

    // Test two argument macro
    localparam int TWO_ARG_VAL = `TWO_ARG_MACRO(5, 7);

    // Test definition behind macro
    `DECLARE_INT(some_int, 1)
    `DECLARE_INT(other_int, 2)

    `INFO(some_int + other_int)

    localparam int x = some_int;


    // Test macro in generate blocks
    genvar i;
    generate
        for (i = 0; i < `SIMPLE_MACRO; i++) begin : gen_block
            logic [`FUNC_MACRO(7):0] gen_signal;
        end
    endgenerate

    // Test macro in always blocks
    always_comb begin
        case (test_var)
            `SIMPLE_MACRO: bus_signal = `TWO_ARG_MACRO(1, 2);
            default: bus_signal = `FUNC_MACRO(0);
        endcase
    end


    // Signals for memory instantiation
    logic clk, rst_n, write_enable;
    logic [31:0] write_data, read_data;

    // Test ifdef/else conditional compilation
`ifdef USE_MEMORY_MODULE
    // Use the macro to instantiate memory_module
    `INST_MEMORY(mem_inst, 32)
`else
    // Alternative implementation without memory_module
    test_struct my_struct;
`endif

    // Test macro with system tasks
    initial begin
        $display("Testing macro: %d", `SIMPLE_MACRO);
        $display("Function macro result: %d", `FUNC_MACRO(25));
        $display("Max value: %d", `MAX(10, 20));
        $display("Min value: %d", `MIN(10, 20));

        // Test assertion macro from common_macros.svh
        `ASSERT_EQ(10, 10)
    end

endmodule
