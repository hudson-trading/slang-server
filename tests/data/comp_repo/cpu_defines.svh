`ifndef CPU_DEFINES_SVH
`define CPU_DEFINES_SVH

// CPU State enumeration
typedef enum logic [2:0] {
    CPU_RESET   = 3'b000,
    CPU_FETCH   = 3'b001,
    CPU_DECODE  = 3'b010,
    CPU_EXECUTE = 3'b011,
    CPU_HALT    = 3'b100
} cpu_state_t;

// ALU Operation enumeration
typedef enum logic [3:0] {
    ALU_ADD = 4'b0000,
    ALU_SUB = 4'b0001,
    ALU_AND = 4'b0010,
    ALU_OR  = 4'b0011,
    ALU_XOR = 4'b0100,
    ALU_SLL = 4'b0101,
    ALU_SRL = 4'b0110,
    ALU_SRA = 4'b0111
} alu_op_t;

// Instruction opcodes
`define ADD_OPCODE  6'b000000
`define SUB_OPCODE  6'b000001
`define AND_OPCODE  6'b000010
`define OR_OPCODE   6'b000011
`define XOR_OPCODE  6'b000100
`define LOAD_OPCODE 6'b100000
`define STORE_OPCODE 6'b100001
`define HALT_OPCODE 6'b111111

// Common parameters
`define DEFAULT_DATA_WIDTH 32
`define DEFAULT_ADDR_WIDTH 32
`define REGISTER_COUNT     32

// Helper macros
`define ZERO_EXTEND(width, value) {{(width-$bits(value)){1'b0}}, value}
`define SIGN_EXTEND(width, value) {{(width-$bits(value)){value[$bits(value)-1]}}, value}

`endif // CPU_DEFINES_SVH
