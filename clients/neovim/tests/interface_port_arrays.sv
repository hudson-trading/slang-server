interface test_bus;
    logic valid;
endinterface

module consumer(test_bus bus, test_bus buses[-1:0]);
endmodule

module interface_port_top;
    test_bus bus();
    test_bus buses[-1:0]();
    consumer dut(.bus(bus), .buses(buses));
endmodule
