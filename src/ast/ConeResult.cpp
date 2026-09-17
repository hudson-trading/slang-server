//------------------------------------------------------------------------------
// ConeResult.cpp
// Driver and load cone result conversions
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#include "ast/ConeResult.h"

#include "util/Converters.h"
#include <filesystem>

#include "slang/text/SourceManager.h"

namespace server {

std::vector<ConeEntry> ConeResult::getLocations() const {
    std::vector<ConeEntry> result;
    for (const auto& leaf : m_leaves) {
        auto range = leaf.getDeclarationRange();
        if (range.start().valid()) {
            auto fullPath = std::filesystem::absolute(m_sourceManager.getFileName(range.start()));
            result.push_back({.path = leaf.getHierarchicalPath(),
                              .location = {.uri = URI::fromFile(fullPath),
                                           .range = toRange(range, m_sourceManager)}});
        }
    }
    return result;
}

std::vector<std::string> ConeResult::getPaths() const {
    std::vector<std::string> result;
    std::set<std::string> seen;
    for (const auto& leaf : m_leaves) {
        std::string hier = leaf.getHierarchicalPath();
        if (seen.insert(hier).second) {
            result.push_back(hier);
        }
    }
    return result;
}

} // namespace server
