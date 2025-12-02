// Test file for macro argument references

`define ASSIGN_ZERO(signal) signal = '0
`define ASSIGN_VALUE(dest, src) dest = src
`define COMPARE(a, b) (a == b)
`define INCREMENT(counter) counter = counter + 1
`define REGISTER_UPDATE(clk, rst, reg_name, value) \
    always_ff @(posedge clk) begin \
        if (rst) \
            reg_name <= '0; \
        else \
            reg_name <= value; \
    end

module macro_test(
    input logic clk,
    input logic rst,
    input logic [7:0] din,
    output logic [7:0] dout
);
    logic [7:0] counter;
    logic [7:0] temp_val;
    logic [7:0] result;

    // Macro invocations with signal references
    initial begin
        `ASSIGN_ZERO(counter);
        `ASSIGN_VALUE(temp_val, din);
    end

    always_comb begin
        if (`COMPARE(counter, temp_val)) begin
            result = counter;
        end else begin
            result = temp_val;
        end
    end

    always_ff @(posedge clk) begin
        if (rst) begin
            `ASSIGN_ZERO(counter);
        end else begin
            `INCREMENT(counter);
        end
    end

    // Multi-line macro with references
    `REGISTER_UPDATE(clk, rst, dout, result)

endmodule
