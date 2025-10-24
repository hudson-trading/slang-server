// Common macros used across multiple files

`define WIDTH 32
`define DEPTH 1024
`define ADDR_WIDTH $clog2(`DEPTH)

`define REG_ADDR(name, offset) localparam int name``_ADDR = offset;

`define CREATE_REG(name, width) \
    logic [width-1:0] name; \
    assign name = {width{1'b0}};

`define ASSERT_EQ(actual, expected) \
    if (actual !== expected) \
        $error("Assertion failed: %s != %s", `"actual`", `"expected`");

`define MAX(a, b) ((a) > (b) ? (a) : (b))
`define MIN(a, b) ((a) < (b) ? (a) : (b))

`define CLK_PERIOD 10
`define RESET_CYCLES 5
