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

    // Check the main buffer and included buffers
    auto buffers = tree->getSourceBufferIds();
    for (auto buf : buffers) {
        if (buf.valid() && !sm.isLatestData(buf)) {
            return false;
        }
    }

    return true;
}

} // namespace server
