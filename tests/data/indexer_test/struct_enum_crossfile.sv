// Cross-file test for struct and enum member references

`include "struct_enum_refs.sv"

module struct_enum_user;
    logic clk, rst;
    transaction_s my_tx;
    bus_packet_s my_packet;
    state_e state;
    command_e command;

    struct_enum_test dut (
        .clk(clk),
        .rst(rst)
    );

    initial begin
        // Use struct members
        my_tx.addr = 8'hFF;
        my_tx.data = 32'hDEADBEEF;
        my_tx.valid = 1'b1;
        my_tx.ready = 1'b0;

        // Use nested struct members
        my_packet.request.addr = my_tx.addr;
        my_packet.request.data = my_tx.data;
        my_packet.response.valid = my_tx.valid;
        my_packet.id = 4'hA;

        // Use enum members
        state = IDLE;
        command = CMD_READ;

        if (state == ACTIVE) begin
            command = CMD_WRITE;
        end else if (state == ERROR) begin
            command = CMD_ERASE;
        end
    end

    always @(posedge clk) begin
        if (my_tx.ready && my_packet.response.ready) begin
            state <= WAIT;
        end
    end

endmodule
