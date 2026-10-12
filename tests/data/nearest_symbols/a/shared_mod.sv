module shared_mod(input logic from_a);
endmodule

interface shared_bus;
    logic from_a;
    modport view_a(input from_a);
endinterface
