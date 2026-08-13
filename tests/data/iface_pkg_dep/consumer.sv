module consumer (
    simple_if bus,
    output logic valid
);
  assign valid = bus.payload.valid;
endmodule
