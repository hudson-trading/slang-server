`SIMPLE_MACRO(a, b)
`NO_ARGS_MACRO
`ANOTHER_NO_ARGS

`define GEN_SLOT(index_) \
    logic gen_slot_``index_``_data; \
    logic gen_slot_``index_

module top;
  `BARE_MACRO
  logic x = `OTHER_MACRO;
  `WITH_ARGS(foo)
endmodule
