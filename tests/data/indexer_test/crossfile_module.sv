// Module that uses definitions from crossfile_pkg
module crossfile_user
    import crossfile_pkg::*;
#(
    parameter int DEPTH = FIFO_DEPTH
)(
    input logic clk,
    input logic rst,
    input transaction_t trans_in,
    output transaction_t trans_out
);
    transaction_t buffer[DEPTH];
    logic [$clog2(DEPTH)-1:0] wr_ptr;
    logic [$clog2(DEPTH)-1:0] rd_ptr;

    int total_size;

    initial begin
        total_size = calculate_size(DEPTH);
    end

    always_ff @(posedge clk) begin
        if (rst) begin
            wr_ptr <= '0;
            rd_ptr <= '0;
        end else begin
            buffer[wr_ptr] <= trans_in;
            trans_out <= buffer[rd_ptr];
            wr_ptr <= wr_ptr + 1;
            rd_ptr <= rd_ptr + 1;
        end
    end
endmodule

module crossfile_top;
    logic clk;
    logic rst;
    crossfile_pkg::transaction_t t1, t2;

    crossfile_user #(
        .DEPTH(crossfile_pkg::FIFO_DEPTH)
    ) u_user (
        .clk(clk),
        .rst(rst),
        .trans_in(t1),
        .trans_out(t2)
    );
endmodule
