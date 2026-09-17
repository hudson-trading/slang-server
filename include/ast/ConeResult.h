//------------------------------------------------------------------------------
// ConeResult.h
// Driver and load cone results
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "ConeTracer.h"
#include "lsp/LspTypes.h"
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace slang {
class SourceManager;
}

namespace server {

/// A single endpoint of a driver or load cone.
struct ConeEntry {
    /// The hierarchical RTL path of the signal.
    std::string path;

    /// The declaration location of the signal.
    lsp::Location location;
};

/// Cone leaves with conversions to hierarchical paths and source locations.
/// The source manager and compilation containing the leaves must outlive this result.
class ConeResult {
public:
    /// Own the leaves and use the source manager to resolve their declaration locations.
    ConeResult(std::set<ConeLeaf> leaves, const slang::SourceManager& sourceManager) :
        m_leaves(std::move(leaves)), m_sourceManager(sourceManager) {}

    /// Return endpoints with valid declaration locations.
    std::vector<ConeEntry> getLocations() const;

    /// Return distinct hierarchical paths in the cone.
    std::vector<std::string> getPaths() const;

private:
    /// The unique symbol endpoints collected by tracing.
    std::set<ConeLeaf> m_leaves;

    /// Source manager for the compilation containing the leaves.
    const slang::SourceManager& m_sourceManager;
};

} // namespace server
