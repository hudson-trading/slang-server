module leaf(input logic data, output logic result);
    assign result = data;
endmodule

module open_leaf(input logic open_data);
    logic open_copy;
    assign open_copy = open_data;
endmodule

module explicit_leaf(.port_a(internal_a));
    input logic internal_a;
    logic explicit_copy;
    assign explicit_copy = internal_a;
endmodule

module top(input logic clk, input logic rst, input logic sel);
    typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
    state_t state, next_state;
    logic a, b, q, y, value;
    logic [1:0] bits;

    // Continuous assign
    assign a = sel;

    // Combinational process
    always_comb b = a;

    // Same bits assigned in both branches: only the first assignment is a driver
    always_ff @(posedge clk) begin
        if (rst) q <= 1'b0;
        else q <= b;
    end

    // FSM: only the first assignment to next_state is a driver
    always_comb begin
        case (state)
            IDLE: next_state = RUN;
            RUN: next_state = DONE;
            default: next_state = IDLE;
        endcase
    end

    always_ff @(posedge clk) state <= next_state;

    // Different bits from a process and a continuous assign
    always_comb bits[0] = a;
    assign bits[1] = b;

    // Output port: the connection and the driver inside the module
    leaf u(.data(q), .result(y));

    // Unconnected input port and non-ANSI explicit port
    open_leaf o(.open_data());
    explicit_leaf e(.port_a(value));
    assign value = y;
endmodule
