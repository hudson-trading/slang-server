interface cross_if;
    logic ready;
    modport producer(output ready);
    modport consumer(input ready);
endinterface
