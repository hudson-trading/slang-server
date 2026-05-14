// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include <string_view>

#include "slang/parsing/KnownSystemName.h"

namespace server {

/// @brief Hover documentation for a slang-known system task or method.
///
/// Empty fields are valid for entries that have not yet been documented;
/// `getSystemTaskDoc` returns nullptr for those.
struct SystemTaskDoc {
    /// SystemVerilog signature, e.g. `function int $bits(<type_or_expression>)`.
    std::string_view signature;
    /// Plain-text description; the hover renders it as a paragraph.
    std::string_view description;
    /// IEEE 1800-2017 reference, e.g. `21.2.1`. May be empty.
    std::string_view ieeeSection;
};

/// @returns the documentation for the given slang `KnownSystemName`, or
/// nullptr if `name` is `Unknown` or the entry is not yet documented.
const SystemTaskDoc* getSystemTaskDoc(slang::parsing::KnownSystemName name);

} // namespace server
