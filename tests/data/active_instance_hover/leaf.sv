module leaf #(
    parameter int WIDTH = 1
) ();
    logic [WIDTH-1:0] data;

    for (genvar i = 0; i < 2; i++) begin : lanes
        generated_leaf lane();
    end

    for (genvar j = 0; j < 3; j++) begin : channels
        generated_leaf channel();
    end
endmodule
