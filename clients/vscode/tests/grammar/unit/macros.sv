// SYNTAX TEST "source.systemverilog" "macro call highlighting"

// Simple macro call
`MY_MACRO(a, b)
// <-------- entity.name.function.macro.systemverilog

// Macro call with nested parens
`MY_MACRO(foo(bar), baz)
// <-------- entity.name.function.macro.systemverilog

// Macro call with deeply nested parens
`MY_MACRO(foo(bar(baz)), qux)
// <-------- entity.name.function.macro.systemverilog

// Macro call with string argument
`MY_MACRO("hello world")
// <-------- entity.name.function.macro.systemverilog
//         ^^^^^^^^^^^ string.quoted.double.systemverilog

// Macro call with string and nested parens
`MY_MACRO(foo("test"), bar(baz))
// <-------- entity.name.function.macro.systemverilog
//            ^^^^^^ string.quoted.double.systemverilog

// Macro call with system task/function argument
`MY_MACRO($bits(my_type))
// <-------- entity.name.function.macro.systemverilog
//        ^^^^^ entity.name.function.systemverilog

// Code after macro should highlight normally
module test;
// <------ keyword.declaration.systemverilog
endmodule
// <-------- keyword.control.systemverilog
