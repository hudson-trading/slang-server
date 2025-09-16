module foo;
    for(genvar i = 0; i < 4; i++) begin: gen_loop
        sub #(.param(i)) the_sub();
    end
endmodule

module sub #(
    parameter int param
);
endmodule
