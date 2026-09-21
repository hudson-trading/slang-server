//------------------------------------------------------------------------------
// ShallowCompilation.h
// Compilation state shared by a source file and its included-file analyses
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "ast/ActiveDesignContext.h"
#include <memory>
#include <optional>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/AnalysisOptions.h"
#include "slang/analysis/AnalysisQueries.h"
#include "slang/ast/Compilation.h"
#include "slang/diagnostics/Diagnostics.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"
#include "slang/util/Bag.h"

namespace server {

class ServerCompilation;

/// Owns shallow elaboration and analysis shared by all file views of a primary syntax tree.
class ShallowCompilation {
public:
    /// Build a compilation from the primary tree and its dependencies, applying active-design
    /// overrides when a full design is available.
    ShallowCompilation(slang::SourceManager& sourceManager,
                       std::shared_ptr<slang::syntax::SyntaxTree> tree, const slang::Bag& options,
                       const std::vector<std::shared_ptr<slang::syntax::SyntaxTree>>& dependencies,
                       const ServerCompilation* design);

    /// Source manager supplying the buffers used by this compilation.
    slang::SourceManager& getSourceManager() const { return m_sourceManager; }

    /// Primary syntax tree, including the source file and all of its included buffers.
    const std::shared_ptr<slang::syntax::SyntaxTree>& getSyntaxTree() const { return m_tree; }

    /// Slang compilation owning the symbols referenced by per-file indexes.
    const std::unique_ptr<slang::ast::Compilation>& getCompilation() const { return m_compilation; }

    /// Whether every syntax tree still uses the latest source buffers.
    bool hasValidBuffers() const;

    /// Elaborate definitions from the primary tree and return compilation-wide diagnostics.
    const slang::Diagnostics& getSemanticDiagnostics();

    /// Run driver analysis once, returning null when there are no top instances.
    const slang::analysis::AnalysisManager* getAnalysisManager();

    /// Return cached driver-analysis diagnostics, computing them on first use.
    slang::Diagnostics getAnalysisDiags();

    /// Return drivers from the active design, falling back to shallow analysis.
    std::vector<const slang::analysis::ValueDriver*> getDrivers(
        const slang::ast::ValueSymbol& symbol);

    /// Whether this compilation has bindings to a selected full-design instance.
    bool hasActiveDesign() const { return m_activeDesign.has_value(); }

    /// Return the full-design symbol corresponding to a shallow symbol, if available.
    const slang::ast::Symbol* getDesignSymbol(const slang::ast::Symbol& shallowSymbol) const {
        return m_activeDesign ? m_activeDesign->getDesignSymbol(shallowSymbol) : nullptr;
    }

    /// Return the selected full-design connection for an interface port, if available.
    const InterfaceConnection* getActiveInterfaceConnection(
        const slang::ast::InterfacePortSymbol& port) const {
        return m_activeDesign ? m_activeDesign->getInterfaceConnection(port) : nullptr;
    }

private:
    /// Source manager used to validate the compilation's buffers.
    slang::SourceManager& m_sourceManager;

    /// Primary tree whose definitions receive shallow semantic elaboration.
    std::shared_ptr<slang::syntax::SyntaxTree> m_tree;

    /// Primary tree and dependency trees used to construct the compilation.
    std::vector<std::shared_ptr<slang::syntax::SyntaxTree>> m_allTrees;

    /// Owns all shallow symbols and semantic diagnostics.
    std::unique_ptr<slang::ast::Compilation> m_compilation;

    /// Selected full-design instances and overrides used by every file view.
    std::optional<ActiveDesignContext> m_activeDesign;

    /// Options used when lazily running driver analysis.
    slang::analysis::AnalysisOptions m_analysisOptions;

    /// Driver analysis shared by all file views of the compilation.
    std::unique_ptr<slang::analysis::AnalysisManager> m_driverAnalysis;

    /// Instance-aware driver queries and localized drivers shared by all file views.
    std::unique_ptr<slang::analysis::AnalysisQueries> m_analysisQueries;

    /// Cached driver-analysis diagnostics with irrelevant unused-definition warnings removed.
    std::optional<slang::Diagnostics> m_cachedAnalysisDiags;

    /// Whether definitions from the primary tree have received semantic elaboration.
    bool m_editedDefinitionsElaborated = false;
};

} // namespace server
