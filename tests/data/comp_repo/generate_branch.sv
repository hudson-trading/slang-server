// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

module top;
    sub #(.condition ('1)) the_sub();
endmodule

module sub #(
    parameter bit condition
);
    if (condition) begin: gen_yes
        sub_sub yes_sub_sub();
    end else begin: gen_no
        logic some_logic;
        sub_sub no_sub_sub();
    end
endmodule

module sub_sub;
endmodule
