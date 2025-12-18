//------------------------------------------------------------------------------
// SlangExtensions.h
// Functions that only deal with slang objects and could potentially go upstream
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once

#include <memory>

#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

namespace server {

/// @brief Check if a syntax tree has valid (latest) buffers in the source manager
/// @param sm The source manager to check against
/// @param tree The syntax tree to validate
/// @return true if all buffers in the tree are up-to-date
bool hasValidBuffers(const slang::SourceManager& sm,
                     const std::shared_ptr<slang::syntax::SyntaxTree>& tree);

} // namespace server
