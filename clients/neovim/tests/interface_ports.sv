interface bus_if;
    logic valid;
endinterface

module uses_if(bus_if single_bus, bus_if bus_array[2]);
endmodule

module interface_top;
    bus_if single_bus();
    bus_if bus_array[2]();
    uses_if u(.single_bus, .bus_array);
endmodule
