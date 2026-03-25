// Test: non-typedef struct/union/enum highlighting

// Anonymous struct variable
struct {
    logic [7:0] data;
    logic       valid;
} my_var;

// Nested anonymous struct
struct {
    struct {
        logic [7:0] inner_data;
    } inner;
    logic [3:0] count;
} nested_var;

// Packed struct variable
struct packed {
    logic [15:0] addr;
    logic [15:0] data;
} bus;

// Union variable
union {
    logic [31:0] word;
    logic [7:0]  bytes [4];
} my_union;

// Enum variable
enum logic [1:0] {
    RED   = 2'b00,
    GREEN = 2'b01,
    BLUE  = 2'b10
} color;
