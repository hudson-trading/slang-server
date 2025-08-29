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
