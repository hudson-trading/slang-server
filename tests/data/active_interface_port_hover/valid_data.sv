interface StreamBus #(
    parameter int width = 1,
    parameter type payload_t = logic
) ();
    logic [width-1:0] payload;
    payload_t typed_payload;
    modport source(output payload, typed_payload);
endinterface
