//------------------------------------------------------------------------------
// ServerCompilation.cpp
// Implementation of server compilation class
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "ast/ServerCompilation.h"

#include "ast/HierarchicalView.h"
#include "util/Converters.h"
#include "util/Logging.h"
#include <memory>

#include "slang/ast/Compilation.h"
#include "slang/text/SourceManager.h"

namespace server {

ServerCompilation::ServerCompilation(std::vector<std::shared_ptr<SlangDoc>> documents, Bag options,
                                     SourceManager& m_sourceManager) :
    m_documents(std::move(documents)), m_options(std::move(options)),
    m_sourceManager(m_sourceManager) {
    refresh();
}

void ServerCompilation::refresh() {
    // Create a new compilation

    // The main compilation is allowed to refer to old buffers
    m_sourceManager.clearOldBuffers();
    comp = std::make_unique<slang::ast::Compilation>(m_options);
    for (auto& doc : m_documents) {
        comp->addSyntaxTree(doc->getSyntaxTree());
    }

    // reset and rebuild indexed info
    auto& root = comp->getRoot();
    m_instances.reset(&root);
}

std::vector<hier::InstanceSet> ServerCompilation::getScopesByModule() {
    std::vector<hier::InstanceSet> result;
    for (auto& it : m_instances.syntaxToInstance) {
        if (it.second.size() == 0) {
            continue;
        }

        std::optional<hier::QualifiedInstance> instance;
        if (it.second.size() == 1) {
            instance.emplace(hier::toQualifiedInstance(*it.second[0], m_sourceManager));
        }
        auto& definition = it.second[0]->getDefinition();
        result.push_back(hier::InstanceSet{
            .declName = std::string(definition.name),
            .declLoc = toLocation(definition.getSyntax()->sourceRange(), m_sourceManager),
            .instCount = it.second.size(),
            .inst = instance,
        });
    }
    return result;
}

std::vector<hier::QualifiedInstance> ServerCompilation::getInstancesOfModule(
    const std::string& moduleName) {
    auto it = m_instances.syntaxToInstance.find(moduleName);
    if (it == m_instances.syntaxToInstance.end()) {
        return {};
    }
    std::vector<hier::QualifiedInstance> result;
    for (auto& inst : it->second) {
        result.push_back(hier::toQualifiedInstance(*inst, m_sourceManager));
    }
    return result;
}

std::vector<hier::HierItem_t> ServerCompilation::getScope(const std::string& hierPath) {
    auto& root = comp->getRoot();

    if (hierPath.empty()) {
        std::vector<hier::HierItem_t> result;
        for (auto& inst : root.topInstances) {
            INFO("Adding top instance {}", inst->name);
            hier::handleInstance(result, *inst, m_sourceManager, true);
        }
        for (auto& pkg : comp->getPackages()) {
            hier::handlePackage(result, *pkg, m_sourceManager);
        }
        return result;
    }

    const slang::ast::Scope* scope = nullptr;
    {
        auto sym = root.lookupName(hierPath);
        if (sym) {
            switch (sym->kind) {
                case slang::ast::SymbolKind::Instance:
                    scope = &sym->as_if<slang::ast::InstanceSymbol>()->body;
                    break;
                default:
                    ERROR("Unknown symbol kind for getScope: {}", toString(sym->kind));
                    return {};
            }
        }
    }
    if (!scope) {
        scope = comp->getPackage(hierPath);
        if (!scope) {
            ERROR("Failed to find symbol at path {}", hierPath);
            return {};
        }
    }
    return hier::getScopeChildren(*scope, m_sourceManager);
}

} // namespace server
