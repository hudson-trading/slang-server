`include "defs.svh"

module child_one #(
    parameter int WIDTH = `DEFAULT_WIDTH
) (
    input logic [WIDTH-1:0] data
);
    $static_assert(WIDTH == `DEFAULT_WIDTH, "instance width differs");
endmodule
