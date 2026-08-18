`include "cpu_defines.svh"

module alu #(
    parameter int WIDTH = 32
) (
    input  logic [WIDTH-1:0]    a,
    input  logic [WIDTH-1:0]    b,
    input  alu_op_t             op,
//         ^^^^^^^^ MissingCompletion[alu_op_t] Context[Expression] Trigger[Invoked] Items[9]
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
//          ^^^^^^^ MissingCompletion[ALU_ADD] Context[Procedural] Trigger[Invoked] Items[9]
                extended_result = {1'b0, a} + {1'b0, b};
                overflow = extended_result[WIDTH];
            end
            ALU_SUB: begin
//          ^^^^^^^ MissingCompletion[ALU_SUB] Context[Procedural] Trigger[Invoked] Items[9]
                extended_result = {1'b0, a} - {1'b0, b};
                overflow = extended_result[WIDTH];
            end
            ALU_AND: begin
//          ^^^^^^^ MissingCompletion[ALU_AND] Context[Procedural] Trigger[Invoked] Items[9]
                extended_result = {1'b0, a & b};
            end
            ALU_OR: begin
//          ^^^^^^ MissingCompletion[ALU_OR] Context[Procedural] Trigger[Invoked] Items[9]
                extended_result = {1'b0, a | b};
            end
            ALU_XOR: begin
//          ^^^^^^^ MissingCompletion[ALU_XOR] Context[Procedural] Trigger[Invoked] Items[9]
                extended_result = {1'b0, a ^ b};
            end
            ALU_SLL: begin
//          ^^^^^^^ MissingCompletion[ALU_SLL] Context[Procedural] Trigger[Invoked] Items[9]
                extended_result = {1'b0, a << b[4:0]};
            end
            ALU_SRL: begin
//          ^^^^^^^ MissingCompletion[ALU_SRL] Context[Procedural] Trigger[Invoked] Items[9]
                extended_result = {1'b0, a >> b[4:0]};
            end
            ALU_SRA: begin
//          ^^^^^^^ MissingCompletion[ALU_SRA] Context[Procedural] Trigger[Invoked] Items[9]
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
