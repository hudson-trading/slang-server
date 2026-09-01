module BusHolder(StreamBus upstream);
    StreamBus selected();
endmodule

module top;
    StreamBus upstream();
    BusHolder holder(.upstream);
endmodule
