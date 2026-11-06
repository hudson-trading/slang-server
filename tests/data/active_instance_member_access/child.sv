// SPDX-License-Identifier: MIT

module child #(parameter type T = logic) (
    input T item,
    output logic valid
);
    assign valid = item.valid;
endmodule
