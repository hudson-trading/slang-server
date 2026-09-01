package empty_pkg;
endpackage

package params_pkg;
    localparam int PACKAGE_PARAM = 1;
endpackage

module child;
endmodule

module leaf;
endmodule

module branch;
    leaf nested_leaf();
endmodule

module top;
    logic data_signal;
    child u_child();
    branch branch_array[1:0]();

    if (1) begin : params_only
        localparam int HIDDEN = 1;
    end

    for (genvar i = 0; i < 2; i++) begin : generated
        leaf generated_leaf();
    end
endmodule
