// Test: DPI import/export highlighting

// Plain import
import "DPI-C" function int my_func(input int x);

// Import with pure modifier
import "DPI-C" pure function real sqrt(real x);

// Import with context modifier
import "DPI-C" context function void ctx_call();

// Legacy "DPI" spec string
import "DPI" function int legacy_func();

// Import with task instead of function
import "DPI-C" task my_imported_task(input int x);

// Import with c_identifier alias
import "DPI-C" my_c_name = function int aliased_func(int x);

// Import with pure modifier and alias
import "DPI-C" pure my_pure_alias = function int pure_aliased(int x);

// Plain export function
export "DPI-C" function my_export_func;

// Plain export task
export "DPI-C" task my_export_task;

// Export with c_identifier alias
export "DPI-C" = my_c_alias function my_export_aliased;
