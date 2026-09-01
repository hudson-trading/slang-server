module leaf #(
    parameter type selected_t = types_pkg::side_t
) (
    input selected_t selected,
    output types_pkg::side_t local_value
);
    assign local_value = selected;
endmodule
