interface simple_if;
    import types_pkg::*;

    payload_t payload;

    modport producer_mp(output payload);
    modport consumer_mp(input payload);
endinterface
