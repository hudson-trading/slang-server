`include "defs.svh"

module top;
    localparam int Width = `DEFAULT_WIDTH;
    logic [31:0] data;

    child_one #(.WIDTH(32)) child_one_inst (.data(data));
    child_two #(.WIDTH(32)) child_two_inst (.data(data));
endmodule
