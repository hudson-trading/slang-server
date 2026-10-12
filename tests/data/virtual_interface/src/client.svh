// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

class client;
    local virtual bus_if vif;
    extern function void set_vif(virtual bus_if bus);

    function int read();
        return vif.value;
    endfunction
endclass

function void client::set_vif(virtual bus_if bus);
    vif = bus;
endfunction
