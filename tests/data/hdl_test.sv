package test_pkg;
    parameter int WIDTH = 32;
    typedef logic [WIDTH-1:0] id_t;

    typedef struct packed {
        id_t id;
        logic [15:0] data;
    } packet_t;

    typedef enum logic [1:0] {
        STATE_A,
        STATE_B,
        STATE_C
    } state_t;

    parameter type data_t = logic [7:0];
endpackage

interface bus_if #(
    parameter type data_t = bit,
    parameter int ADDR_WIDTH = 32
)(
    input logic clk,
    input logic rst
);

    logic valid;
    logic ready;
    logic [ADDR_WIDTH-1:0] addr;
    data_t data;
    logic write_enable;

    // Modport for master
    modport master (
        input clk, rst, ready,
        output valid, addr, data, write_enable
    );

    // Modport for follower
    modport follower (
        input clk, rst, valid, addr, data, write_enable,
        output ready
    );

    // Task for master to write data
    task automatic write_data(input logic [ADDR_WIDTH-1:0] address, input data_t write_data);
        @(posedge clk);
        addr <= address;
        data <= write_data;
        write_enable <= 1'b1;
        valid <= 1'b1;
        @(posedge clk iff ready);
        valid <= 1'b0;
        write_enable <= 1'b0;
    endtask

    // Task for master to read data
    task automatic read_data(input logic [ADDR_WIDTH-1:0] address, output data_t read_data);
        @(posedge clk);
        addr <= address;
        write_enable <= 1'b0;
        valid <= 1'b1;
        @(posedge clk iff ready);
        read_data = data;
        valid <= 1'b0;
    endtask

endinterface

module Sub #(
    parameter int WIDTH = 16
)(
    input logic clk,
    input logic rst,
    input logic [WIDTH-1:0] data_in,
    output logic [WIDTH-1:0] data_out
);

    always_ff @(posedge clk or posedge rst) begin
        if (rst) begin
            data_out <= '0;
        end else begin
            data_out <= data_in;
        end
    end

endmodule

module TestModule (
    input logic clk,
    input logic rst,
    input logic [test_pkg::WIDTH-1:0] width_port,
    input test_pkg::id_t [test_pkg::WIDTH-1:0] id_array,
    input test_pkg::packet_t pkt_in,
    output test_pkg::data_t data_out,
    bus_if.master bus_master,
    bus_if.follower bus_follower
);

    test_pkg::state_t state, state_next;
    test_pkg::id_t counter;

    always_ff @(posedge clk) begin
        if (rst) begin
            state <= test_pkg::STATE_A;
            counter <= '0;
        end else begin
            state <= state_next;
            counter <= counter + 1'b1;
        end
    end

    always_comb begin
        case (state)
            test_pkg::STATE_A: state_next = test_pkg::STATE_B;
            test_pkg::STATE_B: state_next = test_pkg::STATE_C;
            default: state_next = test_pkg::STATE_A;
        endcase
    end

    // Use the bus interfaces
    always_ff @(posedge clk) begin
        if (rst) begin
            data_out <= '0;
            bus_follower.ready <= 1'b0;
        end else begin
            // Simple follower logic - always ready, echo data
            bus_follower.ready <= 1'b1;
            if (bus_follower.valid && bus_follower.write_enable) begin
                data_out <= bus_follower.data[7:0]; // Convert to data_t size
            end
        end
    end

    // Master interface usage example
    initial begin
        bus_master.valid <= 1'b0;
        bus_master.write_enable <= 1'b0;
        bus_master.addr <= '0;
        bus_master.data <= state;
    end

endmodule


module NoDefaults #(
    parameter int WIDTH,
    parameter int n_iters,
    parameter bit gen_branch = 0
)(
    input logic clk,
    input logic rst,
    input logic [WIDTH-1:0] data_in,
    output logic [WIDTH-1:0] data_out
);

    Sub #(
        .WIDTH(WIDTH)
    ) sub_inst (
        .clk(clk),
        .rst(rst),
        .data_in(data_in),
        .data_out(data_out)
    );

    genvar i;
    for(i = 0; i < n_iters; i++) begin : gen_loop
        Sub #(
            .WIDTH(WIDTH)
         ) sub (
            .clk(clk),
            .rst(rst),
            .data_in(data_in),
            .data_out(data_out)
        );
    end

    if (gen_branch) begin : gen_branch_block
        Sub #(
            .WIDTH(WIDTH)
        ) sub_branch (
            .clk(clk),
            .rst(rst),
            .data_in(data_in),
            .data_out(data_out)
        );
    end else begin : gen_no_branch_block
        Sub #(
            .WIDTH(WIDTH)
        ) sub_no_branch (
            .clk(clk),
            .rst(rst),
            .data_in(data_in),
            .data_out(data_out)
        );
    end

endmodule
