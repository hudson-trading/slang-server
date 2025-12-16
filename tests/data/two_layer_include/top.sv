// Top module that includes intermediate header
`include "intermediate.svh"

module top #(
    parameter int WIDTH = `BUS_WIDTH,
    parameter int ADDR = `ADDR_SIZE
) (
    input  logic [WIDTH-1:0] data,
    input  logic [ADDR-1:0] addr,
    output logic valid
);

    assign valid = 1'b1;

endmodule
