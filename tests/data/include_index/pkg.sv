// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

`include "entry.svh"
package pkg;
    typedef logic [`ENTRY_WIDTH-1:0] value_t;
    class item;
        `include "members.svh"
    endclass
endpackage
