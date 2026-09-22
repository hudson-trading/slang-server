module shared_mod(input logic from_b);
endmodule

interface shared_bus;
    logic from_b;
    modport view_b(input from_b);
endinterface
