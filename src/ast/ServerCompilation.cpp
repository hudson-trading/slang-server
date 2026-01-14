//------------------------------------------------------------------------------
// ServerCompilation.cpp
// Implementation of server compilation class
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "ast/ServerCompilation.h"

#include "ast/HierarchicalView.h"
#include "ast/InstanceVisitor.h"
#include "util/Converters.h"
#include "util/Logging.h"
#include <memory>

#include "slang/ast/Compilation.h"
#include "slang/text/SourceManager.h"

namespace fs = std::filesystem;

namespace server {

ServerCompilation::ServerCompilation(std::vector<std::shared_ptr<SlangDoc>> documents, Bag options,
                                     SourceManager& sourceManager, std::optional<std::string> top) :
    m_documents(std::move(documents)), m_options(std::move(options)), m_top(std::move(top)),
    m_sourceManager(sourceManager) {

    if (m_top) {
        m_options.insertOrGet<slang::ast::CompilationOptions>().topModules = {*m_top};
    }
    refresh();
}

void ServerCompilation::refresh() {
    m_analysis = std::make_unique<ServerCompilationAnalysis>(m_documents, m_options,
                                                             m_sourceManager);
}

std::vector<hier::InstanceSet> ServerCompilation::getScopesByModule() {
    std::vector<hier::InstanceSet> result;
    for (auto& [_name, instances] : m_analysis->instances.moduleToInstances) {
        if (instances.size() == 0) {
            continue;
        }

        auto& definition = instances[0]->getDefinition();
        auto instSet = hier::InstanceSet{
            .declName = std::string(definition.name),
            .declLoc = toLocation(definition.getSyntax()->sourceRange(), m_sourceManager),
            .instCount = instances.size(),
        };
        if (instances.size() == 1) {
            instSet.inst = hier::toQualifiedInstance(*instances[0], m_sourceManager);
        }
        result.push_back(instSet);
    }
    return result;
}

std::vector<hier::QualifiedInstance> ServerCompilation::getInstancesOfModule(
    const std::string& moduleName) {
    auto it = m_analysis->instances.moduleToInstances.find(moduleName);
    if (it == m_analysis->instances.moduleToInstances.end()) {
        return {};
    }
    std::vector<hier::QualifiedInstance> result;
    for (auto& inst : it->second) {
        result.push_back(hier::toQualifiedInstance(*inst, m_sourceManager));
    }
    return result;
}

std::vector<hier::HierItem_t> ServerCompilation::getScope(const std::string& hierPath) {
    auto& root = m_analysis->compilation.getRoot();

    if (hierPath.empty()) {
        std::vector<hier::HierItem_t> result;
        for (auto& inst : root.topInstances) {
            INFO("Adding top instance {}", inst->name);
            hier::handleInstance(result, *inst, m_sourceManager, true);
        }
        for (auto& pkg : m_analysis->compilation.getPackages()) {
            hier::handlePackage(result, *pkg, m_sourceManager);
        }
        return result;
    }

    const slang::ast::Scope* scope = nullptr;
    {
        auto sym = root.lookupName(hierPath, ast::LookupLocation::max,
                                   ast::LookupFlags::AllowUnnamedGenerate);
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
        scope = m_analysis->compilation.getPackage(hierPath);
        if (!scope) {
            ERROR("Failed to find symbol at path {}", hierPath);
            return {};
        }
    }
    return hier::getScopeChildren(*scope, m_sourceManager);
}

std::vector<std::string> ServerCompilation::getInstances(
    const lsp::TextDocumentPositionParams& params) {
    std::shared_ptr<SlangDoc> doc;
    // NOCOMMIT -- be better, index by URI
    for (const auto& document : m_documents) {
        if (document->getPath() == params.textDocument.uri.getPath()) {
            doc = document;
            break;
        }
    }
    if (!doc) {
        ERROR("Unknown doc: {}", params.textDocument.uri.getPath());
        return {};
    }
    auto location = m_sourceManager.getSourceLocation(doc->getBuffer(), params.position.line,
                                                      params.position.character);
    if (location) {
        inst::InstanceVisitor visitor(*location);
        m_analysis->compilation.getRoot().visit(visitor);
        return visitor.getInstances();
    }

    return {};
}

std::optional<std::vector<lsp::CallHierarchyItem>> ServerCompilation::getDocPrepareCallHierarchy(
    const lsp::CallHierarchyPrepareParams& params) {
    lsp::TextDocumentPositionParams posParams{
        .textDocument = params.textDocument,
        .position = params.position,
    };
    std::vector<lsp::CallHierarchyItem> result;
    for (const auto& instance : getInstances(posParams)) {
        // TODO -- trace aggregates too
        // TODO -- remove isWcpVariable once not needed here
        if (!isWcpVariable(instance)) {
            continue;
        }
        // TODO: change to doc of actual symbol, not the declToken
        result.emplace_back(
            lsp::CallHierarchyItem{.name = instance, .uri = params.textDocument.uri});
    }
    return std::optional(result);
}

bool ServerCompilation::isWcpVariable(const std::string& path) {
    const auto& root = m_analysis->compilation.getRoot();
    slang::ast::LookupResult result;
    slang::ast::ASTContext context(root, ast::LookupLocation::max);
    slang::ast::Lookup::name(m_analysis->compilation.parseName(path), context,
                             ast::LookupFlags::None, result);

    if (!result.found) {
        return false;
    }

    if (const auto val = result.found->as_if<slang::ast::ValueSymbol>()) {
        const slang::ast::Type* type = &val->getType().getCanonicalType();
        for (size_t sel = 0; sel < result.selectors.size(); sel++) {
            if (type->isStruct()) {
                const auto scope = type->as_if<slang::ast::Scope>();
                if (!scope) {
                    return false;
                }
                const auto selector = std::get_if<slang::ast::LookupResult::MemberSelector>(
                    &result.selectors[sel]);
                if (!selector) {
                    return false;
                }
                const auto child = scope->find(selector->name);
                if (!child) {
                    return false;
                }
                const auto field = child->as_if<slang::ast::FieldSymbol>();
                if (!field) {
                    return false;
                }
                type = &field->getType().getCanonicalType();
            }
            else if (type->isArray()) {
                if (type->getArrayElementType()->isSimpleBitVector()) {
                    return true;
                }
            }
        }
        if (type->isSimpleBitVector()) {
            return true;
        }
    }

    return false;
}

std::optional<lsp::ShowDocumentParams> ServerCompilation::getHierDocParams(
    const std::string& path) {
    // TODO -- structs / nested structs -- currently taken to variable instance, not
    // type definition -- do we want both?
    slang::ast::LookupResult result;
    slang::ast::ASTContext context(m_analysis->compilation.getRoot(),
                                   slang::ast::LookupLocation::max);
    slang::ast::Lookup::name(m_analysis->compilation.parseName(path), context,
                             slang::ast::LookupFlags::None, result);
    if (!result.found) {
        return std::nullopt;
    }
    auto loc = result.found->location;
    if (!loc.valid()) {
        return std::nullopt;
    }
    auto fullPath = fs::absolute(m_sourceManager.getFileName(loc));
    return lsp::ShowDocumentParams{.uri = URI::fromFile(fullPath),
                                   .external = false,
                                   .takeFocus = true,
                                   .selection = std::optional<lsp::Range>(
                                       toRange(loc, m_sourceManager, result.found->name.length()))};
}

void ServerCompilation::issueDiagnosticsTo(slang::DiagnosticEngine& diagEngine) {
    m_analysis->issueDiagnosticsTo(diagEngine);
}

} // namespace server
