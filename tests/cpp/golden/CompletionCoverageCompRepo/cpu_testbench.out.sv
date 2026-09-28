`include "cpu_defines.svh"
`include "bus_interface.svh"

module cpu_testbench;

    // Clock and reset
    logic clk;
    logic rst_n;

    // CPU signals
    logic [31:0] mem_addr;
    logic [31:0] mem_wdata;
    logic [31:0] mem_rdata;
    logic        mem_we;
    logic        mem_req;
    logic        mem_ack;
    cpu_state_t  cpu_state;
    logic        halted;

    // Clock generation
    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    // Reset generation
    initial begin
        rst_n = 1'b0;
        #100;
        rst_n = 1'b1;
        #1000;
        $finish;
    end

    // CPU instance
    cpu #(
        .DATA_WIDTH(32),
        .ADDR_WIDTH(32)
    ) dut (
        .clk(clk),
        .rst_n(rst_n),
        .mem_addr(mem_addr),
        .mem_wdata(mem_wdata),
        .mem_rdata(mem_rdata),
        .mem_we(mem_we),
        .mem_req(mem_req),
        .mem_ack(mem_ack),
        .state(cpu_state),
        .halted(halted)
    );

    // Simple memory model for testing
    logic [31:0] test_memory [0:255];

    initial begin
        // Initialize test memory with simple program
        test_memory[0] = {`ADD_OPCODE, 5'd1, 5'd2, 5'd3, 11'd0}; // ADD r3, r1, r2
        test_memory[1] = {`SUB_OPCODE, 5'd3, 5'd1, 5'd4, 11'd0}; // SUB r4, r3, r1
        test_memory[2] = {`HALT_OPCODE, 26'd0};                   // HALT

        for (int i = 3; i < 256; i++) begin
            test_memory[i] = 32'h00000000;
        end
    end

    // Memory response logic
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            mem_ack <= 1'b0;
            mem_rdata <= 32'h0;
        end else begin
            mem_ack <= mem_req;
            if (mem_req && !mem_we) begin
                mem_rdata <= test_memory[mem_addr[9:2]];
            end else if (mem_req && mem_we) begin
                test_memory[mem_addr[9:2]] <= mem_wdata;
            end
        end
    end

    // Monitor
    always_ff @(posedge clk) begin
        if (rst_n) begin
            $display("Time: %0t, State: %s, PC: 0x%08x, Halted: %b",
                     $time, cpu_state.name(), mem_addr, halted);
        end
    end

endmodule
