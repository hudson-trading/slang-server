module top;
    logic some_var;
    logic other_var;
    logic [1:0] this_var;
    logic [3:0] nested_var;

    assign this_var = {
        some_var,
        other_var
    };

    assign nested_var = {
        {some_var, other_var},
        2{some_var}
    };

    some_module
    inst_name
        (.A(some_var));
endmodule
