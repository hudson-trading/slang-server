module Foo #(
) (
);
    always_comb begin
        out_data = '{
            field_a: cond ? '0 : in_data,
            field_b: cond || other_cond
        };
    end

    always_ff @(posedge clk) begin
        if (rst) begin
            state.valid <= '0;
            state.held <= '0;
            state.prev <= 'x;
        end else begin
        end
    end
endmodule
