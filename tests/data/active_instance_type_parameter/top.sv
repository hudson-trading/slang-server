module wrapper #(parameter type T = types_pkg::side_t);
    leaf #(.selected_t(T)) inst();
endmodule

module top;
    typedef types_pkg::side_t alias_t;
    typedef alias_t chained_t;
    typedef enum logic { LocalFirst, LocalSecond } local_t;

    leaf #(.selected_t(types_pkg::side_t)) selected_leaf();
    leaf #(.selected_t(alias_t)) aliased_leaf();
    leaf #(.selected_t(chained_t)) chained_leaf();
    wrapper #(.T(chained_t)) wrapper();
    passthrough #(.T(local_t)) local_leaf();
endmodule
