// SYNTAX TEST "source.systemverilog" "non-typedef struct/union/enum highlighting"

// Anonymous struct variable
struct {
// <------ keyword.control.systemverilog
    logic [7:0] data;
//  ^^^^^ storage.type.vector.systemverilog
//              ^^^^ variable.other.identifier.systemverilog
} my_var;

// Nested anonymous struct
struct {
// <------ keyword.control.systemverilog
    struct {
//  ^^^^^^ keyword.control.systemverilog
        logic [7:0] inner_data;
//      ^^^^^ storage.type.vector.systemverilog
    } inner;
} nested_var;

// Packed struct variable
struct packed {
// <------ keyword.control.systemverilog
//     ^^^^^^ storage.modifier.systemverilog
} bus;

// Union variable
union {
// <----- keyword.control.systemverilog
    logic [31:0] word;
//  ^^^^^ storage.type.vector.systemverilog
} my_union;

// Enum variable
enum logic [1:0] {
// <---- keyword.control.systemverilog
    RED   = 2'b00,
    GREEN = 2'b01,
    BLUE  = 2'b10
} color;
