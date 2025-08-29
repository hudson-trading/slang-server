`include "cpu_defines.svh"

module memory_controller #(
    parameter int ADDR_WIDTH = 32,
    parameter int DATA_WIDTH = 32
) (
    input  logic                    clk,
    input  logic                    rst_n,

    // CPU interface
    input  logic [ADDR_WIDTH-1:0]  cpu_addr,
    input  logic [DATA_WIDTH-1:0]  cpu_wdata,
    output logic [DATA_WIDTH-1:0]  cpu_rdata,
    input  logic                    cpu_we,
    input  logic                    cpu_req,
    output logic                    cpu_ack
);

    // Simple memory model
    logic [DATA_WIDTH-1:0] memory [0:1023];
    logic [1:0] state;

    localparam IDLE = 2'b00;
    localparam READ = 2'b01;
    localparam WRITE = 2'b10;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= IDLE;
            cpu_ack <= 1'b0;
            cpu_rdata <= '0;
        end else begin
            case (state)
                IDLE: begin
                    cpu_ack <= 1'b0;
                    if (cpu_req) begin
                        if (cpu_we) begin
                            state <= WRITE;
                        end else begin
                            state <= READ;
                        end
                    end
                end

                READ: begin
                    cpu_rdata <= memory[cpu_addr[11:2]]; // Word-aligned access
                    cpu_ack <= 1'b1;
                    state <= IDLE;
                end

                WRITE: begin
                    memory[cpu_addr[11:2]] <= cpu_wdata; // Word-aligned access
                    cpu_ack <= 1'b1;
                    state <= IDLE;
                end

                default: begin
                    state <= IDLE;
                end
            endcase
        end
    end

    // Initialize memory with some test data
    initial begin
        for (int i = 0; i < 1024; i++) begin
            memory[i] = i * 4;
        end
    end

endmodule
