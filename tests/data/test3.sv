module test;
    logic foo, bar;
    always_comb foo = bar;

    sub the_sub_1();
    sub the_sub_2();

    other_sub the_other_sub();
endmodule

module sub;
    logic [15:0] baz;
    always_comb baz = '1;

    intf the_intf_1();
    intf the_intf_2();
endmodule

interface intf;
    logic sig1;
    logic sig2;
endinterface

package pkg;
    typedef struct packed {
        logic abc;
        logic def;
    } type_2_t;

    typedef struct packed {
        type_2_t t2;
    } type_1_t;
endpackage

module other_sub;
    pkg::type_1_t t1;
    initial $display(t1.t2.abc);

    typedef enum {
        FOO,
        BAR,
        BAZ
    } enum_t;
    enum_t the_enum;
    always_comb the_enum = BAR;

    logic [31:0] the_array [8];
    initial $display(the_array[4]);

    intf2 some_intf();
    sub_w_intf the_sub_w_intf(
        .all_in_port (some_intf),
        .intf_port (some_intf)
    );
endmodule

module sub_w_intf(
    intf2.all_in all_in_port,
    intf2 intf_port
);
    initial begin
        $display(all_in_port.def);
        $display(intf_port.abc);
    end

    if (1) initial $display(all_in_port.abc);
endmodule

interface intf2;
    logic abc;
    logic def;

    modport all_in(
        input abc, def
    );

    modport all_out(
        output abc, def
    );
endinterface
