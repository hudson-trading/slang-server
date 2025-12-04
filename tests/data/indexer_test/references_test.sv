// Test file for finding references

module test_module #(
    parameter WIDTH = 8
)(
    input logic clk,
    input logic rst,
    input logic [WIDTH-1:0] din,
    output logic [WIDTH-1:0] dout
);
    logic [7:0] data;
    logic [WIDTH-1:0] temp;

    always_ff @(posedge clk) begin
        if (rst) begin
            data <= 8'h00;
            temp <= '0;
        end else begin
            data <= din[7:0];
            temp <= din;
        end
    end

    assign dout = temp;

endmodule : test_module
