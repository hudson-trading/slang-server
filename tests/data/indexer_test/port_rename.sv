// Test file for port rename functionality
module child_module (
    input logic clk,
    input logic rst,
    output logic [7:0] data_out
);
    // Use the ports within the module
    always_ff @(posedge clk or posedge rst) begin
        if (rst)
            data_out <= '0;
        else
            data_out <= data_out + 1;
    end
endmodule

module parent_module;
    logic sys_clk;
    logic sys_rst;
    logic [7:0] result;

    // Instance with named port connections
    child_module u_child (
        .clk(sys_clk),
        .rst(sys_rst),
        .data_out(result)
    );
endmodule
