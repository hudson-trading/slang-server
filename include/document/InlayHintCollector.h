// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include "Config.h"
#include "lsp/LspTypes.h"
#include <vector>

#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxFwd.h"

namespace slang {
class SourceManager;
namespace syntax {
struct NamedPortConnectionSyntax;
struct WildcardPortConnectionSyntax;
struct OrderedPortConnectionSyntax;
struct OrderedParamAssignmentSyntax;
struct InvocationExpressionSyntax;
} // namespace syntax
} // namespace slang

namespace server {

class ShallowAnalysis;

/// Collects syntax nodes for inlay hints and generates hints on demand
class InlayHintCollector {
public:
    InlayHintCollector(const ShallowAnalysis& analysis, lsp::Range range,
                       const Config::InlayHints& config);
    /// Get all inlay hints in the specified range
    std::vector<lsp::InlayHint> result;

    void collectHints();

private:
    const ShallowAnalysis& m_analysis;
    lsp::Range m_range;

    // Cached config values
    bool m_portTypes;
    bool m_orderedInstanceNames;
    bool m_wildcardNames;
    int m_funcArgNames;
    int m_macroArgNames;

    /// Handle instances- will show port/param names if ordered, names for wildcards, and types for
    /// named ports
    void handle(const slang::syntax::HierarchyInstantiationSyntax& syntax);

    /// Handle function calls- will show argument names for calls with more than one arg
    void handle(const slang::syntax::InvocationExpressionSyntax& syntax);

    /// Handle macro usages- will show argument names for calls with more than one arg
    void handle(const slang::syntax::MacroUsageSyntax& syntax);
};

} // namespace server
