interface StreamBusBundle;
    StreamBus #(.width(16), .payload_t(longint)) nested_bus();
endinterface

module StageTapWrapper #(
    parameter int width,
    parameter type data_t
) (
    input wire [width-1:0] data_in,
    input wire data_clk,
    StreamBus.source data_bus
);
    StageTap #(.width(width), .data_t(data_t)) tap(
        .data_in,
        .data_clk,
        .data_bus(data_bus)
    );
endmodule

module SelectedStageTapWrapper(
    input wire [15:0] data_in,
    input wire data_clk,
    StreamBusBundle bundle
);
    StageTap #(.width(16), .data_t(longint)) selected_tap(
        .data_in,
        .data_clk,
        .data_bus(bundle.nested_bus)
    );
endmodule

module top;
    logic [7:0] lane_data8;
    logic [15:0] lane_data16;
    logic lane_clk;

    StreamBus #(.width(8), .payload_t(byte)) bus8();
    StreamBus #(.width(16), .payload_t(longint)) bus16();
    StreamBus #(.width(32), .payload_t(shortint)) bus_array[2]();
    StreamBusBundle bundle();

    StageTap #(.width(8), .data_t(byte)) tap8(
        .data_in(lane_data8),
        .data_clk(lane_clk),
        .data_bus(bus8)
    );

    StageTap #(.width(16), .data_t(int)) tap16(
        .data_in(lane_data16),
        .data_clk(lane_clk),
        .data_bus(bus16)
    );

    StageTapWrapper #(.width(8), .data_t(byte)) wrapper8(
        .data_in(lane_data8),
        .data_clk(lane_clk),
        .data_bus(bus8)
    );

    SelectedStageTapWrapper selected_wrapper(
        .data_in(lane_data16),
        .data_clk(lane_clk),
        .bundle(bundle)
    );

    BusArrayTap array_tap(.buses(bus_array));
endmodule
