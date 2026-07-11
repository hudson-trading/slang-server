`include "cpu_defines.svh"

module cpu #(
    parameter int DATA_WIDTH = 32,
    parameter int ADDR_WIDTH = 32
) (
    input  logic                    clk,
    input  logic                    rst_n,

    // Memory interface
    output logic [ADDR_WIDTH-1:0]  mem_addr,
    output logic [DATA_WIDTH-1:0]  mem_wdata,
    input  logic [DATA_WIDTH-1:0]  mem_rdata,
    output logic                    mem_we,
    output logic                    mem_req,
    input  logic                    mem_ack,

    // Status
    output cpu_state_t              state,
//         ^^^^^^^^^^^ MissingCompletion[cpu_state_t] Context[Expression] Trigger[Invoked] Items[33]
    output logic                    halted
);

    // Internal registers
    logic [DATA_WIDTH-1:0] pc;
    logic [DATA_WIDTH-1:0] instruction;
    logic [DATA_WIDTH-1:0] register_file [0:31];

    // ALU instance
    alu #(
        .WIDTH(DATA_WIDTH)
//       ^^^^^ MissingCompletion[WIDTH] Context[Expression] Trigger[.] Items[0]
    ) alu_inst (
        .a(register_file[instruction[25:21]]),
//       ^ MissingCompletion[a] Context[Expression] Trigger[.] Items[0]
        .b(register_file[instruction[20:16]]),
//       ^ MissingCompletion[b] Context[Expression] Trigger[.] Items[0]
        .op(instruction[31:26]),
//       ^^ MissingCompletion[op] Context[Expression] Trigger[.] Items[0]
        .result(alu_result),
//       ^^^^^^ MissingCompletion[result] Context[Expression] Trigger[.] Items[0]
        .zero(alu_zero),
//       ^^^^ MissingCompletion[zero] Context[Expression] Trigger[.] Items[0]
        .overflow(alu_overflow)
//       ^^^^^^^^ MissingCompletion[overflow] Context[Expression] Trigger[.] Items[0]
    );

    // Memory controller instance
    memory_controller #(
        .ADDR_WIDTH(ADDR_WIDTH),
//       ^^^^^^^^^^ MissingCompletion[ADDR_WIDTH] Context[Expression] Trigger[.] Items[0]
        .DATA_WIDTH(DATA_WIDTH)
//       ^^^^^^^^^^ MissingCompletion[DATA_WIDTH] Context[Expression] Trigger[.] Items[0]
    ) mem_ctrl (
        .clk(clk),
//       ^^^ MissingCompletion[clk] Context[Expression] Trigger[.] Items[0]
        .rst_n(rst_n),
//       ^^^^^ MissingCompletion[rst_n] Context[Expression] Trigger[.] Items[0]
        .cpu_addr(mem_addr),
//       ^^^^^^^^ MissingCompletion[cpu_addr] Context[Expression] Trigger[.] Items[0]
        .cpu_wdata(mem_wdata),
//       ^^^^^^^^^ MissingCompletion[cpu_wdata] Context[Expression] Trigger[.] Items[0]
        .cpu_rdata(mem_rdata),
//       ^^^^^^^^^ MissingCompletion[cpu_rdata] Context[Expression] Trigger[.] Items[0]
        .cpu_we(mem_we),
//       ^^^^^^ MissingCompletion[cpu_we] Context[Expression] Trigger[.] Items[0]
        .cpu_req(mem_req),
//       ^^^^^^^ MissingCompletion[cpu_req] Context[Expression] Trigger[.] Items[0]
        .cpu_ack(mem_ack)
//       ^^^^^^^ MissingCompletion[cpu_ack] Context[Expression] Trigger[.] Items[0]
    );

    logic [DATA_WIDTH-1:0] alu_result;
    logic alu_zero, alu_overflow;

    // Instance array - array of 4 ALU instances
    logic [DATA_WIDTH-1:0] alu_array_a [4];
    logic [DATA_WIDTH-1:0] alu_array_b [4];
    logic [DATA_WIDTH-1:0] alu_array_result [4];
    alu_op_t alu_array_op [4];
//  ^^^^^^^^ MissingCompletion[alu_op_t] Context[ModuleMember] Trigger[Invoked] Items[17]
    logic alu_array_zero [4];
    logic alu_array_overflow [4];

    alu #(
        .WIDTH(DATA_WIDTH)
//       ^^^^^ MissingCompletion[WIDTH] Context[Expression] Trigger[.] Items[0]
    ) alu_inst_array [3:0] (
        .a(alu_array_a),
//       ^ MissingCompletion[a] Context[Expression] Trigger[.] Items[0]
        .b(alu_array_b),
//       ^ MissingCompletion[b] Context[Expression] Trigger[.] Items[0]
        .op(alu_array_op),
//       ^^ MissingCompletion[op] Context[Expression] Trigger[.] Items[0]
        .result(alu_array_result),
//       ^^^^^^ MissingCompletion[result] Context[Expression] Trigger[.] Items[0]
        .zero(alu_array_zero),
//       ^^^^ MissingCompletion[zero] Context[Expression] Trigger[.] Items[0]
        .overflow(alu_array_overflow)
//       ^^^^^^^^ MissingCompletion[overflow] Context[Expression] Trigger[.] Items[0]
    );

    // Instance array of length 1 - using simple_counter module
    logic counter_enable [1];
    logic [7:0] counter_count [1];

    simple_counter #(
        .WIDTH(8)
//       ^^^^^ MissingCompletion[WIDTH] Context[Expression] Trigger[.] Items[0]
    ) counter_inst [0:0] (
        .clk(clk),
//       ^^^ MissingCompletion[clk] Context[Expression] Trigger[.] Items[0]
        .rst_n(rst_n),
//       ^^^^^ MissingCompletion[rst_n] Context[Expression] Trigger[.] Items[0]
        .enable(counter_enable),
//       ^^^^^^ MissingCompletion[enable] Context[Expression] Trigger[.] Items[0]
        .count(counter_count)
//       ^^^^^ MissingCompletion[count] Context[Expression] Trigger[.] Items[0]
    );

    // Generate block with generate array
    genvar i;
    generate
        for (i = 0; i < 3; i++) begin : gen_alu_array
            logic [DATA_WIDTH-1:0] gen_alu_a, gen_alu_b, gen_alu_result;
            alu_op_t gen_alu_op;
//          ^^^^^^^^ MissingCompletion[alu_op_t] Context[ModuleMember] Trigger[Invoked] Items[17]
            logic gen_alu_zero, gen_alu_overflow;

            alu #(
                .WIDTH(DATA_WIDTH)
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

    // State machine
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc <= '0;
            state <= CPU_RESET;
//                   ^^^^^^^^^ MissingCompletion[CPU_RESET] Context[Expression] Trigger[Invoked] Items[33]
            halted <= 1'b0;
        end else begin
            case (state)
                CPU_RESET: begin
//              ^^^^^^^^^ MissingCompletion[CPU_RESET] Context[Procedural] Trigger[Invoked] Items[33]
                    state <= CPU_FETCH;
//                           ^^^^^^^^^ MissingCompletion[CPU_FETCH] Context[Expression] Trigger[Invoked] Items[33]
                end
                CPU_FETCH: begin
//              ^^^^^^^^^ MissingCompletion[CPU_FETCH] Context[Procedural] Trigger[Invoked] Items[33]
                    mem_addr <= pc;
                    mem_req <= 1'b1;
                    if (mem_ack) begin
                        instruction <= mem_rdata;
                        state <= CPU_DECODE;
//                               ^^^^^^^^^^ MissingCompletion[CPU_DECODE] Context[Expression] Trigger[Invoked] Items[33]
                        mem_req <= 1'b0;
                    end
                end
                CPU_DECODE: begin
//              ^^^^^^^^^^ MissingCompletion[CPU_DECODE] Context[Procedural] Trigger[Invoked] Items[33]
                    state <= CPU_EXECUTE;
//                           ^^^^^^^^^^^ MissingCompletion[CPU_EXECUTE] Context[Expression] Trigger[Invoked] Items[33]
                end
                CPU_EXECUTE: begin
//              ^^^^^^^^^^^ MissingCompletion[CPU_EXECUTE] Context[Procedural] Trigger[Invoked] Items[33]
                    // Execute instruction
                    pc <= pc + 4;
                    state <= CPU_FETCH;
//                           ^^^^^^^^^ MissingCompletion[CPU_FETCH] Context[Expression] Trigger[Invoked] Items[33]

                    // Check for halt instruction
                    if (instruction[31:26] == HALT_OPCODE) begin
                        halted <= 1'b1;
                        state <= CPU_HALT;
//                               ^^^^^^^^ MissingCompletion[CPU_HALT] Context[Expression] Trigger[Invoked] Items[33]
                    end
                end
                CPU_HALT: begin
//              ^^^^^^^^ MissingCompletion[CPU_HALT] Context[Procedural] Trigger[Invoked] Items[33]
                    // Stay halted
                end
            endcase
        end
    end

endmodule
