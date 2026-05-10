// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "SystemTaskDocs.h"

#include <array>

namespace server {

using slang::parsing::KnownSystemName;
using KSN = KnownSystemName;

namespace {

constexpr std::size_t kNumKsn = slang::parsing::KnownSystemName_traits::values.size();

// `KnownSystemName` covers two distinct categories of names:
//   - $-prefixed system tasks/functions ($display, $bits, $fopen, ...) —
//     reachable from `getDefinitionInfoAt` today via the rawText[0]=='$' guard
//     and `Compilation::getSystemSubroutine`.
//   - Built-in *method* names on queues, strings, enums, dynamic/associative
//     arrays (pop_back, len, name, find, ...) — invoked as `q.pop_back()`,
//     not as `$pop_back`. The current resolver path doesn't reach these, so
//     their entries here are forward-compatible: when method-call hover
//     resolution lands, the docs are already populated. Keeping them in the
//     same table is what lets the static_assert below cover *every* enum value.
constexpr std::array<SystemTaskDoc, kNumKsn> buildDocs() {
    std::array<SystemTaskDoc, kNumKsn> d{};

    // ---- §20.2 Simulation control ----
    d[size_t(KSN::Finish)] = {
        "task $finish(int finish_number = 1)",
        "Halts the simulator and exits. The optional `finish_number` controls the "
        "diagnostic message printed (0: nothing, 1: simulation time and location, "
        "2: also resource usage).",
        "20.2"};
    d[size_t(KSN::Stop)] = {"task $stop(int finish_number = 1)",
                            "Suspends the simulator and returns interactive control to the user. "
                            "The optional `finish_number` has the same meaning as for `$finish`.",
                            "20.2"};
    d[size_t(KSN::Exit)] = {
        "task $exit()",
        "Within a program block, causes the program to terminate. Outside a program "
        "block, has no effect.",
        "20.2"};

    // ---- §20.3 Simulation time ----
    d[size_t(KSN::Time)] = {
        "function time $time()",
        "Returns the current simulation time as a 64-bit integer, scaled to the "
        "timescale of the calling module.",
        "20.3.1"};
    d[size_t(KSN::STime)] = {
        "function int $stime()",
        "Returns the low-order 32 bits of the current simulation time, scaled to "
        "the timescale of the calling module.",
        "20.3.1"};
    d[size_t(KSN::RealTime)] = {
        "function realtime $realtime()",
        "Returns the current simulation time as a real number, scaled to the "
        "timescale of the calling module.",
        "20.3.1"};

    // ---- §20.4 Timescale ----
    d[size_t(KSN::PrintTimeScale)] = {
        "task $printtimescale([hierarchical_identifier])",
        "Displays the time unit and precision for the given module, or the calling "
        "module if no argument is given.",
        "20.4.1"};
    d[size_t(KSN::TimeFormat)] = {
        "task $timeformat(int units_number = -9, int precision_number = 0, "
        "string suffix_string = \"\", int minimum_field_width = 20)",
        "Sets the format used by `%t` in display tasks for the entire simulation. "
        "`units_number` is the time unit (0 = 1s, -3 = 1ms, -6 = 1us, -9 = 1ns, etc.).",
        "20.4.2"};

    // ---- §20.5 Conversion ----
    d[size_t(KSN::Signed)] = {"function signed_value $signed(value)",
                              "Returns the value with the same bit pattern but signed type.",
                              "20.5"};
    d[size_t(KSN::Unsigned)] = {"function unsigned_value $unsigned(value)",
                                "Returns the value with the same bit pattern but unsigned type.",
                                "20.5"};
    d[size_t(KSN::Rtoi)] = {"function integer $rtoi(real_value)",
                            "Converts a real to an integer by truncating the fractional part.",
                            "20.5"};
    d[size_t(KSN::Itor)] = {"function real $itor(integer_value)", "Converts an integer to a real.",
                            "20.5"};
    d[size_t(KSN::RealToBits)] = {"function bit [63:0] $realtobits(real_value)",
                                  "Converts a real to its 64-bit IEEE 754 bit representation.",
                                  "20.5"};
    d[size_t(KSN::BitsToReal)] = {"function real $bitstoreal(bit [63:0] bit_value)",
                                  "Converts a 64-bit IEEE 754 bit representation to a real.",
                                  "20.5"};
    d[size_t(KSN::ShortrealToBits)] = {
        "function bit [31:0] $shortrealtobits(shortreal_value)",
        "Converts a shortreal to its 32-bit IEEE 754 bit representation.", "20.5"};
    d[size_t(KSN::BitsToShortreal)] = {
        "function shortreal $bitstoshortreal(bit [31:0] bit_value)",
        "Converts a 32-bit IEEE 754 bit representation to a shortreal.", "20.5"};

    // ---- §20.6 Data query ----
    d[size_t(KSN::Bits)] = {
        "function int $bits(type_or_expression)",
        "Returns the number of bits required to hold the value of the given type "
        "or expression.",
        "20.6.2"};
    d[size_t(KSN::Typename)] = {"function string $typename(type_or_expression)",
                                "Returns a string describing the resolved type of the operand.",
                                "20.6.1"};
    d[size_t(KSN::IsUnbounded)] = {
        "function bit $isunbounded(parameter_identifier)",
        "Returns 1 if the parameter has the unbounded literal `$` as its value, "
        "0 otherwise.",
        "20.6.3"};

    // ---- §20.7 Array query ----
    d[size_t(KSN::Dimensions)] = {
        "function int $dimensions(array)",
        "Returns the total number of dimensions of the array (packed + unpacked). "
        "Returns 0 for non-array types and 1 for strings.",
        "20.7"};
    d[size_t(KSN::UnpackedDimensions)] = {"function int $unpacked_dimensions(array)",
                                          "Returns the number of unpacked dimensions of the array.",
                                          "20.7"};
    d[size_t(KSN::Left)] = {"function int $left(array, int dimension = 1)",
                            "Returns the left bound of the given dimension of the array.", "20.7"};
    d[size_t(KSN::Right)] = {"function int $right(array, int dimension = 1)",
                             "Returns the right bound of the given dimension of the array.",
                             "20.7"};
    d[size_t(KSN::Low)] = {"function int $low(array, int dimension = 1)",
                           "Returns the lesser of `$left` and `$right` of the given dimension.",
                           "20.7"};
    d[size_t(KSN::High)] = {"function int $high(array, int dimension = 1)",
                            "Returns the greater of `$left` and `$right` of the given dimension.",
                            "20.7"};
    d[size_t(KSN::Increment)] = {"function int $increment(array, int dimension = 1)",
                                 "Returns 1 if `$left >= $right`, -1 otherwise.", "20.7"};
    d[size_t(KSN::Size)] = {"function int $size(array, int dimension = 1)",
                            "Returns the number of elements in the given dimension. Equal to "
                            "`$high - $low + 1`.",
                            "20.7"};

    // ---- §20.8 Math ----
    d[size_t(KSN::Clog2)] = {
        "function int $clog2(integer_value)",
        "Returns the ceiling of the base-2 logarithm of the argument. Commonly used "
        "to compute the address width needed to index N elements: `$clog2(N)`.",
        "20.8.1"};
    d[size_t(KSN::CountOnes)] = {
        "function int $countones(bit_vector)",
        "Returns the number of `1` bits in the operand. Bits with `x` or `z` "
        "values are not counted.",
        "20.9"};
    d[size_t(KSN::CountBits)] = {
        "function int $countbits(bit_vector, control_bit, ...)",
        "Returns the number of bits that match any of the given control values "
        "(`0`, `1`, `x`, or `z`).",
        "20.9"};
    d[size_t(KSN::OneHot)] = {"function bit $onehot(bit_vector)",
                              "Returns 1 if exactly one bit of the operand is set, 0 otherwise.",
                              "20.9"};
    d[size_t(KSN::OneHot0)] = {"function bit $onehot0(bit_vector)",
                               "Returns 1 if at most one bit of the operand is set, 0 otherwise.",
                               "20.9"};
    d[size_t(KSN::IsUnknown)] = {"function bit $isunknown(bit_vector)",
                                 "Returns 1 if any bit of the operand is `x` or `z`, 0 otherwise.",
                                 "20.9"};
    d[size_t(KSN::Ln)] = {"function real $ln(real x)", "Natural logarithm of `x`.", "20.8.2"};
    d[size_t(KSN::Log10)] = {"function real $log10(real x)", "Base-10 logarithm of `x`.", "20.8.2"};
    d[size_t(KSN::Exp)] = {"function real $exp(real x)", "Returns e^x.", "20.8.2"};
    d[size_t(KSN::Sqrt)] = {"function real $sqrt(real x)", "Square root of `x`.", "20.8.2"};
    d[size_t(KSN::Pow)] = {"function real $pow(real x, real y)", "Returns `x^y`.", "20.8.2"};
    d[size_t(KSN::Floor)] = {"function real $floor(real x)",
                             "Largest integer not greater than `x`, returned as a real.", "20.8.2"};
    d[size_t(KSN::Ceil)] = {"function real $ceil(real x)",
                            "Smallest integer not less than `x`, returned as a real.", "20.8.2"};
    d[size_t(KSN::Sin)] = {"function real $sin(real x)", "Sine of `x` (radians).", "20.8.2"};
    d[size_t(KSN::Cos)] = {"function real $cos(real x)", "Cosine of `x` (radians).", "20.8.2"};
    d[size_t(KSN::Tan)] = {"function real $tan(real x)", "Tangent of `x` (radians).", "20.8.2"};
    d[size_t(KSN::Asin)] = {"function real $asin(real x)", "Inverse sine of `x` (radians).",
                            "20.8.2"};
    d[size_t(KSN::Acos)] = {"function real $acos(real x)", "Inverse cosine of `x` (radians).",
                            "20.8.2"};
    d[size_t(KSN::Atan)] = {"function real $atan(real x)", "Inverse tangent of `x` (radians).",
                            "20.8.2"};
    d[size_t(KSN::Atan2)] = {
        "function real $atan2(real y, real x)",
        "Inverse tangent of `y/x` (radians), with quadrant determined by signs.", "20.8.2"};
    d[size_t(KSN::Hypot)] = {"function real $hypot(real x, real y)", "Returns `sqrt(x*x + y*y)`.",
                             "20.8.2"};

    // ---- §20.10 Severity ----
    d[size_t(KSN::Fatal)] = {"task $fatal(int finish_number = 1, string format = \"\", ...)",
                             "Reports a fatal severity message and terminates the simulator. "
                             "`finish_number` is forwarded to `$finish`.",
                             "20.10"};
    d[size_t(KSN::Error)] = {"task $error(string format = \"\", ...)",
                             "Reports an error severity message. Simulation continues.", "20.10"};
    d[size_t(KSN::Warning)] = {"task $warning(string format = \"\", ...)",
                               "Reports a warning severity message. Simulation continues.",
                               "20.10"};
    d[size_t(KSN::Info)] = {"task $info(string format = \"\", ...)",
                            "Reports an informational severity message. Simulation continues.",
                            "20.10"};

    // ---- §18.13 Random ----
    d[size_t(KSN::Random)] = {
        "function integer $random[(inout integer seed)]",
        "Returns a 32-bit signed pseudo-random integer. The seed argument is "
        "optional; when supplied it both seeds the generator and is updated in place.",
        "18.13.2"};
    d[size_t(KSN::URandom)] = {
        "function bit [31:0] $urandom[(int seed)]",
        "Returns an unsigned 32-bit pseudo-random integer. The optional seed argument "
        "reseeds the thread's generator; with no argument, the current thread state is used.",
        "18.13.3"};
    d[size_t(KSN::URandomRange)] = {
        "function bit [31:0] $urandom_range(bit [31:0] maxval[, bit [31:0] minval])",
        "Returns a uniformly-distributed unsigned 32-bit value in the inclusive "
        "range `[minval, maxval]`. `minval` defaults to 0 when omitted.",
        "18.13.3"};

    // ---- §21.2 Display tasks ----
    d[size_t(KSN::Display)] = {
        "task $display(string format = \"\", ...)",
        "Writes the formatted arguments to stdout, followed by a newline. The "
        "default base for unformatted values is decimal. Format specifiers include "
        "`%d`, `%h`, `%b`, `%s`, `%t`, `%m`, etc.",
        "21.2.1"};
    d[size_t(KSN::DisplayB)] = {
        "task $displayb(string format = \"\", ...)",
        "Like `$display`, but the default base for unformatted values is binary.", "21.2.1"};
    d[size_t(KSN::DisplayO)] = {"task $displayo(string format = \"\", ...)",
                                "Like `$display`, but the default base is octal.", "21.2.1"};
    d[size_t(KSN::DisplayH)] = {"task $displayh(string format = \"\", ...)",
                                "Like `$display`, but the default base is hex.", "21.2.1"};
    d[size_t(KSN::Write)] = {"task $write(string format = \"\", ...)",
                             "Like `$display`, but does not append a newline.", "21.2.1"};
    d[size_t(KSN::WriteB)] = {"task $writeb(string format = \"\", ...)",
                              "Like `$write`, with binary as the default base.", "21.2.1"};
    d[size_t(KSN::WriteO)] = {"task $writeo(string format = \"\", ...)",
                              "Like `$write`, with octal as the default base.", "21.2.1"};
    d[size_t(KSN::WriteH)] = {"task $writeh(string format = \"\", ...)",
                              "Like `$write`, with hex as the default base.", "21.2.1"};
    d[size_t(KSN::Strobe)] = {
        "task $strobe(string format = \"\", ...)",
        "Like `$display`, but the values are sampled at the end of the current "
        "time step (after all assignments resolve).",
        "21.2.3"};
    d[size_t(KSN::Monitor)] = {
        "task $monitor(string format = \"\", ...)",
        "Registers a monitor that prints whenever any of the argument expressions "
        "change. Only one `$monitor` is active at a time.",
        "21.2.4"};
    d[size_t(KSN::MonitorOn)] = {"task $monitoron()",
                                 "Re-enables a previously suspended `$monitor`.", "21.2.4"};
    d[size_t(KSN::MonitorOff)] = {"task $monitoroff()",
                                  "Suspends the active `$monitor` without unregistering it.",
                                  "21.2.4"};

    // ---- §21.3 File I/O ----
    d[size_t(KSN::FOpen)] = {
        "function int $fopen(string filename[, string type])",
        "Opens a file and returns a descriptor. With one argument, returns a "
        "multichannel descriptor (bits 0-30 select up to 31 logfiles). With two "
        "arguments, `type` is a fopen-style mode (`r`, `w`, `a`, `r+`, etc.) and a "
        "regular file descriptor is returned. Returns 0 on failure.",
        "21.3.1"};
    d[size_t(KSN::FClose)] = {"task $fclose(int fd)", "Closes a file opened with `$fopen`.",
                              "21.3.1"};
    d[size_t(KSN::FDisplay)] = {"task $fdisplay(int fd, string format = \"\", ...)",
                                "Like `$display`, but writes to the file descriptor `fd`.",
                                "21.3.2"};
    d[size_t(KSN::FWrite)] = {"task $fwrite(int fd, string format = \"\", ...)",
                              "Like `$write`, but writes to the file descriptor `fd`.", "21.3.2"};
    d[size_t(KSN::FFlush)] = {
        "task $fflush[(int fd)]",
        "Flushes buffered output for the given file descriptor, or all open files "
        "if `fd` is omitted.",
        "21.3.5"};
    d[size_t(KSN::FGets)] = {
        "function int $fgets(output string str, int fd)",
        "Reads a line (up to newline or EOF) from `fd` into `str`. Returns the "
        "number of characters read, or 0 on EOF.",
        "21.3.4"};
    d[size_t(KSN::FScanf)] = {
        "function int $fscanf(int fd, string format, ...)",
        "Reads from `fd` according to `format` and assigns to the remaining output "
        "arguments. Returns the number of items successfully scanned, or `EOF`.",
        "21.3.4"};
    d[size_t(KSN::SScanf)] = {
        "function int $sscanf(string str, string format, ...)",
        "Scans `str` according to `format` and assigns to the remaining output "
        "arguments. Returns the number of items successfully scanned.",
        "21.3.4"};
    d[size_t(KSN::FEof)] = {"function int $feof(int fd)",
                            "Returns non-zero if the end-of-file indicator is set for `fd`.",
                            "21.3.4"};

    // ---- §21.4 Memory load ----
    d[size_t(KSN::ReadMemB)] = {
        "task $readmemb(string filename, ref memory[, int start_addr[, int finish_addr]])",
        "Loads binary values from `filename` into `memory`. The file format is "
        "whitespace-separated binary numbers, with optional `@<address>` markers. "
        "Address bounds are optional; when omitted, slang fills from the array's low "
        "to high index.",
        "21.4"};
    d[size_t(KSN::ReadMemH)] = {
        "task $readmemh(string filename, ref memory[, int start_addr[, int finish_addr]])",
        "Loads hex values from `filename` into `memory`. The file format is "
        "whitespace-separated hex numbers, with optional `@<address>` markers. "
        "Address bounds are optional; when omitted, slang fills from the array's low "
        "to high index.",
        "21.4"};
    d[size_t(KSN::WriteMemB)] = {
        "task $writememb(string filename, ref memory[, int start_addr[, int finish_addr]])",
        "Writes the contents of `memory` to `filename` as binary values. Address "
        "bounds are optional and default to the array's full range.",
        "21.4"};
    d[size_t(KSN::WriteMemH)] = {
        "task $writememh(string filename, ref memory[, int start_addr[, int finish_addr]])",
        "Writes the contents of `memory` to `filename` as hex values. Address bounds "
        "are optional and default to the array's full range.",
        "21.4"};

    // ---- §21.3.3 String formatting ----
    d[size_t(KSN::SFormat)] = {
        "task $sformat(output string str, string format, ...)",
        "Formats arguments into `str` using `format` and the same specifiers as "
        "`$display`.",
        "21.3.3"};
    d[size_t(KSN::SFormatF)] = {"function string $sformatf(string format, ...)",
                                "Returns the formatted string. Functional version of `$sformat`.",
                                "21.3.3"};

    // ---- §20.12 Assertion control ----
    d[size_t(KSN::AssertOn)] = {
        "task $asserton[(int levels[, scope_or_assertion, ...])]",
        "Re-enables previously disabled assertions in the named scopes (or all "
        "scopes if no scope arguments are given). `levels` controls hierarchical "
        "depth (0 means all levels).",
        "20.12"};
    d[size_t(KSN::AssertOff)] = {
        "task $assertoff[(int levels[, scope_or_assertion, ...])]",
        "Disables checking of assertions in the named scopes (or all scopes if no "
        "scope arguments are given). Currently-executing assertions are not affected. "
        "`levels` controls hierarchical depth (0 means all levels).",
        "20.12"};
    d[size_t(KSN::AssertKill)] = {
        "task $assertkill[(int levels[, scope_or_assertion, ...])]",
        "Kills any currently-executing assertions in the named scopes and disables "
        "future evaluation, like `$assertoff`. `levels` controls hierarchical depth "
        "(0 means all levels).",
        "20.12"};
    d[size_t(KSN::AssertControl)] = {
        "task $assertcontrol(int control_type[, int assertion_type[, int directive_type"
        "[, int levels[, scope_or_assertion, ...]]]])",
        "Generic assertion-control task; `control_type` selects the action "
        "(equivalent of `$asserton`/`$assertoff`/`$assertkill`/etc.) and "
        "`assertion_type` / `directive_type` filter which assertions are affected.",
        "20.12"};
    d[size_t(KSN::AssertPassOn)] = {
        "task $assertpasson[(int levels[, scope_or_assertion, ...])]",
        "Re-enables pass action blocks of the named assertions (or all if no scopes).", "20.12"};
    d[size_t(KSN::AssertPassOff)] = {
        "task $assertpassoff[(int levels[, scope_or_assertion, ...])]",
        "Disables pass action blocks of the named assertions; the assertions still "
        "evaluate but their `else` clauses don't fire.",
        "20.12"};
    d[size_t(KSN::AssertFailOn)] = {"task $assertfailon[(int levels[, scope_or_assertion, ...])]",
                                    "Re-enables fail action blocks of the named assertions.",
                                    "20.12"};
    d[size_t(KSN::AssertFailOff)] = {
        "task $assertfailoff[(int levels[, scope_or_assertion, ...])]",
        "Disables fail action blocks; assertions still evaluate but failures don't "
        "trigger their action blocks.",
        "20.12"};
    d[size_t(KSN::AssertNonvacuousOn)] = {
        "task $assertnonvacuouson[(int levels[, scope_or_assertion, ...])]",
        "Re-enables reporting of non-vacuous assertion successes.", "20.12"};
    d[size_t(KSN::AssertVacuousOff)] = {
        "task $assertvacuousoff[(int levels[, scope_or_assertion, ...])]",
        "Disables reporting of vacuous assertion successes.", "20.12"};

    // ---- §20.13 Sampled-value functions (SVA) ----
    d[size_t(KSN::Sampled)] = {"function bit $sampled(expr)",
                               "Returns the sampled value of `expr` — its value at the most recent "
                               "sampling event. Used in concurrent assertions.",
                               "16.9.3"};
    d[size_t(KSN::Past)] = {
        "function bit $past(expr[, num_clocks][, gating_expr][, clocking_event])",
        "Returns the value of `expr` from `num_clocks` (default 1) clocking events "
        "ago. The clocking event defaults to the inferred clock.",
        "16.9.3"};
    d[size_t(KSN::Rose)] = {"function bit $rose(expr[, clocking_event])",
                            "Returns 1 if the LSB of `expr` rose from 0 to 1 since the previous "
                            "clocking event, 0 otherwise.",
                            "16.9.3"};
    d[size_t(KSN::Fell)] = {"function bit $fell(expr[, clocking_event])",
                            "Returns 1 if the LSB of `expr` fell from 1 to 0 since the previous "
                            "clocking event.",
                            "16.9.3"};
    d[size_t(KSN::Stable)] = {
        "function bit $stable(expr[, clocking_event])",
        "Returns 1 if `expr` did not change since the previous clocking event.", "16.9.3"};
    d[size_t(KSN::Changed)] = {"function bit $changed(expr[, clocking_event])",
                               "Returns 1 if `expr` changed since the previous clocking event.",
                               "16.9.3"};

    // ---- §16.16.6 Global-clock sampled-value functions ----
    d[size_t(KSN::PastGclk)] = {"function bit $past_gclk(expr)",
                                "Returns the value of `expr` from the previous global-clock tick.",
                                "16.16.6"};
    d[size_t(KSN::RoseGclk)] = {
        "function bit $rose_gclk(expr)",
        "Returns 1 if the LSB of `expr` rose since the previous global-clock tick.", "16.16.6"};
    d[size_t(KSN::FellGclk)] = {
        "function bit $fell_gclk(expr)",
        "Returns 1 if the LSB of `expr` fell since the previous global-clock tick.", "16.16.6"};
    d[size_t(KSN::StableGclk)] = {
        "function bit $stable_gclk(expr)",
        "Returns 1 if `expr` is stable across the previous global-clock tick.", "16.16.6"};
    d[size_t(KSN::ChangedGclk)] = {"function bit $changed_gclk(expr)",
                                   "Returns 1 if `expr` changed at the previous global-clock tick.",
                                   "16.16.6"};
    d[size_t(KSN::FutureGclk)] = {"function bit $future_gclk(expr)",
                                  "Returns the value of `expr` at the next global-clock tick.",
                                  "16.16.6"};
    d[size_t(KSN::RisingGclk)] = {"function bit $rising_gclk(expr)",
                                  "Returns 1 if `expr` rises at the next global-clock tick.",
                                  "16.16.6"};
    d[size_t(KSN::FallingGclk)] = {"function bit $falling_gclk(expr)",
                                   "Returns 1 if `expr` falls at the next global-clock tick.",
                                   "16.16.6"};
    d[size_t(KSN::SteadyGclk)] = {
        "function bit $steady_gclk(expr)",
        "Returns 1 if `expr` is steady (no change) across the next global-clock tick.", "16.16.6"};
    d[size_t(KSN::ChangingGclk)] = {"function bit $changing_gclk(expr)",
                                    "Returns 1 if `expr` changes at the next global-clock tick.",
                                    "16.16.6"};
    d[size_t(KSN::GlobalClock)] = {
        "function bit $global_clock()",
        "Returns the current value of the global clock used for SVA across "
        "asynchronous reset domains.",
        "16.16"};
    d[size_t(KSN::InferredClock)] = {
        "function event $inferred_clock()",
        "Returns the clocking event inferred for the surrounding property/sequence. "
        "Used in default-clocking-aware property libraries.",
        "16.16.1"};
    d[size_t(KSN::InferredDisable)] = {
        "function bit $inferred_disable()",
        "Returns the disable condition inferred for the surrounding property.", "16.16.1"};
    d[size_t(KSN::Triggered)] = {
        "function bit triggered.triggered",
        "Returns 1 if the named sequence has reached an end point in the current "
        "or a prior simulation step.",
        "16.13"};
    d[size_t(KSN::Matched)] = {
        "function bit sequence.matched",
        "Returns 1 if the named sequence reached an end point in the prior cycle "
        "(used to detect a match across clock boundaries).",
        "16.13"};

    // ---- §20.10 Probabilistic distribution functions ----
    d[size_t(KSN::DistUniform)] = {
        "function int $dist_uniform(inout int seed, int start, int end)",
        "Returns a pseudo-random integer uniformly distributed in `[start, end]`.", "20.15.2"};
    d[size_t(KSN::DistNormal)] = {
        "function int $dist_normal(inout int seed, int mean, int standard_deviation)",
        "Returns a pseudo-random integer with normal distribution.", "20.15.2"};
    d[size_t(KSN::DistExponential)] = {
        "function int $dist_exponential(inout int seed, int mean)",
        "Returns a pseudo-random integer with exponential distribution.", "20.15.2"};
    d[size_t(KSN::DistPoisson)] = {"function int $dist_poisson(inout int seed, int mean)",
                                   "Returns a pseudo-random integer with Poisson distribution.",
                                   "20.15.2"};
    d[size_t(KSN::DistChiSquare)] = {
        "function int $dist_chi_square(inout int seed, int degree_of_freedom)",
        "Returns a pseudo-random integer with chi-square distribution.", "20.15.2"};
    d[size_t(KSN::DistT)] = {"function int $dist_t(inout int seed, int degree_of_freedom)",
                             "Returns a pseudo-random integer with Student's t distribution.",
                             "20.15.2"};
    d[size_t(KSN::DistErlang)] = {
        "function int $dist_erlang(inout int seed, int k_stage, int mean)",
        "Returns a pseudo-random integer with Erlang distribution.", "20.15.2"};

    // ---- §20.8 Math (hyperbolic) ----
    d[size_t(KSN::Sinh)] = {"function real $sinh(real x)", "Hyperbolic sine of `x`.", "20.8.2"};
    d[size_t(KSN::Cosh)] = {"function real $cosh(real x)", "Hyperbolic cosine of `x`.", "20.8.2"};
    d[size_t(KSN::Tanh)] = {"function real $tanh(real x)", "Hyperbolic tangent of `x`.", "20.8.2"};
    d[size_t(KSN::Asinh)] = {"function real $asinh(real x)", "Inverse hyperbolic sine of `x`.",
                             "20.8.2"};
    d[size_t(KSN::Acosh)] = {"function real $acosh(real x)", "Inverse hyperbolic cosine of `x`.",
                             "20.8.2"};
    d[size_t(KSN::Atanh)] = {"function real $atanh(real x)", "Inverse hyperbolic tangent of `x`.",
                             "20.8.2"};

    // ---- §20.14 Coverage control ----
    d[size_t(KSN::CoverageControl)] = {
        "function int $coverage_control(int control_constant, int coverage_type, "
        "int scope_def, string modules_or_instance)",
        "Controls coverage collection for a scope. `control_constant` is one of "
        "`COV_CHECK`, `COV_START`, `COV_STOP` etc. defined in `coverage_control_pkg`.",
        "20.14"};
    d[size_t(KSN::CoverageGetMax)] = {
        "function int $coverage_get_max(int coverage_type, int scope_def, "
        "string modules_or_instance)",
        "Returns the maximum coverage attainable for the given coverage type and scope.", "20.14"};
    d[size_t(KSN::CoverageGet)] = {
        "function int $coverage_get(int coverage_type, int scope_def, "
        "string modules_or_instance)",
        "Returns the current coverage count for the given coverage type and scope.", "20.14"};
    d[size_t(KSN::CoverageMerge)] = {
        "function int $coverage_merge(int coverage_type, string name)",
        "Merges saved coverage data sets matching `name` into the current run.", "20.14"};
    d[size_t(KSN::CoverageSave)] = {
        "function int $coverage_save(int coverage_type, string name)",
        "Saves the current coverage data to the named persistent store.", "20.14"};
    d[size_t(KSN::GetCoverage)] = {
        "function real $get_coverage()",
        "Returns the overall coverage percentage (0.0 .. 100.0) for the design.", "20.14"};
    d[size_t(KSN::SetCoverageDbName)] = {
        "task $set_coverage_db_name(string filename)",
        "Sets the file name to be used for the coverage database written at end of "
        "simulation.",
        "20.14"};
    d[size_t(KSN::LoadCoverageDb)] = {
        "task $load_coverage_db(string filename)",
        "Loads coverage data from the named database file into the current simulation.", "20.14"};

    // ---- §20.6 Plus-args ----
    d[size_t(KSN::TestPlusArgs)] = {
        "function int $test$plusargs(string user_string)",
        "Returns non-zero if `user_string` matches a `+option` provided on the "
        "simulator command line (matched against the prefix).",
        "20.6.1"};
    d[size_t(KSN::ValuePlusArgs)] = {
        "function int $value$plusargs(string user_string, output value)",
        "Searches the simulator command line for a `+option=value` matching "
        "`user_string`'s format spec; on match, parses `value` per the spec and "
        "writes it to the output. Returns 0 if no match.",
        "20.6.2"};

    // ---- §20.5 Conversion ----
    d[size_t(KSN::Cast)] = {
        "function bit $cast(output dest, input source)",
        "Dynamic cast: assigns `source` to `dest` if value is type-compatible. "
        "Returns 1 on success, 0 on failure (compile-time form raises an error).",
        "6.24.3"};

    // ---- §20.11 Static checks ----
    d[size_t(KSN::StaticAssert)] = {
        "$static_assert(condition[, message])",
        "Compile-time assertion checked during elaboration. The optional `message` "
        "is printed when `condition` evaluates to 0.",
        "20.11"};

    // ---- §20.4 Time / scope ----
    d[size_t(KSN::TimeUnit)] = {
        "function real $timeunit[(hierarchical_identifier)]",
        "Returns the timeunit of the calling scope (or named scope), as a real number.", "20.4.1"};
    d[size_t(KSN::TimePrecision)] = {
        "function real $timeprecision[(hierarchical_identifier)]",
        "Returns the time precision of the calling scope (or named scope), as a "
        "real number.",
        "20.4.1"};
    d[size_t(KSN::Scale)] = {"function real $scale(value)",
                             "Returns `value` scaled to the timeunit of the calling scope.",
                             "20.4.1"};
    d[size_t(KSN::Stacktrace)] = {
        "task $stacktrace", "Prints the call stack of the simulator at the point of invocation.",
        ""};
    d[size_t(KSN::CountDrivers)] = {
        "function int $countdrivers(net[, output is_forced[, output number_of_01x_drivers, ...]])",
        "Returns the total number of drivers on the given scalar net. Optional "
        "output args report force state and per-state driver counts.",
        "21.4"};
    d[size_t(KSN::GetPattern)] = {"function $getpattern(memory_element)",
                                  "Returns the binary pattern of the given memory element.", ""};

    // ---- §20.13 SDF ----
    d[size_t(KSN::SdfAnnotate)] = {
        "task $sdf_annotate(string sdf_file[, module_instance][, string config_file]"
        "[, string log_file][, string mtm_spec][, string scale_factors][, string scale_type])",
        "Annotates SDF (Standard Delay Format) timing data into the design.", "16.7.1"};

    // ---- §16.16 PSPRINT (deferred) ----
    d[size_t(KSN::PSPrintF)] = {
        "function string psprintf(string format, ...)",
        "Returns a formatted string. Verilog-AMS extension; equivalent to `$sformatf`.", ""};

    // ---- §18.5 Random object methods ----
    d[size_t(KSN::Randomize)] = {
        "function int randomize([variable_list])",
        "Randomizes the named variables (or all `rand`/`randc` variables) of the "
        "calling object subject to its constraints. Returns 1 on success, 0 on failure.",
        "18.7"};
    d[size_t(KSN::RandMode)] = {
        "function void rand_mode(bit on_off)\n"
        "function int  rand_mode()",
        "Object/field method: enable (1) or disable (0) randomization of `rand` "
        "members; the no-argument form returns the current mode.",
        "18.8"};
    d[size_t(KSN::ConstraintMode)] = {
        "function void constraint_mode(bit on_off)\n"
        "function int  constraint_mode()",
        "Object/constraint method: enable or disable a constraint block; the "
        "no-argument form returns the current mode.",
        "18.9"};

    // ---- §21.2 Display family — base-suffix variants ----
    d[size_t(KSN::StrobeB)] = {"task $strobeb(string format = \"\", ...)",
                               "Like `$strobe`, with binary as the default base.", "21.2.3"};
    d[size_t(KSN::StrobeO)] = {"task $strobeo(string format = \"\", ...)",
                               "Like `$strobe`, with octal as the default base.", "21.2.3"};
    d[size_t(KSN::StrobeH)] = {"task $strobeh(string format = \"\", ...)",
                               "Like `$strobe`, with hex as the default base.", "21.2.3"};
    d[size_t(KSN::MonitorB)] = {"task $monitorb(string format = \"\", ...)",
                                "Like `$monitor`, with binary as the default base.", "21.2.4"};
    d[size_t(KSN::MonitorO)] = {"task $monitoro(string format = \"\", ...)",
                                "Like `$monitor`, with octal as the default base.", "21.2.4"};
    d[size_t(KSN::MonitorH)] = {"task $monitorh(string format = \"\", ...)",
                                "Like `$monitor`, with hex as the default base.", "21.2.4"};
    d[size_t(KSN::FDisplayB)] = {"task $fdisplayb(int fd, string format = \"\", ...)",
                                 "Like `$fdisplay`, with binary as the default base.", "21.3.2"};
    d[size_t(KSN::FDisplayO)] = {"task $fdisplayo(int fd, string format = \"\", ...)",
                                 "Like `$fdisplay`, with octal as the default base.", "21.3.2"};
    d[size_t(KSN::FDisplayH)] = {"task $fdisplayh(int fd, string format = \"\", ...)",
                                 "Like `$fdisplay`, with hex as the default base.", "21.3.2"};
    d[size_t(KSN::FWriteB)] = {"task $fwriteb(int fd, string format = \"\", ...)",
                               "Like `$fwrite`, with binary as the default base.", "21.3.2"};
    d[size_t(KSN::FWriteO)] = {"task $fwriteo(int fd, string format = \"\", ...)",
                               "Like `$fwrite`, with octal as the default base.", "21.3.2"};
    d[size_t(KSN::FWriteH)] = {"task $fwriteh(int fd, string format = \"\", ...)",
                               "Like `$fwrite`, with hex as the default base.", "21.3.2"};
    d[size_t(KSN::FStrobe)] = {
        "task $fstrobe(int fd, string format = \"\", ...)",
        "Like `$strobe`, but writes to file descriptor `fd`. Values are sampled at "
        "end of time step.",
        "21.3.2"};
    d[size_t(KSN::FStrobeB)] = {"task $fstrobeb(int fd, string format = \"\", ...)",
                                "`$fstrobe` with binary default base.", "21.3.2"};
    d[size_t(KSN::FStrobeO)] = {"task $fstrobeo(int fd, string format = \"\", ...)",
                                "`$fstrobe` with octal default base.", "21.3.2"};
    d[size_t(KSN::FStrobeH)] = {"task $fstrobeh(int fd, string format = \"\", ...)",
                                "`$fstrobe` with hex default base.", "21.3.2"};
    d[size_t(KSN::FMonitor)] = {"task $fmonitor(int fd, string format = \"\", ...)",
                                "Like `$monitor`, but writes to file descriptor `fd`.", "21.3.2"};
    d[size_t(KSN::FMonitorB)] = {"task $fmonitorb(int fd, string format = \"\", ...)",
                                 "`$fmonitor` with binary default base.", "21.3.2"};
    d[size_t(KSN::FMonitorO)] = {"task $fmonitoro(int fd, string format = \"\", ...)",
                                 "`$fmonitor` with octal default base.", "21.3.2"};
    d[size_t(KSN::FMonitorH)] = {"task $fmonitorh(int fd, string format = \"\", ...)",
                                 "`$fmonitor` with hex default base.", "21.3.2"};
    d[size_t(KSN::SWrite)] = {
        "task $swrite(output string str, ...)",
        "Like `$sformat`, but accepts space-separated arguments without a format "
        "string (each argument formatted in its default base).",
        "21.3.3"};
    d[size_t(KSN::SWriteB)] = {"task $swriteb(output string str, ...)",
                               "`$swrite` with binary as the default base.", "21.3.3"};
    d[size_t(KSN::SWriteO)] = {"task $swriteo(output string str, ...)",
                               "`$swrite` with octal as the default base.", "21.3.3"};
    d[size_t(KSN::SWriteH)] = {"task $swriteh(output string str, ...)",
                               "`$swrite` with hex as the default base.", "21.3.3"};

    // ---- §21.3 File I/O extras ----
    d[size_t(KSN::FError)] = {"function int $ferror(int fd, output string str)",
                              "Returns the most recent file-I/O error code for `fd` and writes a "
                              "human-readable message to `str`. Returns 0 if no error.",
                              "21.3.5"};
    d[size_t(KSN::FRead)] = {"function int $fread(output mem, int fd[, int start[, int count]])",
                             "Reads bytes from `fd` into `mem`. Returns the number of bytes (or "
                             "elements) read.",
                             "21.3.4"};
    d[size_t(KSN::FGetC)] = {"function int $fgetc(int fd)",
                             "Reads one character from `fd`. Returns the character code, or `EOF`.",
                             "21.3.4"};
    d[size_t(KSN::UngetC)] = {
        "function int $ungetc(int c, int fd)",
        "Pushes character `c` back onto the input buffer of `fd` so that the next "
        "read returns it.",
        "21.3.4"};
    d[size_t(KSN::FTell)] = {
        "function int $ftell(int fd)",
        "Returns the current byte offset within the file for `fd`, or -1 on error.", "21.3.6"};
    d[size_t(KSN::FSeek)] = {
        "function int $fseek(int fd, int offset, int whence)",
        "Sets the file position for `fd`. `whence` is 0 (SEEK_SET), 1 (SEEK_CUR), "
        "or 2 (SEEK_END). Returns 0 on success.",
        "21.3.6"};
    d[size_t(KSN::Rewind)] = {
        "function int $rewind(int fd)",
        "Sets the file position of `fd` back to byte 0. Returns 0 on success.", "21.3.6"};
    d[size_t(KSN::SReadMemB)] = {
        "task $sreadmemb(ref memory, int start_addr, int finish_addr, string str0[, ...])",
        "Loads binary values from one or more strings into `memory` (string-source "
        "form of `$readmemb`).",
        "21.4"};
    d[size_t(KSN::SReadMemH)] = {
        "task $sreadmemh(ref memory, int start_addr, int finish_addr, string str0[, ...])",
        "Loads hex values from one or more strings into `memory` (string-source "
        "form of `$readmemh`).",
        "21.4"};

    // ---- §21.7 Value Change Dump (VCD) ----
    d[size_t(KSN::DumpFile)] = {
        "task $dumpfile(string filename)",
        "Names the VCD output file. Defaults to `verilog.dump` if not called.", "21.7.1.1"};
    d[size_t(KSN::DumpVars)] = {
        "task $dumpvars[(int levels[, scope_or_var, ...])]",
        "Selects variables to dump to VCD. `levels` controls hierarchical depth "
        "(0 = all). With no args, dumps everything in the design.",
        "21.7.1.2"};
    d[size_t(KSN::DumpOn)] = {"task $dumpon", "Resumes VCD dumping after a previous `$dumpoff`.",
                              "21.7.1.3"};
    d[size_t(KSN::DumpOff)] = {"task $dumpoff", "Suspends VCD dumping; resumed by `$dumpon`.",
                               "21.7.1.3"};
    d[size_t(KSN::DumpAll)] = {
        "task $dumpall", "Forces a VCD checkpoint with the current values of all dumped variables.",
        "21.7.1.4"};
    d[size_t(KSN::DumpLimit)] = {
        "task $dumplimit(int filesize)",
        "Limits the VCD file size in bytes. Dumping stops once the limit is reached.", "21.7.1.5"};
    d[size_t(KSN::DumpFlush)] = {"task $dumpflush",
                                 "Flushes the OS file buffer for the VCD file to disk.",
                                 "21.7.1.6"};
    d[size_t(KSN::DumpPorts)] = {
        "task $dumpports[(scope_list, string filename)]",
        "Selects ports to dump to extended VCD (eVCD). Like `$dumpvars` but "
        "restricted to ports.",
        "21.7.2"};
    d[size_t(KSN::DumpPortsOn)] = {"task $dumpportson[(string filename)]",
                                   "Resumes extended-VCD port dumping.", "21.7.2.5"};
    d[size_t(KSN::DumpPortsOff)] = {"task $dumpportsoff[(string filename)]",
                                    "Suspends extended-VCD port dumping.", "21.7.2.5"};
    d[size_t(KSN::DumpPortsAll)] = {"task $dumpportsall[(string filename)]",
                                    "Forces an extended-VCD checkpoint with current port values.",
                                    "21.7.2.5"};
    d[size_t(KSN::DumpPortsLimit)] = {"task $dumpportslimit(int filesize[, string filename])",
                                      "Sets a size limit on the extended-VCD output file.",
                                      "21.7.2.5"};
    d[size_t(KSN::DumpPortsFlush)] = {"task $dumpportsflush[(string filename)]",
                                      "Flushes the OS buffer for the extended-VCD file.",
                                      "21.7.2.5"};

    // ---- §22 Compiler/simulator interactive control ----
    d[size_t(KSN::Input)] = {"task $input(string filename)",
                             "Reads simulator commands from `filename` (interactive batch input).",
                             "22.10"};
    d[size_t(KSN::Key)] = {"task $key[(string filename)]",
                           "Begins logging keyboard input to `filename` (default `key.dat`).",
                           "22.10"};
    d[size_t(KSN::NoKey)] = {"task $nokey", "Disables keyboard logging started by `$key`.",
                             "22.10"};
    d[size_t(KSN::Log)] = {"task $log[(string filename)]",
                           "Begins logging simulator output to `filename` (default `verilog.log`).",
                           "22.10"};
    d[size_t(KSN::NoLog)] = {"task $nolog", "Disables logging started by `$log`.", "22.10"};
    d[size_t(KSN::Reset)] = {
        "task $reset[(int stop_value[, int reset_value[, int diagnostics_value]])]",
        "Resets the simulation back to the initial state.", "22.7"};
    d[size_t(KSN::Save)] = {
        "task $save(string filename)",
        "Saves the current simulation state to `filename` for later `$restart`.", "22.6"};
    d[size_t(KSN::Restart)] = {
        "task $restart(string filename)",
        "Restarts the simulation from a state previously saved with `$save`.", "22.6"};
    d[size_t(KSN::IncSave)] = {
        "task $incsave(string filename)",
        "Saves an incremental simulation state (delta from the most recent `$save`).", "22.6"};
    d[size_t(KSN::ShowVars)] = {
        "task $showvars[(net_or_reg, ...)]",
        "Prints internal information about the named nets/regs (debugger aid).", "22.10"};
    d[size_t(KSN::ShowScopes)] = {
        "task $showscopes[(int n)]",
        "Lists scopes within the current scope, descending up to `n` levels.", "22.10"};
    d[size_t(KSN::Scope)] = {"task $scope(hierarchical_identifier)",
                             "Sets the interactive-debugger current scope.", "22.10"};
    d[size_t(KSN::List)] = {"task $list[(hierarchical_identifier)]",
                            "Lists the source code of the named scope (interactive debugger).",
                            "22.10"};
    d[size_t(KSN::System)] = {"function int $system(string command)",
                              "Executes `command` via the host shell and returns its exit status.",
                              ""};
    d[size_t(KSN::Deposit)] = {
        "task $deposit(variable, value)",
        "Forces `value` into `variable` for one cycle without continuous-assignment "
        "semantics.",
        ""};

    // ---- §20 Stochastic-analysis queue tasks ----
    d[size_t(KSN::QInitialize)] = {
        "task $q_initialize(int q_id, int q_type, int max_length, output int status)",
        "Creates a stochastic queue of the given type and capacity.", "20.16"};
    d[size_t(KSN::QAdd)] = {"task $q_add(int q_id, int job_id, int inform_id, output int status)",
                            "Adds a job to a stochastic queue.", "20.16"};
    d[size_t(KSN::QRemove)] = {
        "task $q_remove(int q_id, int job_id, output int inform_id, output int status)",
        "Removes the head job from a stochastic queue.", "20.16"};
    d[size_t(KSN::QExam)] = {
        "task $q_exam(int q_id, int q_stat_code, output int q_stat_value, output int status)",
        "Reports a statistic about a stochastic queue (length, mean wait, etc.).", "20.16"};
    d[size_t(KSN::QFull)] = {"function int $q_full(int q_id, output int status)",
                             "Returns 1 if the named stochastic queue is full.", "20.16"};

    // ---- §21.5 PLA modeling tasks ----
    d[size_t(KSN::AsyncAndArray)] = {
        "task $async$and$array(memory, input_terms, output_terms)",
        "Asynchronous AND-plane PLA represented as a personality matrix.", "21.5"};
    d[size_t(KSN::SyncAndArray)] = {"task $sync$and$array(memory, input_terms, output_terms)",
                                    "Synchronous AND-plane PLA.", "21.5"};
    d[size_t(KSN::AsyncAndPlane)] = {
        "task $async$and$plane(memory, input_terms, output_terms)",
        "Asynchronous AND-plane PLA with the personality matrix transposed.", "21.5"};
    d[size_t(KSN::SyncAndPlane)] = {"task $sync$and$plane(memory, input_terms, output_terms)",
                                    "Synchronous AND-plane PLA, plane form.", "21.5"};
    d[size_t(KSN::AsyncNandArray)] = {"task $async$nand$array(memory, input_terms, output_terms)",
                                      "Asynchronous NAND-plane PLA, array form.", "21.5"};
    d[size_t(KSN::SyncNandArray)] = {"task $sync$nand$array(memory, input_terms, output_terms)",
                                     "Synchronous NAND-plane PLA, array form.", "21.5"};
    d[size_t(KSN::AsyncNandPlane)] = {"task $async$nand$plane(memory, input_terms, output_terms)",
                                      "Asynchronous NAND-plane PLA, plane form.", "21.5"};
    d[size_t(KSN::SyncNandPlane)] = {"task $sync$nand$plane(memory, input_terms, output_terms)",
                                     "Synchronous NAND-plane PLA, plane form.", "21.5"};
    d[size_t(KSN::AsyncOrArray)] = {"task $async$or$array(memory, input_terms, output_terms)",
                                    "Asynchronous OR-plane PLA, array form.", "21.5"};
    d[size_t(KSN::SyncOrArray)] = {"task $sync$or$array(memory, input_terms, output_terms)",
                                   "Synchronous OR-plane PLA, array form.", "21.5"};
    d[size_t(KSN::AsyncOrPlane)] = {"task $async$or$plane(memory, input_terms, output_terms)",
                                    "Asynchronous OR-plane PLA, plane form.", "21.5"};
    d[size_t(KSN::SyncOrPlane)] = {"task $sync$or$plane(memory, input_terms, output_terms)",
                                   "Synchronous OR-plane PLA, plane form.", "21.5"};
    d[size_t(KSN::AsyncNorArray)] = {"task $async$nor$array(memory, input_terms, output_terms)",
                                     "Asynchronous NOR-plane PLA, array form.", "21.5"};
    d[size_t(KSN::SyncNorArray)] = {"task $sync$nor$array(memory, input_terms, output_terms)",
                                    "Synchronous NOR-plane PLA, array form.", "21.5"};
    d[size_t(KSN::AsyncNorPlane)] = {"task $async$nor$plane(memory, input_terms, output_terms)",
                                     "Asynchronous NOR-plane PLA, plane form.", "21.5"};
    d[size_t(KSN::SyncNorPlane)] = {"task $sync$nor$plane(memory, input_terms, output_terms)",
                                    "Synchronous NOR-plane PLA, plane form.", "21.5"};

    // ---- §7.10–7.12 Built-in array methods ----
    d[size_t(KSN::Reverse)] = {"function void array.reverse()",
                               "Reverses the elements of the array in place.", "7.12.4"};
    d[size_t(KSN::Sort)] = {
        "function void array.sort([with (item.expr)])",
        "Sorts the array in ascending order. Optional `with` clause selects the "
        "sort key.",
        "7.12.4"};
    d[size_t(KSN::Rsort)] = {"function void array.rsort([with (item.expr)])",
                             "Sorts the array in descending order.", "7.12.4"};
    d[size_t(KSN::Shuffle)] = {"function void array.shuffle()",
                               "Randomly permutes the elements of the array.", "7.12.4"};
    d[size_t(KSN::Sum)] = {"function T array.sum([with (item.expr)])",
                           "Returns the sum of the elements (or of the `with`-clause expression).",
                           "7.12.3"};
    d[size_t(KSN::Product)] = {
        "function T array.product([with (item.expr)])",
        "Returns the product of the elements (or of the `with`-clause expression).", "7.12.3"};
    d[size_t(KSN::And)] = {"function T array.and([with (item.expr)])",
                           "Returns the bitwise-AND reduction of the elements.", "7.12.3"};
    d[size_t(KSN::Or)] = {"function T array.or([with (item.expr)])",
                          "Returns the bitwise-OR reduction of the elements.", "7.12.3"};
    d[size_t(KSN::XOr)] = {"function T array.xor([with (item.expr)])",
                           "Returns the bitwise-XOR reduction of the elements.", "7.12.3"};
    d[size_t(KSN::Min)] = {"function T array.min([with (item.expr)])",
                           "Returns the minimum element (or `with`-clause result).", "7.12.3"};
    d[size_t(KSN::Max)] = {"function T array.max([with (item.expr)])",
                           "Returns the maximum element (or `with`-clause result).", "7.12.3"};
    d[size_t(KSN::Unique)] = {
        "function array_type array.unique([with (item.expr)])",
        "Returns a queue of all unique elements (by value or by `with`-clause key).", "7.12.3"};
    d[size_t(KSN::UniqueIndex)] = {
        "function int_queue array.unique_index([with (item.expr)])",
        "Returns a queue of indexes of the first occurrence of each unique value.", "7.12.3"};
    d[size_t(KSN::Find)] = {
        "function array_type array.find with (item.expr)",
        "Returns a queue of all elements satisfying the `with`-clause predicate.", "7.12.3"};
    d[size_t(KSN::FindIndex)] = {
        "function int_queue array.find_index with (item.expr)",
        "Returns a queue of indexes of all elements satisfying the predicate.", "7.12.3"};
    d[size_t(KSN::FindFirst)] = {
        "function array_type array.find_first with (item.expr)",
        "Returns a queue containing the first element satisfying the predicate.", "7.12.3"};
    d[size_t(KSN::FindFirstIndex)] = {
        "function int_queue array.find_first_index with (item.expr)",
        "Returns a queue with the index of the first element satisfying the predicate.", "7.12.3"};
    d[size_t(KSN::FindLast)] = {
        "function array_type array.find_last with (item.expr)",
        "Returns a queue containing the last element satisfying the predicate.", "7.12.3"};
    d[size_t(KSN::FindLastIndex)] = {
        "function int_queue array.find_last_index with (item.expr)",
        "Returns a queue with the index of the last element satisfying the predicate.", "7.12.3"};
    d[size_t(KSN::ArraySize)] = {"function int array.size()",
                                 "Returns the number of elements in the array.", "7.10.2"};

    // ---- §7.10 Associative array methods ----
    d[size_t(KSN::Delete)] = {
        "function void array.delete([index])",
        "Removes the element at the given index, or empties the entire array if "
        "no index is given.",
        "7.9.3"};
    d[size_t(KSN::Exists)] = {"function int array.exists(index)",
                              "Returns 1 if an element with the given index exists in the array.",
                              "7.9.4"};
    d[size_t(KSN::Insert)] = {"function void queue.insert(int index, value)",
                              "Inserts `value` at position `index` in the queue.", "7.10.2"};
    d[size_t(KSN::Index)] = {"function pseudo $",
                             "Pseudo-method `[$]` indexes the last element of a queue (used as an "
                             "expression, not a method call).",
                             "7.10"};
    d[size_t(KSN::Map)] = {"function array_type array.map with (item.expr)",
                           "Returns a queue obtained by applying `item.expr` to each element.",
                           "7.12.3"};

    // ---- §7.10 Associative-array iterators ----
    d[size_t(KSN::Num)] = {"function int array.num()",
                           "Returns the number of entries in the associative array.", "7.9.2"};
    d[size_t(KSN::First)] = {
        "function int array.first(ref index)",
        "Sets `index` to the smallest key in the associative array. Returns 0 if "
        "the array is empty, 1 otherwise.",
        "7.9.5"};
    d[size_t(KSN::Last)] = {
        "function int array.last(ref index)",
        "Sets `index` to the largest key in the associative array. Returns 0 if "
        "empty, 1 otherwise.",
        "7.9.5"};
    d[size_t(KSN::Next)] = {
        "function int array.next(ref index)",
        "Advances `index` to the next-larger key. Returns 0 if there is no next "
        "key, 1 otherwise.",
        "7.9.5"};
    d[size_t(KSN::Prev)] = {
        "function int array.prev(ref index)",
        "Sets `index` to the next-smaller key. Returns 0 if there is no previous "
        "key, 1 otherwise.",
        "7.9.5"};

    // ---- §7.10 Queue methods ----
    d[size_t(KSN::PopFront)] = {"function T queue.pop_front()",
                                "Removes and returns the first element of the queue.", "7.10.2"};
    d[size_t(KSN::PopBack)] = {"function T queue.pop_back()",
                               "Removes and returns the last element of the queue.", "7.10.2"};
    d[size_t(KSN::PushFront)] = {"function void queue.push_front(T value)",
                                 "Prepends `value` to the queue.", "7.10.2"};
    d[size_t(KSN::PushBack)] = {"function void queue.push_back(T value)",
                                "Appends `value` to the queue.", "7.10.2"};

    // ---- §6.22.6 Enum methods ----
    d[size_t(KSN::Name)] = {"function string enum.name()",
                            "Returns the string name of the current enum value (or empty string if "
                            "the value is not a labeled member).",
                            "6.19.5"};

    // ---- §6.16/§7.10 String methods ----
    d[size_t(KSN::Len)] = {"function int string.len()",
                           "Returns the length of the string in bytes.", "6.16.2"};
    d[size_t(KSN::Putc)] = {"function void string.putc(int i, byte c)",
                            "Replaces the byte at index `i` with `c`.", "6.16.2"};
    d[size_t(KSN::Getc)] = {"function byte string.getc(int i)", "Returns the byte at index `i`.",
                            "6.16.2"};
    d[size_t(KSN::Substr)] = {"function string string.substr(int i, int j)",
                              "Returns the substring from byte `i` through byte `j` (inclusive).",
                              "6.16.2"};
    d[size_t(KSN::ToUpper)] = {"function string string.toupper()",
                               "Returns the string with all letters uppercased.", "6.16.2"};
    d[size_t(KSN::ToLower)] = {"function string string.tolower()",
                               "Returns the string with all letters lowercased.", "6.16.2"};
    d[size_t(KSN::Compare)] = {"function int string.compare(string s)",
                               "Lexicographic comparison: returns 0 if equal, < 0 if `this` < `s`, "
                               "> 0 if `this` > `s`.",
                               "6.16.2"};
    d[size_t(KSN::ICompare)] = {"function int string.icompare(string s)",
                                "Case-insensitive variant of `compare`.", "6.16.2"};
    d[size_t(KSN::AToI)] = {"function integer string.atoi()",
                            "Parses the string as a decimal integer.", "6.16.2"};
    d[size_t(KSN::AToHex)] = {"function integer string.atohex()",
                              "Parses the string as a hex integer.", "6.16.2"};
    d[size_t(KSN::AToOct)] = {"function integer string.atooct()",
                              "Parses the string as an octal integer.", "6.16.2"};
    d[size_t(KSN::AToBin)] = {"function integer string.atobin()",
                              "Parses the string as a binary integer.", "6.16.2"};
    d[size_t(KSN::AToReal)] = {"function real string.atoreal()",
                               "Parses the string as a real number.", "6.16.2"};
    d[size_t(KSN::IToA)] = {"function void string.itoa(integer n)",
                            "Sets the string to the decimal representation of `n`.", "6.16.2"};
    d[size_t(KSN::HexToA)] = {"function void string.hextoa(integer n)",
                              "Sets the string to the hex representation of `n`.", "6.16.2"};
    d[size_t(KSN::OctToA)] = {"function void string.octtoa(integer n)",
                              "Sets the string to the octal representation of `n`.", "6.16.2"};
    d[size_t(KSN::BinToA)] = {"function void string.bintoa(integer n)",
                              "Sets the string to the binary representation of `n`.", "6.16.2"};
    d[size_t(KSN::RealToA)] = {"function void string.realtoa(real r)",
                               "Sets the string to the decimal representation of `r`.", "6.16.2"};

    // ---- Misc / counters ----
    d[size_t(KSN::ResetCount)] = {"function int $reset_count()",
                                  "Returns the number of times `$reset` has been called this run.",
                                  "22.7"};
    d[size_t(KSN::ResetValue)] = {
        "function int $reset_value()",
        "Returns the `reset_value` argument passed to the most recent `$reset`.", "22.7"};

    return d;
}

constexpr std::array<SystemTaskDoc, kNumKsn> DOCS = buildDocs();

constexpr std::size_t countDocumented() {
    std::size_t n = 0;
    for (std::size_t i = 1; i < DOCS.size(); ++i) {
        if (!DOCS[i].signature.empty()) {
            ++n;
        }
    }
    return n;
}

// Per-entry coverage guarantee: every slang-known system name (except the
// `Unknown` sentinel at index 0) must have a populated entry. Adding a new
// `KnownSystemName` upstream without a corresponding row here will fail the
// build.
static_assert(countDocumented() == kNumKsn - 1,
              "SystemTaskDocs: every KnownSystemName except `Unknown` must have a "
              "populated entry. Add the missing rows to `buildDocs()`.");

} // namespace

const SystemTaskDoc* getSystemTaskDoc(KnownSystemName name) {
    auto idx = static_cast<size_t>(name);
    if (idx == 0 || idx >= kNumKsn) {
        return nullptr;
    }
    const auto& doc = DOCS[idx];
    if (doc.signature.empty()) {
        return nullptr;
    }
    return &doc;
}

} // namespace server
