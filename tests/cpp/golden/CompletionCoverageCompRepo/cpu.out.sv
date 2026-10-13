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
    output logic                    halted
);

    // Internal registers
    logic [DATA_WIDTH-1:0] pc;
    logic [DATA_WIDTH-1:0] instruction;
    logic [DATA_WIDTH-1:0] register_file [0:31];

    // ALU instance
    alu #(
        .WIDTH(DATA_WIDTH)
    ) alu_inst (
        .a(register_file[instruction[25:21]]),
        .b(register_file[instruction[20:16]]),
        .op(instruction[31:26]),
        .result(alu_result),
        .zero(alu_zero),
        .overflow(alu_overflow)
    );

    // Memory controller instance
    memory_controller #(
        .ADDR_WIDTH(ADDR_WIDTH),
        .DATA_WIDTH(DATA_WIDTH)
    ) mem_ctrl (
        .clk(clk),
        .rst_n(rst_n),
        .cpu_addr(mem_addr),
        .cpu_wdata(mem_wdata),
        .cpu_rdata(mem_rdata),
        .cpu_we(mem_we),
        .cpu_req(mem_req),
        .cpu_ack(mem_ack)
    );

    logic [DATA_WIDTH-1:0] alu_result;
    logic alu_zero, alu_overflow;

    // Instance array - array of 4 ALU instances
    logic [DATA_WIDTH-1:0] alu_array_a [4];
    logic [DATA_WIDTH-1:0] alu_array_b [4];
    logic [DATA_WIDTH-1:0] alu_array_result [4];
    alu_op_t alu_array_op [4];
    logic alu_array_zero [4];
    logic alu_array_overflow [4];

    alu #(
        .WIDTH(DATA_WIDTH)
    ) alu_inst_array [3:0] (
        .a(alu_array_a),
        .b(alu_array_b),
        .op(alu_array_op),
        .result(alu_array_result),
        .zero(alu_array_zero),
        .overflow(alu_array_overflow)
    );

    // Instance array of length 1 - using simple_counter module
    logic counter_enable [1];
    logic [7:0] counter_count [1];

    simple_counter #(
        .WIDTH(8)
    ) counter_inst [0:0] (
        .clk(clk),
        .rst_n(rst_n),
        .enable(counter_enable),
        .count(counter_count)
    );

    // Generate block with generate array
    genvar i;
    generate
        for (i = 0; i < 3; i++) begin : gen_alu_array
            logic [DATA_WIDTH-1:0] gen_alu_a, gen_alu_b, gen_alu_result;
            alu_op_t gen_alu_op;
            logic gen_alu_zero, gen_alu_overflow;

            alu #(
                .WIDTH(DATA_WIDTH)
            ) gen_alu_inst (
                .a(gen_alu_a),
                .b(gen_alu_b),
                .op(gen_alu_op),
                .result(gen_alu_result),
                .zero(gen_alu_zero),
                .overflow(gen_alu_overflow)
            );
        end
    endgenerate

    // State machine
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc <= '0;
            state <= CPU_RESET;
            halted <= 1'b0;
        end else begin
            case (state)
                CPU_RESET: begin
                    state <= CPU_FETCH;
                end
                CPU_FETCH: begin
                    mem_addr <= pc;
                    mem_req <= 1'b1;
                    if (mem_ack) begin
                        instruction <= mem_rdata;
                        state <= CPU_DECODE;
                        mem_req <= 1'b0;
                    end
                end
                CPU_DECODE: begin
                    state <= CPU_EXECUTE;
                end
                CPU_EXECUTE: begin
                    // Execute instruction
                    pc <= pc + 4;
                    state <= CPU_FETCH;

                    // Check for halt instruction
                    if (instruction[31:26] == HALT_OPCODE) begin
                        halted <= 1'b1;
                        state <= CPU_HALT;
                    end
                end
                CPU_HALT: begin
                    // Stay halted
                end
            endcase
        end
    end

endmodule
