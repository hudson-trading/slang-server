// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

// Two modules in one file, where the top (tb) instantiates the cell. The cell has a
// non-defaulted parameter so it is never a valid top on its own.
module inner_cell
  #(parameter int width)
   ();
    leaf dut ();
endmodule

module outer_tb;
    inner_cell #(.width(8)) the_cell ();
endmodule

module leaf;
endmodule
