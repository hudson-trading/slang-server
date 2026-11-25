// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/util/Function.h"

namespace server {

/// Indexes the elaborated hierarchy, sharing the child list of each canonical instance body.
class InstanceIndexer {
public:
    /// Summary of all reachable instances with the same definition name.
    struct Module {
        /// First declaration in hierarchy traversal order, for navigation.
        const slang::ast::DefinitionSymbol* definition = nullptr;
        /// Number of concrete instance paths in the hierarchy.
        size_t count = 0;
        /// Instantiation site for the default selection, possibly under a shared parent.
        const slang::ast::InstanceSymbol* preferredSite = nullptr;
        /// Concrete path of the default selection.
        std::string preferredPath;
        /// Last concrete instance resolved for an active-design query.
        mutable const slang::ast::InstanceSymbol* resolved = nullptr;
    };

    /// Build the index after slang has determined which instance bodies are equivalent.
    void build(slang::ast::Compilation& compilation);

    /// Summaries in module-name order for the hierarchy view.
    const auto& modules() const { return m_modules; }

    /// Visit the concrete paths of one module in source hierarchy order.
    void visit(
        std::string_view moduleName,
        slang::function_ref<void(const slang::ast::InstanceSymbol&, std::string_view)> fn) const;

    /// Resolve a concrete occurrence, including nested instances below a shared body.
    const slang::ast::InstanceSymbol* resolve(const Module& module, std::string_view path) const;

private:
    /// A direct instantiation, including any intervening generate scopes or array indices.
    struct Child {
        /// Site in the representative parent body.
        const slang::ast::InstanceSymbol* site;
        /// Path from the parent instance to this site.
        std::string relativePath;
        /// Index of the child body in postorder.
        size_t body;
    };

    /// Child lists in postorder, ending with the synthetic design root.
    std::vector<std::vector<Child>> m_bodies;
    /// Instance counts and default selections keyed by definition name.
    std::map<std::string_view, Module> m_modules;
};

} // namespace server
