// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

interface bus_if #(parameter int INITIAL = 0);
    defs_pkg::value_t value = INITIAL;
    modport monitor(input value);
endinterface
