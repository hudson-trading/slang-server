// Memory module that uses common macros

`include "common_macros.svh"

module memory_module #(
    parameter int DATA_WIDTH = `WIDTH,
    parameter int MEM_DEPTH = `DEPTH
) (
    input  logic clk,
    input  logic rst_n,
    input  logic [`ADDR_WIDTH-1:0] addr,
    input  logic [DATA_WIDTH-1:0] wdata,
    input  logic wen,
    output logic [DATA_WIDTH-1:0] rdata
);

    // Memory array
    logic [DATA_WIDTH-1:0] mem [`DEPTH];

    // Read/Write logic
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rdata <= '0;
        end else begin
            if (wen) begin
                mem[addr] <= wdata;
            end
            rdata <= mem[addr];
        end
    end

    // Register addresses using macro
    `REG_ADDR(STATUS, 'h00)
    `REG_ADDR(CONTROL, 'h04)
    `REG_ADDR(DATA, 'h08)

endmodule
