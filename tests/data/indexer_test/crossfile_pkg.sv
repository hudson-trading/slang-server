// Package for cross-file reference testing
package crossfile_pkg;
    typedef struct packed {
        logic [7:0] addr;
        logic [31:0] data;
    } transaction_t;

    parameter int FIFO_DEPTH = 16;
    parameter int DATA_WIDTH = 32;

    function automatic int calculate_size(int depth);
        return depth * DATA_WIDTH;
    endfunction
endpackage
