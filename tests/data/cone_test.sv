interface intf();
    logic [31:0] qux;
    logic quz;

    modport quxIn (
        input qux,
        output quz
        );

    modport quxOut (
        output qux,
        input quz
        );
endinterface

module test(
    input logic [31:0] a,
    input logic [31:0] b,
    output logic [31:0] x,
    input logic clk
);
    logic [31:0] x1;

    intf the_intfs [3] ();

    sub the_sub_1(
        .a,
        .b,
        .x (x1),
        .qux_in (the_intfs[0]),
        .qux_out (the_intfs[1]),
        .clk);
    sub the_sub_2(
        .a,
        .b (x1),
        .x (x),
        .qux_in (the_intfs[1]),
        .qux_out (the_intfs[2]),
        .clk);
endmodule

module sub(
    input logic [31:0] a,
    input logic [31:0] b,
    output logic [31:0] x,
    intf.quxIn qux_in,
    intf.quxOut qux_out,
    input logic clk
);

    always_ff @(posedge clk) begin
        x <= a + b;
    end

    always_comb begin
        qux_out.qux = qux_in.qux + b;
        qux_in.quz = qux_out.quz;
    end

    sub_sub the_sub_sub(
        .foo (a[0]),
        .bar (b[0]),
        .clk);
endmodule

module sub_sub(
    input logic foo,
    input logic bar,
    input logic clk
);

    logic result;
    logic nope;

    always_ff @(posedge clk) begin
        if (foo) begin
            result <= bar;
        end else begin
            result <= '1;
        end

        nope <= '0;
    end

    logic switched_result;
    always_comb begin
        case(bar)
            1'b0: switched_result = foo;
            1'b1: switched_result = 1'b0;
        endcase
    end
endmodule
