// SPDX-License-Identifier: MIT

module top;
    typedef struct packed {
        logic valid;
    } item_t;

    child #(.T(item_t)) selected(.item('{valid: 1'b1}), .valid());
endmodule
