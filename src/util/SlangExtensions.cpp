//------------------------------------------------------------------------------
// SlangExtensions.cpp
// Functions that only deal with slang objects and could potentially go upstream
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "util/SlangExtensions.h"

namespace server {

using namespace slang;

bool hasValidBuffers(const SourceManager& sm, const std::shared_ptr<syntax::SyntaxTree>& tree) {
    if (!tree)
        return false;

    // Check the main buffer
    auto buffers = tree->getSourceBufferIds();
    if (buffers.empty() || !sm.isLatestData(buffers[0]))
        return false;

    // Check all included file buffers
    for (const auto& inc : tree->getIncludeDirectives()) {
        if (!sm.isLatestData(inc.buffer.id))
            return false;
    }

    return true;
}

} // namespace server
