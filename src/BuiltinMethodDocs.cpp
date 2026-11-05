// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "BuiltinMethodDocs.h"

#include "util/Converters.h"
#include "util/Formatting.h"
#include <fmt/format.h>

#include "slang/ast/Compilation.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/syntax/AllSyntax.h"

namespace server {
using namespace slang;

const BuiltinMethodDoc* getBuiltinMethodDoc(const ast::SubroutineSymbol& method) {
    if (!method.flags.has(ast::MethodFlags::BuiltIn))
        return nullptr;
    auto* scope = method.getParentScope();
    if (!scope)
        return nullptr;
    auto& stdPackage = method.getCompilation().getStdPackage();
    std::string_view owner;
    if (scope == &stdPackage || scope->asSymbol().getParentScope() == &stdPackage)
        owner = scope->asSymbol().name;
    else if (scope->asSymbol().kind != ast::SymbolKind::ClassType)
        return nullptr;

    static constexpr struct {
        std::string_view owner;
        std::string_view name;
        BuiltinMethodDoc doc;
    } docs[] = {
        {"process", "self", {"Returns a handle to the calling process.", "9.7"}},
        {"process", "status", {"Returns the current process state.", "9.7"}},
        {"process", "kill", {"Terminates this process and its descendant subprocesses.", "9.7"}},
        {"process",
         "await",
         {"Waits until this process terminates. A process cannot await itself.", "9.7"}},
        {"process", "suspend", {"Suspends execution of this process.", "9.7"}},
        {"process", "resume", {"Resumes a suspended process.", "9.7"}},
        {"process", "srandom", {"Seeds this process's random number generator.", "18.13.3"}},
        {"process",
         "get_randstate",
         {"Returns this process's random number generator state as a string.", "18.13.4"}},
        {"process",
         "set_randstate",
         {"Restores this process's random number generator state from a previously saved string.",
          "18.13.5"}},
        {"semaphore",
         "new",
         {"Creates a semaphore with the given number of keys; the default is zero.", "15.3.1"}},
        {"semaphore",
         "put",
         {"Returns keys to the semaphore, making them available to waiting processes.", "15.3.2"}},
        {"semaphore",
         "get",
         {"Obtains the requested number of keys, blocking until enough keys are available.",
          "15.3.3"}},
        {"semaphore",
         "try_get",
         {"Tries to obtain keys without blocking. Returns a positive integer on success or 0 if "
          "insufficient keys "
          "are available.",
          "15.3.4"}},
        {"mailbox",
         "new",
         {"Creates a mailbox. A bound of zero gives an unbounded queue; a positive bound limits "
          "its capacity.",
          "15.4.1"}},
        {"mailbox", "num", {"Returns the current number of queued messages.", "15.4.2"}},
        {"mailbox",
         "put",
         {"Appends a message in FIFO order, blocking while a bounded mailbox is full.", "15.4.3"}},
        {"mailbox",
         "try_put",
         {"Appends a message without blocking. Returns a positive integer on success or zero if "
          "the mailbox is full.",
          "15.4.4"}},
        {"mailbox",
         "get",
         {"Removes the oldest message, blocking while the mailbox is empty. A message type "
          "mismatch is a runtime error.",
          "15.4.5"}},
        {"mailbox",
         "try_get",
         {"Removes the oldest message without blocking. Returns a positive integer on success, "
          "zero if empty, or a negative integer for a type mismatch.",
          "15.4.6"}},
        {"mailbox",
         "peek",
         {"Copies the oldest message without removing it, blocking while the mailbox is empty. A "
          "message type mismatch is a runtime error.",
          "15.4.7"}},
        {"mailbox",
         "try_peek",
         {"Copies the oldest message without removing it or blocking. Returns a positive integer "
          "on success, zero if empty, or a negative integer for a type mismatch.",
          "15.4.8"}},
        {"weak_reference",
         "new",
         {"Creates a weak reference to the referent without keeping it strongly reachable.",
          "8.30.2"}},
        {"weak_reference",
         "get",
         {"Returns the referent, or null if the weak reference has been cleared.", "8.30.3"}},
        {"weak_reference",
         "clear",
         {"Clears the weak reference so subsequent get() calls return null.", "8.30.4"}},
        {"weak_reference",
         "get_id",
         {"Returns a stable identifier unique during the object's lifetime, or zero for null.",
          "8.30.5"}},
        {"std",
         "randomize",
         {"Randomizes the supplied scope variables subject to an optional with constraint block. "
          "Returns 1 on success or 0 on failure.",
          "18.12"}},
        {"",
         "randomize",
         {"Randomizes this object's active random variables subject to its enabled constraints and "
          "an optional with block. Returns 1 on success or 0 on failure.",
          "18.6.1"}},
        {"",
         "pre_randomize",
         {"Callback invoked before randomization; override it to prepare the object.", "18.6.2"}},
        {"",
         "post_randomize",
         {"Callback invoked after successful randomization; override it to process the new values.",
          "18.6.2"}},
        {"", "srandom", {"Seeds this object's random number generator.", "18.13.3"}},
        {"",
         "get_randstate",
         {"Returns this object's random number generator state as a string.", "18.13.4"}},
        {"",
         "set_randstate",
         {"Restores this object's random number generator state from a previously saved string.",
          "18.13.5"}},
        {"",
         "rand_mode",
         {"Enables or disables randomization of all random variables in this object.", "18.8"}},
        {"",
         "constraint_mode",
         {"Enables or disables all constraint blocks in this object.", "18.9"}},
    };
    for (auto& entry : docs) {
        if (entry.owner == owner && entry.name == method.name)
            return &entry.doc;
    }
    return nullptr;
}

std::string formatBuiltinMethodSignature(const ast::SubroutineSymbol& method) {
    std::string signature;
    if (method.flags.has(ast::MethodFlags::Static))
        signature += "static ";
    if (method.flags.has(ast::MethodFlags::Virtual))
        signature += "virtual ";
    signature += method.subroutineKind == ast::SubroutineKind::Task ? "task" : "function";
    if (method.subroutineKind == ast::SubroutineKind::Function &&
        !method.flags.has(ast::MethodFlags::Constructor))
        signature += " " + method.getReturnType().toString();
    signature += fmt::format(" {}(", method.name);
    if (method.flags.has(ast::MethodFlags::Randomize))
        signature += "...";
    for (auto* argument : method.getArguments()) {
        if (argument != method.getArguments().front())
            signature += ", ";
        signature += fmt::format("{} {} {}", portString(argument->direction),
                                 argument->getType().toString(), argument->name);
        if (auto* value = argument->getDefaultValue()) {
            if (value->syntax)
                signature += " = " + value->syntax->toString();
            else if (auto* literal = value->as_if<ast::IntegerLiteral>())
                signature += " = " + formatConstantValue(literal->getValue());
        }
    }
    return signature + ");";
}

} // namespace server
