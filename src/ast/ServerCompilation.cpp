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
#include "lsp/LspClient.h"
#include "util/Converters.h"
#include "util/Logging.h"
#include <algorithm>
#include <memory>

#include "slang/ast/Compilation.h"
#include "slang/text/SourceManager.h"

namespace server {

namespace {

const slang::ast::Symbol* lookupHierSymbol(slang::ast::Compilation& compilation,
                                           std::string_view path) {
    if (path.empty()) {
        return nullptr;
    }

    slang::ast::LookupResult result;
    slang::ast::ASTContext context(compilation.getRoot(), ast::LookupLocation::max);
    slang::ast::Lookup::name(compilation.parseName(path), context,
                             ast::LookupFlags::AllowUnnamedGenerate, result);
    return result.found;
}

// Walks up from `symbol` to the nearest enclosing module/interface instance body, or nullptr if
// the symbol isn't inside one.
const slang::ast::InstanceBodySymbol* enclosingInstanceBody(const slang::ast::Symbol& symbol) {
    for (auto* scope = symbol.getParentScope(); scope; scope = scope->asSymbol().getParentScope()) {
        if (scope->asSymbol().kind == slang::ast::SymbolKind::InstanceBody) {
            return &scope->asSymbol().as<slang::ast::InstanceBodySymbol>();
        }
    }
    return nullptr;
}

std::optional<lsp::ShowDocumentParams> makeShowDocumentParams(const SourceRange& range,
                                                              const SourceManager& sm) {
    if (range == SourceRange::NoLocation || !range.start().valid()) {
        return std::nullopt;
    }

    auto location = toLocation(range, sm);
    if (!location.uri.getPath().empty()) {
        return lsp::ShowDocumentParams{.uri = location.uri,
                                       .takeFocus = true,
                                       .selection = location.range};
    }

    // No direct URI (synthesized buffer): fall back to the original/expansion location.
    auto originalRange = sm.getFullyOriginalRange(range);
    if (originalRange == SourceRange::NoLocation || !originalRange.start().valid()) {
        return std::nullopt;
    }
    auto fullPath = sm.getFullPath(originalRange.start().buffer());
    if (fullPath.empty()) {
        return std::nullopt;
    }
    return lsp::ShowDocumentParams{.uri = URI::fromFile(fullPath),
                                   .takeFocus = true,
                                   .selection = toRange(originalRange, sm)};
}

std::optional<lsp::ShowDocumentParams> makeShowDocumentParams(const SourceLocation& loc,
                                                              size_t length,
                                                              const SourceManager& sm) {
    if (!loc.valid()) {
        return std::nullopt;
    }
    return makeShowDocumentParams(SourceRange(loc, loc + length), sm);
}

} // namespace

const slang::ast::Symbol* ServerCompilation::toShallowSymbol(
    const slang::ast::Symbol& symbol) const {
    using slang::ast::SymbolKind;

    // A bare module definition (callers pass inst->getDefinition()) has no place in the instance
    // hierarchy to climb; look it up directly by name.
    if (symbol.kind == SymbolKind::Definition) {
        auto docIt = m_moduleToDoc.find(std::string(symbol.name));
        if (docIt != m_moduleToDoc.end()) {
            auto& shallowComp = *docIt->second->getAnalysis()->getCompilation();
            if (auto* result = lookupHierSymbol(shallowComp, symbol.name)) {
                return result;
            }
        }
        WARN("toShallowSymbol: could not resolve definition {}", symbol.name);
        return nullptr;
    }

    // slang's hierarchical path already has correct generate/array indices and separators, e.g.
    // "cpu_testbench.dut.gen_alu_array[1].gen_alu_inst". We re-root it at an enclosing module by
    // replacing the prefix up to that module's instance with the module name, then resolve against
    // that module's shallow compilation. We climb module-by-module (innermost first) and stop at
    // the shallowest enclosing module that is actually a top instance of its file's shallow comp.
    // Climbing is needed because a non-top module (e.g. two modules in one file where one
    // instantiates the other) can't be a lookup root on its own.
    auto fullPath = symbol.getHierarchicalPath();
    for (auto* body = enclosingInstanceBody(symbol); body;
         body = enclosingInstanceBody(*body->parentInstance)) {
        auto moduleName = std::string(body->getDefinition().name);
        auto docIt = m_moduleToDoc.find(moduleName);
        if (docIt == m_moduleToDoc.end()) {
            continue;
        }

        // Re-root: drop everything up to and including the enclosing instance's path, and prefix
        // the module name. The instance's path is a prefix of the symbol's full path.
        auto instPath = body->parentInstance->getHierarchicalPath();
        std::string_view rel = fullPath;
        if (!rel.starts_with(instPath)) {
            continue;
        }
        rel.remove_prefix(instPath.size());
        auto path = fmt::format("{}{}", moduleName, rel);

        auto& shallowComp = *docIt->second->getAnalysis()->getCompilation();
        if (auto* result = lookupHierSymbol(shallowComp, path)) {
            return result;
        }
    }

    WARN("toShallowSymbol: could not resolve {} in any shallow top", fullPath);
    return nullptr;
}

ServerCompilation::ServerCompilation(std::vector<std::shared_ptr<SlangDoc>> documents, Bag options,
                                     SourceManager& sourceManager, lsp::LspClient& client,
                                     std::optional<std::string> top) :
    m_documents(std::move(documents)), m_options(std::move(options)), m_top(std::move(top)),
    m_sourceManager(sourceManager), m_client(client) {

    if (m_top) {
        m_options.insertOrGet<slang::ast::CompilationOptions>().topModules = {*m_top};
    }
    indexModuleDocs();
    refresh();
}

void ServerCompilation::indexModuleDocs() {
    m_moduleToDoc.clear();
    for (const auto& doc : m_documents) {
        auto tree = doc->getSyntaxTree();
        if (!tree) {
            continue;
        }
        for (auto name : tree->getMetadata().getDeclaredSymbols()) {
            m_moduleToDoc.emplace(std::string(name), doc);
        }
    }
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

const std::vector<const slang::ast::InstanceSymbol*>& ServerCompilation::getInstancesOfModule(
    const std::string& moduleName) const {
    static const std::vector<const slang::ast::InstanceSymbol*> empty;
    auto it = m_analysis->instances.moduleToInstances.find(moduleName);
    if (it == m_analysis->instances.moduleToInstances.end()) {
        return empty;
    }
    return it->second;
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

std::optional<lsp::Location> ServerCompilation::getHierLocation(const std::string& hierPath) {
    auto& comp = m_analysis->compilation;
    const slang::ast::Symbol* sym = lookupHierSymbol(comp, hierPath);

    // Fallback: package names don't show up via Lookup::name.
    if (!sym) {
        if (auto* pkg = comp.getPackage(hierPath)) {
            sym = &pkg->asSymbol();
        }
    }

    // Edit fallback: the full compilation is stale (only refreshed on save). A signal
    // typed since the last build won't resolve in the full comp, so split the path at
    // the deepest dot-segment that still resolves to an instance and look up the
    // remainder against that module's shallow compilation (which rebuilds per-edit).
    if (!sym) {
        for (auto dot = hierPath.rfind('.'); dot != std::string::npos;
             dot = hierPath.rfind('.', dot - 1)) {
            auto prefix = hierPath.substr(0, dot);
            auto* parentSym = lookupHierSymbol(comp, prefix);
            auto* inst = parentSym ? parentSym->as_if<slang::ast::InstanceSymbol>() : nullptr;
            if (!inst) {
                if (dot == 0) {
                    break;
                }
                continue;
            }
            auto* freshDefSym = toShallowSymbol(inst->getDefinition());
            auto* freshDef = freshDefSym ? freshDefSym->as_if<slang::ast::DefinitionSymbol>()
                                         : nullptr;
            if (!freshDef) {
                break;
            }

            std::string remainder = hierPath.substr(dot + 1);
            auto modulePath = fmt::format("{}.{}", freshDef->name, remainder);
            auto fullPath = m_sourceManager.getFullPath(freshDef->location.buffer()).string();
            auto docIt = std::ranges::find_if(m_documents, [&](const auto& doc) {
                return doc->getPath() == fullPath;
            });
            if (docIt == m_documents.end()) {
                break;
            }
            auto& shallowComp = *(*docIt)->getAnalysis()->getCompilation();
            sym = lookupHierSymbol(shallowComp, modulePath);
            break;
        }
    }

    if (!sym) {
        WARN("getHierLocation: {} was not found as a symbol or package", hierPath);
        return std::nullopt;
    }

    // Re-resolve to an open-document symbol when possible, so we land in a buffer the editor
    // actually has loaded.
    if (auto* freshSym = toShallowSymbol(*sym)) {
        sym = freshSym;
    }

    if (auto* inst = sym->as_if<slang::ast::InstanceSymbol>()) {
        auto* syntax = inst->getSyntax();
        if (!syntax) {
            syntax = inst->getDefinition().getSyntax();
        }
        if (!syntax) {
            return std::nullopt;
        }
        auto params = makeShowDocumentParams(syntax->sourceRange(), m_sourceManager);
        if (!params || !params->selection) {
            return std::nullopt;
        }
        return lsp::Location{.uri = params->uri, .range = *params->selection};
    }

    auto params = makeShowDocumentParams(sym->location, sym->name.length(), m_sourceManager);
    if (!params || !params->selection) {
        if (auto* syntax = sym->getSyntax()) {
            params = makeShowDocumentParams(syntax->sourceRange(), m_sourceManager);
        }
    }
    if (!params || !params->selection) {
        return std::nullopt;
    }
    return lsp::Location{.uri = params->uri, .range = *params->selection};
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
    auto location = toSourceLocation(doc->getBuffer(), params.position, m_sourceManager);
    if (location) {
        inst::InstanceVisitor visitor(*location);
        m_analysis->compilation.getRoot().visit(visitor);
        return visitor.getInstances();
    }

    return {};
}

std::vector<lsp::CallHierarchyItem> ServerCompilation::getDocPrepareCallHierarchy(
    const lsp::CallHierarchyPrepareParams& params) {
    auto instances = getInstances(lsp::TextDocumentPositionParams{
        .textDocument = params.textDocument,
        .position = params.position,
    });
    if (instances.empty()) {
        m_client.showWarning("The selected signal is not part of the current design");
        return {};
    }

    std::vector<lsp::CallHierarchyItem> result;
    for (const auto& instance : instances) {
        // TODO -- trace aggregates too
        // TODO -- remove isWcpVariable once not needed here
        if (!isWcpVariable(instance)) {
            continue;
        }
        // TODO: change to doc of actual symbol, not the declToken
        result.emplace_back(
            lsp::CallHierarchyItem{.name = instance, .uri = params.textDocument.uri});
    }
    if (result.empty()) {
        m_client.showWarning("Only simple logic vectors are currently supported");
    }
    return result;
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
    auto found = result.found;
    if (auto freshSym = toShallowSymbol(*found)) {
        found = freshSym;
    }

    auto params = makeShowDocumentParams(found->location, found->name.length(), m_sourceManager);
    if (!params) {
        return std::nullopt;
    }
    params->external = false;
    return params;
}

void ServerCompilation::issueDiagnosticsTo(slang::DiagnosticEngine& diagEngine) {
    m_analysis->issueDiagnosticsTo(diagEngine);
}

} // namespace server
