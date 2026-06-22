module producer (
    simple_if.producer_mp bus
);
  always_comb begin
    bus.payload.valid = 1'b1;
    bus.payload.data  = 8'h42;
  end
endmodule
