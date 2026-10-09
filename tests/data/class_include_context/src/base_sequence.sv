// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

class base_sequence;
    `include "fields.svh"
    function payload_t read();
        return payload ^ `PAYLOAD_MASK;
    endfunction
endclass
