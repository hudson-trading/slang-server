package features;
    localparam int ITEM = 4;
endpackage
package forwarded;
    import features::ITEM;
    export features::ITEM;
endpackage
interface bus;
    logic [3:0] data;
    task run(input logic value);
    endtask
    modport endpoint(output data, import task run(input logic value));
endinterface
module leaf(input logic [3:0] shared);
endmodule
module top;
    import duplicate_pkg::*;
    logic [3:0] shared;
    leaf u(.shared);
    bus b();
    duplicate d();
    for (genvar i = 0; i < 2; i++) begin : g
        localparam int VALUE = i;
    end
endmodule
`ifdef MULTI_MACRO
`endif
