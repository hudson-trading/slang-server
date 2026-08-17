// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

module simple_counter #(
    parameter int WIDTH = 8
) (
    input  logic             clk,
    input  logic             rst_n,
    input  logic             enable,
    output logic [WIDTH-1:0] count
);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            count <= '0;
        else if (enable)
            count <= count + 1'b1;
    end

    class completion_class #(parameter int MEMBER_WIDTH);
        function void run(input logic [MEMBER_WIDTH-1:0] arg);
        endfunction
    endclass

    completion_class #(8) wide;
    completion_class #(2) narrow;
    struct {
        logic field_a;
    } value;

    initial begin
        wide.run('0);
        narrow.run('0);
        value.field_a = '0;
    end

endmodule
