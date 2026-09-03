module StageTap #(
    parameter int width,
    parameter type data_t
) (
    input wire [width-1:0] data_in,
    input wire data_clk,
    StreamBus.source data_bus
);
    data_t typed_data;
    assign typed_data = data_in;
    localparam int interface_width = data_bus.width;
    localparam int bus_width = $bits(data_bus.payload);
    localparam int typed_bus_width = $bits(data_bus.typed_payload);
endmodule

module BusArrayTap(StreamBus.source buses[2]);
endmodule
