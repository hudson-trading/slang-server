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
    parameter type data_t,
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

module TestModule #(
    parameter int NUM_SUBS = 4
)(
    input logic clk,
    input logic rst,
    // some useful info
    input logic          [test_pkg::WIDTH-1:0] width_port,
    input test_pkg::id_t [test_pkg::WIDTH-1:0] id_array,
    input test_pkg::packet_t pkt_in,
    output test_pkg::data_t data_out,
    bus_if.master bus_master,
    bus_if.follower bus_follower
);

    test_pkg::state_t state, state_next;

    // This comment is just here to test the multi line
    // comment functionality.
    test_pkg::id_t counter;

    // Instance array that depends on NUM_SUBS parameter
    logic [15:0] sub_data_in [NUM_SUBS];
    logic [15:0] sub_data_out [NUM_SUBS];

    Sub #(.WIDTH(16)) sub_inst [NUM_SUBS] (
        .clk(clk),
        .rst(rst),
        .data_in(sub_data_in),
        .data_out(sub_data_out)
    );

    // Test instance array access - goto on sub_inst[0] should go to sub_inst declaration
    logic [15:0] inst_array_test;
    assign inst_array_test = sub_inst[0].data_out + sub_inst[1].data_out;

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
        bus_master.addr = width_port[0];
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

    // 2D instance array
    Sub #(.WIDTH(WIDTH)) sub_2d [2][3] (
        .clk(clk),
        .rst(rst),
        .data_in(data_in),
        .data_out()
    );

    // Test 2D instance array access
    logic [WIDTH-1:0] test_2d_access;
    assign test_2d_access = sub_2d[0][1].data_out + sub_2d[1][2].data_out;

    genvar j;
    for(j = 0; j < 3; j++) begin : gen_loop_2
        Sub #(.WIDTH(WIDTH)) gen_sub_array [4] (
            .clk(clk),
            .rst(rst),
            .data_in(data_in),
            .data_out()
        );
    end

    // Test instance array inside generate scope access
    logic [WIDTH-1:0] test_gen_array_access;
    assign test_gen_array_access = gen_loop_2[0].gen_sub_array[2].data_out;

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

module StructAssignmentTest (
    input logic clk,
    input logic rst
);

    // Struct definitions for testing
    typedef struct packed {
        logic [7:0] addr;
        logic [15:0] data;
        logic valid;
    } simple_struct_t;

    typedef struct packed {
        simple_struct_t inner;
        logic [3:0] tag;
        logic enable;
    } nested_struct_t;

    // Variables for testing
    simple_struct_t single_struct;
    simple_struct_t struct_array[4];
    nested_struct_t nested_struct;
    nested_struct_t nested_array[2];
    test_pkg::packet_t pkg_struct;
    test_pkg::packet_t pkg_array[3];

    logic [1:0] index;
    logic [7:0] addr_val;
    logic [15:0] data_val;

    always_ff @(posedge clk) begin
        if (rst) begin
            // Basic struct member assignment
            single_struct.addr <= 8'h00;
            single_struct.data <= 16'h0000;
            single_struct.valid <= 1'b0;

            // Array index struct member assignment
            struct_array[0].addr <= 8'hAA;
            struct_array[1].data <= 16'hBBBB;
            struct_array[2].valid <= 1'b1;

            // Variable index struct member assignment
            struct_array[index].addr <= 8'hCC;
            struct_array[index].data <= data_val;

            // Nested struct member assignment
            nested_struct.inner.addr <= 8'hDD;
            nested_struct.inner.data <= 16'hEEEE;
            nested_struct.inner.valid <= 1'b0;
            nested_struct.tag <= 4'h5;
            nested_struct.enable <= 1'b1;

            // Nested struct array assignment
            nested_array[0].inner.addr <= 8'hFF;
            nested_array[1].inner.data <= 16'h1234;
            nested_array[index].tag <= 4'h7;

            // Package struct member assignment
            pkg_struct.id <= 32'h123;
            pkg_struct.data <= 16'h5678;

            // Package struct array assignment
            pkg_array[0].id <= 32'h456;
            pkg_array[1].data <= 16'h9ABC;
            pkg_array[index].id <= {24'h0, addr_val};

            index <= 2'b00;
            addr_val <= 8'h11;
            data_val <= 16'h2222;
        end else begin
            // Runtime assignments with expressions
            single_struct.addr <= addr_val + 8'h10;
            single_struct.data <= data_val << 1;
            single_struct.valid <= ~single_struct.valid;

            // Complex array indexing with struct members
            struct_array[index + 1].addr <= single_struct.addr;
            struct_array[addr_val[1:0]].data <= nested_struct.inner.data;

            // Chained struct member assignments
            nested_struct.inner.addr <= struct_array[0].addr;
            nested_struct.inner.data <= pkg_struct.data;
            nested_struct.inner.valid <= pkg_array[index].id[0];

            // Multi-dimensional style access
            nested_array[index].inner.addr <= struct_array[addr_val[1:0]].addr;
            nested_array[~index].inner.data <= pkg_array[index + 1].data;

            // Expression-based assignments
            pkg_struct.id <= {16'h0, addr_val, data_val[7:0]};
            pkg_array[index].data <= single_struct.data ^ nested_struct.inner.data;

            // Increment index for next cycle
            index <= index + 1;
            addr_val <= addr_val + 8'h01;
            data_val <= data_val + 16'h0011;
        end
    end

    // Combinational assignments
    always_comb begin
        // Direct member assignments in combinational logic
        if (single_struct.valid) begin
            nested_struct.tag = struct_array[0].addr[3:0];
            nested_struct.enable = |pkg_struct.id;
        end else begin
            nested_struct.tag = 4'h0;
            nested_struct.enable = 1'b0;
        end
    end

    // Initial block assignments
    initial begin
        single_struct.addr = 8'h77;
        single_struct.data = 16'h8888;
        single_struct.valid = 1'b1;

        struct_array[3].addr = 8'h99;
        struct_array[3].data = 16'hAAAA;
        struct_array[3].valid = 1'b0;

        nested_array[1].inner.addr = 8'hBB;
        nested_array[1].inner.data = 16'hCCCC;
        nested_array[1].tag = 4'hD;

        pkg_array[2].id = 32'hDEAD;
        pkg_array[2].data = 16'hBEEF;
    end

endmodule
