`include "cpu_defines.svh"

module alu #(
    parameter int WIDTH = 32
) (
    input  logic [WIDTH-1:0]    a,
    input  logic [WIDTH-1:0]    b,
    input  alu_op_t             op,
    output logic [WIDTH-1:0]    result,
    output logic                zero,
    output logic                overflow
);

    logic [WIDTH:0] extended_result;

    always_comb begin
        extended_result = '0;
        overflow = 1'b0;

        case (op)
            ALU_ADD: begin
                extended_result = {1'b0, a} + {1'b0, b};
                overflow = extended_result[WIDTH];
            end
            ALU_SUB: begin
                extended_result = {1'b0, a} - {1'b0, b};
                overflow = extended_result[WIDTH];
            end
            ALU_AND: begin
                extended_result = {1'b0, a & b};
            end
            ALU_OR: begin
                extended_result = {1'b0, a | b};
            end
            ALU_XOR: begin
                extended_result = {1'b0, a ^ b};
            end
            ALU_SLL: begin
                extended_result = {1'b0, a << b[4:0]};
            end
            ALU_SRL: begin
                extended_result = {1'b0, a >> b[4:0]};
            end
            ALU_SRA: begin
                extended_result = {1'b0, $signed(a) >>> b[4:0]};
            end
            default: begin
                extended_result = '0;
            end
        endcase

        result = extended_result[WIDTH-1:0];
        zero = (result == '0);
    end

endmodule
