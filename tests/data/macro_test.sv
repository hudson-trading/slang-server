// Comprehensive macro test file covering various macro scenarios
// Based on test cases from tests/unittests/parsing/DiagnosticTests.cpp

// Basic macro definitions
`define SIMPLE_MACRO 42
`define FUNC_MACRO(x) (x + 1)
`define TWO_ARG_MACRO(a, b) (a + b)

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
    struct {
        int i;
        logic [7:0] data;
    } test_struct;

    // Variables for testing
    int test_var;
    logic [31:0] bus_signal;

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

    // Test macro with system tasks
    initial begin
        $display("Testing macro: %d", `SIMPLE_MACRO);
        $display("Function macro result: %d", `FUNC_MACRO(25));
    end

endmodule
