// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "ast/InstanceIndexer.h"

#include <algorithm>
#include <limits>
#include <tuple>
#include <unordered_map>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"

namespace server {
using namespace slang;
using namespace slang::ast;

namespace {

/// Collect direct sites through scopes and arrays, stopping at each instance boundary.
struct DirectInstances : ASTVisitor<DirectInstances> {
    std::vector<const InstanceSymbol*> sites;

    void handle(const std::derived_from<Scope> auto& scope) {
        if (!scope.getCompilation().hasFatalErrors()) {
            visitDefault(scope);
            return;
        }
        // Keep the hierarchy already elaborated by slang when it stopped on a fatal diagnostic.
        for (auto* member = scope.getFirstMember(); member; member = member->getNextSibling())
            member->visit(*this);
    }

    void handle(const InstanceSymbol& instance) {
        if (!instance.body.flags.has(InstanceFlags::Uninstantiated))
            sites.push_back(&instance);
    }
};

} // namespace

void InstanceIndexer::build(Compilation& compilation) {
    m_modules.clear();
    m_bodies.clear();
    m_bodies.emplace_back(); // Terminal body for a recursive back edge.
    compilation.getSemanticDiagnostics();
    std::unordered_map<const InstanceBodySymbol*, size_t> indexed;
    auto buildBody = [&](auto&& self, const Symbol& scope, std::string_view prefix) -> size_t {
        DirectInstances direct;
        scope.visit(direct);
        std::vector<Child> children;
        children.reserve(direct.sites.size());
        for (auto* site : direct.sites) {
            const auto& definition = site->getDefinition();
            auto& module = m_modules[definition.name];
            if (!module.definition)
                module.definition = &definition;

            auto* body = site->getCanonicalBody();
            if (!body)
                body = &site->body;
            auto [it, inserted] = indexed.try_emplace(body, 0);
            auto& bodyIndex = it->second;
            if (inserted) {
                // A back edge to a body still being visited ends at the terminal body.
                bodyIndex = self(self, *body, body->parentInstance->getHierarchicalPath());
            }
            auto path = site->getHierarchicalPath();
            SLANG_ASSERT(path.starts_with(prefix));
            children.push_back({site, path.substr(prefix.size()), bodyIndex});
        }
        m_bodies.push_back(std::move(children));
        return m_bodies.size() - 1;
    };
    buildBody(buildBody, compilation.getRoot(), "");

    struct Parents {
        size_t count = 0;
        std::string bestPath;
    };
    std::vector<Parents> parents(m_bodies.size());
    parents.back().count = 1;
    auto addCount = [](size_t& count, size_t n) {
        count += std::min(n, std::numeric_limits<size_t>::max() - count);
    };
    auto depth = [](std::string_view path) { return std::ranges::count(path, '.'); };
    auto preference = [&](const InstanceSymbol& site, std::string_view path) {
        auto loc = site.getSyntax() ? site.getSyntax()->sourceRange().start() : site.location;
        return std::tuple(depth(path), !loc.valid(), loc, path);
    };
    // Visit parents before children so each site inherits the total number of paths to its body.
    for (size_t i = m_bodies.size(); i-- > 0;) {
        const auto& parent = parents[i];
        for (const auto& child : m_bodies[i]) {
            auto path = parent.bestPath + child.relativePath;
            auto& module = m_modules.at(child.site->getDefinition().name);
            addCount(module.count, parent.count);
            if (!module.preferredSite ||
                preference(*child.site, path) <
                    preference(*module.preferredSite, module.preferredPath)) {
                module.preferredSite = child.site;
                module.preferredPath = path;
            }
            // The separator affects lexical order when one parent name is a prefix of another.
            auto& next = parents[child.body];
            if (!next.count || std::pair(depth(path), path + '.') <
                                   std::pair(depth(next.bestPath), next.bestPath + '.'))
                next.bestPath = std::move(path);
            addCount(next.count, parent.count);
        }
    }
}

void InstanceIndexer::visit(std::string_view moduleName,
                            function_ref<void(const InstanceSymbol&, std::string_view)> fn) const {
    auto it = m_modules.find(moduleName);
    if (it == m_modules.end())
        return;
    if (it->second.count == 1) {
        fn(*it->second.preferredSite, it->second.preferredPath);
        return;
    }
    // Only unfold branches containing this module. Other shared subtrees may have exponentially
    // many occurrences even when the requested module has just one.
    std::vector<bool> contains(m_bodies.size());
    for (size_t i = 0; i < m_bodies.size(); ++i) {
        contains[i] = std::ranges::any_of(m_bodies[i], [&](const Child& child) {
            return child.site->getDefinition().name == moduleName || contains[child.body];
        });
    }
    std::string path;
    auto expand = [&](auto&& self, size_t body) -> void {
        auto prefixLength = path.size();
        for (const auto& child : m_bodies[body]) {
            bool match = child.site->getDefinition().name == moduleName;
            if (!match && !contains[child.body])
                continue;
            path += child.relativePath;
            if (match)
                fn(*child.site, path);
            if (contains[child.body])
                self(self, child.body);
            path.resize(prefixLength);
        }
    };
    expand(expand, m_bodies.size() - 1);
}

const InstanceSymbol* InstanceIndexer::resolve(const Module& module, std::string_view path) const {
    if (module.resolved && module.resolved->getHierarchicalPath() == path)
        return module.resolved;
    if (module.preferredSite->getHierarchicalPath() == path)
        return module.preferredSite;

    // Match slang's own path spelling, including escaped names and unnamed generate blocks.
    // Only the ancestors of the requested occurrence need concrete instance bodies.
    auto follow = [&](auto&& self, const InstanceSymbol& site) -> const InstanceSymbol* {
        auto candidate = site.getHierarchicalPath();
        if (path == candidate && site.getDefinition().name == module.definition->name)
            return &site;
        if (!path.starts_with(candidate) ||
            (path.size() > candidate.size() && path[candidate.size()] != '.'))
            return nullptr;
        DirectInstances direct;
        site.body.visit(direct);
        for (auto* child : direct.sites) {
            if (auto* found = self(self, *child))
                return found;
        }
        return nullptr;
    };
    for (const auto& root : m_bodies.back()) {
        if (auto* found = follow(follow, *root.site)) {
            module.resolved = found;
            return found;
        }
    }
    return nullptr;
}

} // namespace server
