// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

`include "cpu_defines.svh"

// Unused top module for testing
module unused_mod
(
);
    // Internal signals for ALU
    logic [31:0] alu_a [4];
    logic [31:0] alu_b [4];
    logic [31:0] alu_result [4];
    alu_op_t alu_op [4];
    logic alu_zero [4];
    logic alu_overflow [4];

    // Single instance array of length 1
    logic [31:0] single_alu_a [1];
    logic [31:0] single_alu_b [1];
    logic [31:0] single_alu_result [1];
    alu_op_t single_alu_op [1];
    logic single_alu_zero [1];
    logic single_alu_overflow [1];

    // Instance array - regular instance array with 4 instances
    alu #(
        .WIDTH(32)
//       ^^^^^ MissingCompletion[WIDTH] Context[Expression] Trigger[.] Items[0]
    ) alu_inst_array [3:0] (
        .a(alu_a),
//       ^ MissingCompletion[a] Context[Expression] Trigger[.] Items[0]
        .b(alu_b),
//       ^ MissingCompletion[b] Context[Expression] Trigger[.] Items[0]
        .op(alu_op),
//       ^^ MissingCompletion[op] Context[Expression] Trigger[.] Items[0]
        .result(alu_result),
//       ^^^^^^ MissingCompletion[result] Context[Expression] Trigger[.] Items[0]
        .zero(alu_zero),
//       ^^^^ MissingCompletion[zero] Context[Expression] Trigger[.] Items[0]
        .overflow(alu_overflow)
//       ^^^^^^^^ MissingCompletion[overflow] Context[Expression] Trigger[.] Items[0]
    );

    // Instance array of length 1
    alu #(
        .WIDTH(32)
//       ^^^^^ MissingCompletion[WIDTH] Context[Expression] Trigger[.] Items[0]
    ) single_alu_inst [0:0] (
        .a(single_alu_a),
//       ^ MissingCompletion[a] Context[Expression] Trigger[.] Items[0]
        .b(single_alu_b),
//       ^ MissingCompletion[b] Context[Expression] Trigger[.] Items[0]
        .op(single_alu_op),
//       ^^ MissingCompletion[op] Context[Expression] Trigger[.] Items[0]
        .result(single_alu_result),
//       ^^^^^^ MissingCompletion[result] Context[Expression] Trigger[.] Items[0]
        .zero(single_alu_zero),
//       ^^^^ MissingCompletion[zero] Context[Expression] Trigger[.] Items[0]
        .overflow(single_alu_overflow)
//       ^^^^^^^^ MissingCompletion[overflow] Context[Expression] Trigger[.] Items[0]
    );

    // Generate block with generate array
    genvar i;
    generate
        for (i = 0; i < 3; i++) begin : gen_alu_array
            logic [31:0] gen_alu_a, gen_alu_b, gen_alu_result;
            alu_op_t gen_alu_op;
            logic gen_alu_zero, gen_alu_overflow;

            alu #(
                .WIDTH(32)
//               ^^^^^ MissingCompletion[WIDTH] Context[Expression] Trigger[.] Items[0]
            ) gen_alu_inst (
                .a(gen_alu_a),
//               ^ MissingCompletion[a] Context[Expression] Trigger[.] Items[0]
                .b(gen_alu_b),
//               ^ MissingCompletion[b] Context[Expression] Trigger[.] Items[0]
                .op(gen_alu_op),
//               ^^ MissingCompletion[op] Context[Expression] Trigger[.] Items[0]
                .result(gen_alu_result),
//               ^^^^^^ MissingCompletion[result] Context[Expression] Trigger[.] Items[0]
                .zero(gen_alu_zero),
//               ^^^^ MissingCompletion[zero] Context[Expression] Trigger[.] Items[0]
                .overflow(gen_alu_overflow)
//               ^^^^^^^^ MissingCompletion[overflow] Context[Expression] Trigger[.] Items[0]
            );
        end
    endgenerate

    initial begin
        $display("This module is not instantiated");
    end
endmodule
