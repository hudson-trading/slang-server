//------------------------------------------------------------------------------
// JsonTypes.h
// Common JSON type definitions for LSP protocol
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once

#include <rfl/json.hpp>
#include <string>
namespace lsp {

using LSPObject = rfl::Object<std::string>;

using LSPArray = std::vector<rfl::Generic>;

using LSPAny = rfl::Generic;

} // namespace lsp