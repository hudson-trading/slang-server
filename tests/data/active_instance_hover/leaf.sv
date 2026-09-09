module leaf #(
    parameter int WIDTH = 1,
    parameter bit ENABLED = 1'b0
) ();
    localparam int DOUBLE_WIDTH = WIDTH * 2;
    logic [WIDTH-1:0] data;

    for (genvar i = 0; i < 2; i++) begin : lanes
        generated_leaf lane();
    end

    for (genvar j = 0; j < 3; j++) begin : channels
        generated_leaf channel();
    end
endmodule
