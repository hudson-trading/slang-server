module child;
endmodule

module leaf;
endmodule

module branch;
    leaf nested_leaf();
endmodule

module top;
    child u_child();
    branch branch_array[1:0]();

    for (genvar i = 0; i < 2; i++) begin : generated
        leaf generated_leaf();
    end
endmodule
