`ifndef BUS_INTERFACE_SVH
`define BUS_INTERFACE_SVH

// Bus interface definition
interface bus_if #(
    parameter int ADDR_WIDTH = 32,
    parameter int DATA_WIDTH = 32
) (
    input logic clk,
    input logic rst_n
);

    logic [ADDR_WIDTH-1:0] addr;
    logic [DATA_WIDTH-1:0] wdata;
    logic [DATA_WIDTH-1:0] rdata;
    logic                  we;
    logic                  req;
    logic                  ack;
    logic                  valid;

    // Master modport (for CPU)
    modport master (
        output addr,
        output wdata,
        input  rdata,
        output we,
        output req,
        input  ack,
        output valid
    );

    // Slave modport (for memory)
    modport slave (
        input  addr,
        input  wdata,
        output rdata,
        input  we,
        input  req,
        output ack,
        input  valid
    );

    // Monitor modport (for verification)
    modport monitor (
        input addr,
        input wdata,
        input rdata,
        input we,
        input req,
        input ack,
        input valid
    );

endinterface

// Transaction types
typedef enum logic [1:0] {
    BUS_IDLE  = 2'b00,
    BUS_READ  = 2'b01,
    BUS_WRITE = 2'b10,
    BUS_ERROR = 2'b11
} bus_transaction_t;

`endif // BUS_INTERFACE_SVH
