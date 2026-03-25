// Macro calls with nested parentheses and strings
`SIMPLE_MACRO(a, b)
`NESTED_MACRO(foo(bar), baz)
`DEEP_MACRO(foo(bar(baz)), qux)
`STRING_MACRO("hello world", 42)
`MIXED_MACRO(foo("test"), bar(baz))
`BITS_MACRO($bits(some_type_t))

module after_macros;
  logic [7:0] data;
endmodule
