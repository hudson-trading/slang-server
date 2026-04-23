`define USE_PKG_NUM(name) localparam int name = pkg::num;
`define USE_OTHER_NUM(name) localparam int name = other_pkg::num;

`USE_PKG_NUM(FROM_INCLUDE0)
`USE_PKG_NUM(FROM_INCLUDE1)
`USE_OTHER_NUM(OTHER_FROM_INCLUDE0)
