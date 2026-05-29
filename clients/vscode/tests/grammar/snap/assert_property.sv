module top;
    ASSERT_LABEL: assert property (@(posedge clk) disable iff (rst) func_a(x) == func_a(y));

    always @(posedge clk) if (~rst) assert property (~sig_a);

    assume property (@(posedge clk) foo);
    cover property (bar);
endmodule
