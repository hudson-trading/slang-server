// SYNTAX TEST "source.systemverilog" "DPI import/export highlighting"

import "DPI-C" function int my_func(input int x);
// <----- keyword.control.systemverilog
//     ^^^^^^^ string.quoted.double.systemverilog
//             ^^^^^^^^ keyword.control.systemverilog

import "DPI-C" pure function real sqrt(real x);
// <----- keyword.control.systemverilog
//     ^^^^^^^ string.quoted.double.systemverilog
//             ^^^^ keyword.control.systemverilog

import "DPI-C" context function void ctx_call();
// <----- keyword.control.systemverilog
//             ^^^^^^^ keyword.control.systemverilog

import "DPI" function int legacy_func();
// <----- keyword.control.systemverilog
//     ^^^^^ string.quoted.double.systemverilog

import "DPI-C" my_c_name = function int aliased_func(int x);
// <----- keyword.control.systemverilog
//     ^^^^^^^ string.quoted.double.systemverilog
//             ^^^^^^^^^ entity.name.function.systemverilog
//                       ^ keyword.operator.assignment.systemverilog

import "DPI-C" pure my_pure_alias = function int pure_aliased(int x);
// <----- keyword.control.systemverilog
//             ^^^^ keyword.control.systemverilog
//                  ^^^^^^^^^^^^^ entity.name.function.systemverilog
//                                ^ keyword.operator.assignment.systemverilog

export "DPI-C" function my_export_func;
// <----- keyword.control.systemverilog
//     ^^^^^^^ string.quoted.double.systemverilog

export "DPI-C" task my_export_task;
// <----- keyword.control.systemverilog
//     ^^^^^^^ string.quoted.double.systemverilog

export "DPI-C" = my_c_alias function my_export_aliased;
// <----- keyword.control.systemverilog
//             ^ keyword.operator.assignment.systemverilog
//               ^^^^^^^^^^ entity.name.function.systemverilog
