import transitive_pkg::*;

interface transitive_if;
    payload_t payload;

    modport consumer_mp(input payload);
endinterface
