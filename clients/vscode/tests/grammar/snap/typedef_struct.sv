// Test: typedef struct/union/enum highlighting

// Basic typedef struct
typedef struct {
    logic [7:0] data;
    logic       valid;
} basic_t;

// Nested typedef struct (issue #238)
typedef struct {
    struct {
        logic [7:0] data;
        logic       valid;
    } inner;
    logic [3:0] count;
} nested_t;

// Deeply nested struct
typedef struct {
    struct {
        struct {
            logic [7:0] data;
        } level2;
    } level1;
} deep_nested_t;

// Typedef union
typedef union {
    logic [31:0] word;
    logic [7:0]  bytes [4];
} word_union_t;

// Typedef enum
typedef enum logic [1:0] {
    IDLE  = 2'b00,
    RUN   = 2'b01,
    STOP  = 2'b10,
    ERROR = 2'b11
} state_t;

// Packed struct
typedef struct packed {
    logic [7:0] addr;
    logic [7:0] data;
} packed_t;

// Signed packed struct
typedef struct packed signed {
    logic [7:0] value;
} signed_packed_t;
