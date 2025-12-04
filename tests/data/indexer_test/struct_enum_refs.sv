// Test file for struct and enum member references

typedef struct packed {
    logic [7:0] addr;
    logic [31:0] data;
    logic valid;
    logic ready;
} transaction_s;

typedef struct packed {
    transaction_s request;
    transaction_s response;
    logic [3:0] id;
} bus_packet_s;

typedef enum logic [1:0] {
    IDLE = 2'b00,
    ACTIVE = 2'b01,
    WAIT = 2'b10,
    ERROR = 2'b11
} state_e;

typedef enum logic [2:0] {
    CMD_READ = 3'b000,
    CMD_WRITE = 3'b001,
    CMD_ERASE = 3'b010,
    CMD_FLUSH = 3'b011
} command_e;

module struct_enum_test(
    input logic clk,
    input logic rst
);
    transaction_s tx1, tx2;
    bus_packet_s packet;
    state_e current_state, next_state;
    command_e cmd;

    // Struct member accesses
    always_comb begin
        tx1.addr = 8'h00;
        tx1.data = 32'h00000000;
        tx1.valid = 1'b0;
        tx1.ready = 1'b1;
    end

    // Nested struct member accesses
    always_ff @(posedge clk) begin
        if (rst) begin
            packet.request.addr <= 8'h00;
            packet.request.data <= 32'h00000000;
            packet.response.valid <= 1'b0;
            packet.id <= 4'h0;
        end else begin
            packet.request.addr <= tx1.addr;
            packet.request.data <= tx1.data;
            packet.response.ready <= tx2.ready;
        end
    end

    // Enum member usage
    always_ff @(posedge clk) begin
        if (rst) begin
            current_state <= IDLE;
            cmd <= CMD_READ;
        end else begin
            case (current_state)
                IDLE: begin
                    current_state <= ACTIVE;
                    cmd <= CMD_WRITE;
                end
                ACTIVE: begin
                    if (tx1.valid)
                        current_state <= WAIT;
                    else
                        current_state <= ERROR;
                end
                WAIT: begin
                    current_state <= IDLE;
                    cmd <= CMD_FLUSH;
                end
                ERROR: begin
                    current_state <= IDLE;
                    cmd <= CMD_ERASE;
                end
            endcase
        end
    end

    // Reading from struct members
    assign tx2.addr = packet.request.addr;
    assign tx2.data = packet.response.data;

endmodule
