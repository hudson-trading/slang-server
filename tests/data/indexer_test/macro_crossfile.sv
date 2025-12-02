// Test file for cross-file macro references

`include "macro_refs.sv"

module macro_user;
    logic clk, rst;
    logic [7:0] data_in, data_out;
    logic [7:0] my_counter;
    logic [7:0] storage;

    macro_test dut (
        .clk(clk),
        .rst(rst),
        .din(data_in),
        .dout(data_out)
    );

    initial begin
        `ASSIGN_ZERO(my_counter);
        `ASSIGN_VALUE(storage, data_out);

        if (`COMPARE(my_counter, storage)) begin
            `INCREMENT(my_counter);
        end
    end

endmodule
