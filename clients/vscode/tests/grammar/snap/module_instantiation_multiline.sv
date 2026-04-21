module top;
    some_module
    inst_name
        (.A(sig_a)
        ,.B(/* TODO */)
        ,.X(/* TODO */)
        );

    some_module #(
        .WIDTH(1)
    )
    inst_with_params
        (.A(sig_b)
        ,.B(/* TODO */)
        ,.X(/* TODO */)
        );

    parameterized_block
    #(.param_a(width_a),
         .param_b(width_b),
         .param_c(width_c))
    block_inst
       (.signal_a,

        .signal_b,
        .signal_c,

        .signal_d,

        .signal_e(bus_e[IDX]),

        .signal_f(bus_f[IDX]));
endmodule
