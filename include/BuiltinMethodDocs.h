// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <string_view>

namespace slang::ast {
class SubroutineSymbol;
}

namespace server {

/// Documentation for a built-in class method or std::randomize.
struct BuiltinMethodDoc {
    /// Method behavior, independent of generic class specialization.
    std::string_view description;
    /// Reference to IEEE 1800-2023.
    std::string_view ieeeSection;
};

/// Find documentation by the method's semantic owner; user methods never match.
const BuiltinMethodDoc* getBuiltinMethodDoc(const slang::ast::SubroutineSymbol& method);

/// Render the specialized signature, including flags, directions and default arguments.
std::string formatBuiltinMethodSignature(const slang::ast::SubroutineSymbol& method);

} // namespace server
