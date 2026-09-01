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
#include <cctype>
#include <memory>
#include <ranges>
#include <tuple>

#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/text/SourceManager.h"

namespace fs = std::filesystem;

namespace server {

namespace {

// Among instances of the same module, pick the one closest to the root, breaking ties by
// source location, then by hierarchical path. Used as a deterministic default "active"
// instance when the user hasn't picked one.
const slang::ast::InstanceSymbol* choosePreferredInstance(
    const std::vector<const slang::ast::InstanceSymbol*>& instances) {
    auto depth = [](const slang::ast::InstanceSymbol& i) {
        auto p = i.getHierarchicalPath();
        return std::count(p.begin(), p.end(), '.');
    };
    auto sortLoc = [](const slang::ast::InstanceSymbol& i) {
        auto* syntax = i.getSyntax();
        return syntax ? syntax->sourceRange().start() : i.location;
    };

    auto less = [&](const slang::ast::InstanceSymbol* a, const slang::ast::InstanceSymbol* b) {
        if (auto da = depth(*a), db = depth(*b); da != db) {
            return da < db;
        }
        auto la = sortLoc(*a), lb = sortLoc(*b);
        if (la.valid() != lb.valid()) {
            return la.valid();
        }
        if (la.valid() && la != lb) {
            return la < lb;
        }
        return a->getHierarchicalPath() < b->getHierarchicalPath();
    };

    const slang::ast::InstanceSymbol* preferred = nullptr;
    for (const auto* inst : instances) {
        if (!preferred || less(inst, preferred)) {
            preferred = inst;
        }
    }
    return preferred;
}

void setActiveInterfaceInstance(
    const slang::ast::Symbol& symbol,
    std::unordered_map<std::string, hier::QualifiedInstance>& activeInstancesByModule,
    const SourceManager& sourceManager) {
    if (auto inst = symbol.as_if<slang::ast::InstanceSymbol>()) {
        activeInstancesByModule[std::string(inst->getDefinition().name)] =
            hier::toQualifiedInstance(*inst, sourceManager);
        return;
    }

    if (auto arr = symbol.as_if<slang::ast::InstanceArraySymbol>()) {
        for (const auto* element : arr->elements) {
            if (element) {
                setActiveInterfaceInstance(*element, activeInstancesByModule, sourceManager);
            }
        }
    }
    else if (auto* port = symbol.as_if<slang::ast::InterfacePortSymbol>()) {
        auto connection = resolveInterfaceConnection(*port);
        if (connection.resolvedEndpoint && connection.resolvedEndpoint != &symbol)
            setActiveInterfaceInstance(*connection.resolvedEndpoint, activeInstancesByModule,
                                       sourceManager);
    }
}

void setConnectedInterfaceInstances(
    const slang::ast::InstanceSymbol& inst,
    std::unordered_map<std::string, hier::QualifiedInstance>& activeInstancesByModule,
    const SourceManager& sourceManager) {
    for (const auto* conn : inst.getPortConnections()) {
        if (!conn || conn->port.kind != slang::ast::SymbolKind::InterfacePort) {
            continue;
        }

        auto [connectedSym, _modport] = conn->getIfaceConn();
        if (!connectedSym) {
            continue;
        }

        setActiveInterfaceInstance(*connectedSym, activeInstancesByModule, sourceManager);
    }
}

const slang::ast::InstanceSymbol* getEnclosingInstance(const slang::ast::Symbol& symbol) {
    for (auto* scope = symbol.getParentScope(); scope; scope = scope->asSymbol().getParentScope()) {
        auto& scopeSymbol = scope->asSymbol();
        if (auto* body = scopeSymbol.as_if<slang::ast::InstanceBodySymbol>()) {
            return body->parentInstance;
        }
    }
    return nullptr;
}

void setActiveInstanceChain(
    const slang::ast::InstanceSymbol& target,
    std::unordered_map<std::string, hier::QualifiedInstance>& activeInstancesByModule,
    const SourceManager& sourceManager) {
    std::vector<const slang::ast::InstanceSymbol*> chain;
    auto* current = &target;
    while (current) {
        chain.push_back(current);
        current = getEnclosingInstance(*current);
    }

    for (auto* instance : chain | std::views::reverse) {
        setConnectedInterfaceInstances(*instance, activeInstancesByModule, sourceManager);
        // Let deeper selections override interface connections discovered on their ancestors.
        activeInstancesByModule[std::string(instance->getDefinition().name)] =
            hier::toQualifiedInstance(*instance, sourceManager);
    }
}

void setActiveGenerateScopes(
    const slang::ast::Symbol& symbol,
    std::unordered_map<std::string, std::string>& activeGenerateScopesByArray) {
    auto setActiveGenerateScope = [&](const slang::ast::GenerateBlockSymbol& block) {
        auto* parentScope = block.getParentScope();
        auto* array = parentScope
                          ? parentScope->asSymbol().as_if<slang::ast::GenerateBlockArraySymbol>()
                          : nullptr;
        if (array) {
            activeGenerateScopesByArray[array->getHierarchicalPath()] = block.getHierarchicalPath();
        }
    };

    if (auto* block = symbol.as_if<slang::ast::GenerateBlockSymbol>())
        setActiveGenerateScope(*block);
    for (auto* scope = symbol.getParentScope(); scope; scope = scope->asSymbol().getParentScope()) {
        if (auto* block = scope->asSymbol().as_if<slang::ast::GenerateBlockSymbol>())
            setActiveGenerateScope(*block);
    }
}

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

std::vector<hier::HierItem_t> getGenerateArrayChildren(
    const slang::ast::GenerateBlockArraySymbol& array, const SourceManager& sourceManager) {
    std::vector<hier::HierItem_t> result;
    for (const slang::ast::GenerateBlockSymbol* block : array.entries) {
        hier::handleBlockScope(result, *block, sourceManager,
                               fmt::format("[{}]", block->constructIndex));
    }
    return result;
}

std::vector<hier::HierItem_t> getInstanceArrayChildren(const slang::ast::InstanceArraySymbol& array,
                                                       const SourceManager& sourceManager) {
    std::vector<hier::HierItem_t> result;

    int32_t instanceIdx = array.range.left;
    int8_t step = array.range.isDescending() ? -1 : 1;
    for (const slang::ast::Symbol* element : array.elements) {
        if (auto inst = element->as_if<slang::ast::InstanceSymbol>()) {
            hier::handleInstance(result, *inst, sourceManager, fmt::format("[{}]", instanceIdx));
            instanceIdx += step;
        }
    }

    return result;
}

bool isHierarchicalScope(const slang::ast::Symbol& sym) {
    switch (sym.kind) {
        case slang::ast::SymbolKind::Instance:
        case slang::ast::SymbolKind::InstanceArray:
        case slang::ast::SymbolKind::InterfacePort:
        case slang::ast::SymbolKind::GenerateBlock:
        case slang::ast::SymbolKind::GenerateBlockArray:
        case slang::ast::SymbolKind::Package:
            return true;
        default:
            return false;
    }
}

void collectGenerateArrays(const slang::ast::Scope& scope,
                           const slang::syntax::LoopGenerateSyntax& loop,
                           std::vector<const slang::ast::GenerateBlockArraySymbol*>& result) {
    for (auto& member : scope.members()) {
        if (auto* array = member.as_if<slang::ast::GenerateBlockArraySymbol>()) {
            if (array->getSyntax() == &loop)
                result.push_back(array);
            for (auto* entry : array->entries)
                collectGenerateArrays(*entry, loop, result);
        }
        else if (auto* block = member.as_if<slang::ast::GenerateBlockSymbol>()) {
            collectGenerateArrays(*block, loop, result);
        }
    }
}

std::optional<size_t> getActiveAncestorCount(
    const slang::ast::GenerateBlockArraySymbol& array,
    const std::unordered_map<std::string, std::string>& activeGenerateScopesByArray) {
    size_t count = 0;
    for (auto* scope = array.getParentScope(); scope; scope = scope->asSymbol().getParentScope()) {
        auto* block = scope->asSymbol().as_if<slang::ast::GenerateBlockSymbol>();
        if (!block)
            continue;

        auto* parentScope = block->getParentScope();
        auto* parentArray =
            parentScope ? parentScope->asSymbol().as_if<slang::ast::GenerateBlockArraySymbol>()
                        : nullptr;
        if (!parentArray)
            continue;

        auto active = activeGenerateScopesByArray.find(parentArray->getHierarchicalPath());
        if (active == activeGenerateScopesByArray.end())
            continue;
        if (active->second != block->getHierarchicalPath())
            return std::nullopt;
        count++;
    }
    return count;
}

const slang::ast::GenerateBlockArraySymbol* chooseGenerateArray(
    const std::vector<const slang::ast::GenerateBlockArraySymbol*>& arrays,
    const std::unordered_map<std::string, std::string>& activeGenerateScopesByArray) {
    const slang::ast::GenerateBlockArraySymbol* best = nullptr;
    std::tuple<int, int, std::string> bestKey;
    for (auto* array : arrays) {
        auto activeAncestorCount = getActiveAncestorCount(*array, activeGenerateScopesByArray);
        auto key = std::tuple(activeAncestorCount ? 0 : 1,
                              -static_cast<int>(activeAncestorCount.value_or(0)),
                              array->getHierarchicalPath());
        if (!best || key < bestKey) {
            best = array;
            bestKey = std::move(key);
        }
    }
    return best;
}

std::vector<hier::HierItem_t> getChildrenForScopeSymbol(const slang::ast::Symbol& sym,
                                                        const SourceManager& sourceManager) {
    switch (sym.kind) {
        case slang::ast::SymbolKind::Instance:
            return hier::getScopeChildren(sym.as<slang::ast::InstanceSymbol>().body, sourceManager);
        case slang::ast::SymbolKind::InstanceArray:
            return getInstanceArrayChildren(sym.as<slang::ast::InstanceArraySymbol>(),
                                            sourceManager);
        case slang::ast::SymbolKind::InterfacePort:
            return hier::getInterfacePortChildren(sym.as<slang::ast::InterfacePortSymbol>(),
                                                  sourceManager);
        case slang::ast::SymbolKind::GenerateBlock:
            return hier::getScopeChildren(sym.as<slang::ast::GenerateBlockSymbol>(), sourceManager);
        case slang::ast::SymbolKind::GenerateBlockArray:
            return getGenerateArrayChildren(sym.as<slang::ast::GenerateBlockArraySymbol>(),
                                            sourceManager);
        case slang::ast::SymbolKind::Package:
            return hier::getScopeChildren(sym.as<slang::ast::PackageSymbol>(), sourceManager);
        default:
            return {};
    }
}

// Run slang's hierarchical lookup, returning the chain of scope-presenting symbols traversed
// (whether or not the final segment resolved). Each `LookupResult::Element` records a step
// slang took as it descended the path, so we get the partial chain for free even when a
// trailing segment doesn't exist (e.g. "top.foo[0].missing" yields [top, foo, foo[0]]).
std::vector<const slang::ast::Symbol*> lookupScopeChain(slang::ast::Compilation& compilation,
                                                        std::string_view hierPath) {
    slang::ast::LookupResult lookup;
    slang::ast::ASTContext context(compilation.getRoot(), ast::LookupLocation::max);
    slang::ast::Lookup::name(compilation.parseName(hierPath), context,
                             ast::LookupFlags::AllowUnnamedGenerate, lookup);

    std::vector<const slang::ast::Symbol*> chain;
    auto push = [&](const slang::ast::Symbol* sym) {
        if (!sym || !isHierarchicalScope(*sym)) {
            return;
        }
        if (!chain.empty() && chain.back() == sym) {
            return;
        }
        chain.push_back(sym);
    };

    for (const auto& element : lookup.path) {
        push(element.symbol);
    }
    push(lookup.found);
    return chain;
}

std::optional<std::vector<hier::HierItem_t>> tryGetScopeChildren(
    slang::ast::Compilation& compilation, const SourceManager& sourceManager,
    std::string_view hierPath) {
    if (hierPath.empty()) {
        std::vector<hier::HierItem_t> result;
        auto& root = compilation.getRoot();
        for (auto& inst : root.topInstances) {
            INFO("Adding top instance {}", inst->name);
            hier::handleInstance(result, *inst, sourceManager, true);
        }
        for (auto& pkg : compilation.getPackages()) {
            hier::handlePackage(result, *pkg, sourceManager);
        }
        return result;
    }

    if (auto* sym = lookupHierSymbol(compilation, hierPath)) {
        if (isHierarchicalScope(*sym)) {
            return getChildrenForScopeSymbol(*sym, sourceManager);
        }
        ERROR("Unknown symbol kind for getScope: {}", toString(sym->kind));
        return std::nullopt;
    }

    if (auto* pkg = compilation.getPackage(hierPath)) {
        return hier::getScopeChildren(*pkg, sourceManager);
    }

    ERROR("Failed to find symbol at path {}", hierPath);
    return std::nullopt;
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

// Resolve through the owning document so edits use current locations without rebuilding others.
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
    m_options(std::move(options)), m_top(std::move(top)), m_sourceManager(sourceManager),
    m_client(client) {

    m_documents.reserve(documents.size());
    for (auto& doc : documents) {
        m_documents.emplace(doc->getPath(), std::move(doc));
    }

    if (m_top) {
        m_options.insertOrGet<slang::ast::CompilationOptions>().topModules = {*m_top};
    }
    indexModuleDocs();
    refresh();
}

void ServerCompilation::indexModuleDocs() {
    m_moduleToDoc.clear();
    for (const auto& [path, doc] : m_documents) {
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
    m_analysis = std::make_shared<ServerCompilationAnalysis>(m_documents, m_options,
                                                             m_sourceManager);
    m_hierarchySearchItems.clear();
    m_hierarchySearchIndexed = false;
    syncActiveInstances();
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
        if (auto activeInstance = getActiveInstanceSelection(instSet.declName)) {
            instSet.inst = std::move(*activeInstance);
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

std::optional<hier::QualifiedInstance> ServerCompilation::getActiveInstanceSelection(
    std::string_view moduleName) const {
    auto activeIt = m_activeInstancesByModule.find(std::string(moduleName));
    if (activeIt == m_activeInstancesByModule.end()) {
        return std::nullopt;
    }
    return activeIt->second;
}

const slang::ast::InstanceSymbol* ServerCompilation::getActiveInstanceSymbol(
    std::string_view moduleName) const {
    auto it = m_analysis->instances.moduleToInstances.find(std::string(moduleName));
    if (it == m_analysis->instances.moduleToInstances.end()) {
        return nullptr;
    }

    if (auto activeInstance = getActiveInstanceSelection(moduleName)) {
        for (const auto* instance : it->second) {
            if (instance->getHierarchicalPath() == activeInstance->instPath)
                return instance;
        }
    }

    if (!it->second.empty()) {
        return choosePreferredInstance(it->second);
    }

    return nullptr;
}

ActiveDesignContext ServerCompilation::createActiveDesignContext(
    std::span<const std::string_view> definitionNames) const {
    ActiveDesignContext result(m_analysis);
    for (auto name : definitionNames) {
        if (auto* instance = getActiveInstanceSymbol(name))
            result.bindInstance(*instance);
    }
    return result;
}

std::optional<ActiveGenerateLoop> ServerCompilation::getActiveGenerateLoop(
    std::string_view moduleName, const syntax::LoopGenerateSyntax& loop) const {
    auto* activeInstance = getActiveInstanceSymbol(moduleName);
    if (!activeInstance)
        return std::nullopt;

    std::vector<const ast::GenerateBlockArraySymbol*> arrays;
    collectGenerateArrays(activeInstance->body, loop, arrays);
    if (arrays.empty())
        return std::nullopt;

    auto* array = chooseGenerateArray(arrays, m_activeGenerateScopesByArray);
    if (!array || array->entries.empty())
        return std::nullopt;

    ActiveGenerateLoop result;
    result.iterationPaths.reserve(array->entries.size());
    auto selected = m_activeGenerateScopesByArray.find(array->getHierarchicalPath());
    for (auto* entry : array->entries) {
        auto path = entry->getHierarchicalPath();
        result.iterationPaths.push_back(path);
        if (selected != m_activeGenerateScopesByArray.end() && selected->second == path)
            result.activePath = path;
    }
    if (result.activePath.empty())
        result.activePath = result.iterationPaths.front();
    return result;
}

std::optional<std::string> ServerCompilation::getPreferredSymbolPath(std::string_view name) const {
    auto& compilation = m_analysis->compilation;
    auto& root = compilation.getRoot();

    if (auto def = compilation.tryGetDefinition(name, root); def.definition) {
        auto loc = m_sourceManager.getFullyOriginalLoc(def.definition->location);
        auto fullPath = m_sourceManager.getFullPath(loc.buffer());
        if (!fullPath.empty()) {
            return fullPath.string();
        }
    }

    if (auto pkg = compilation.getPackage(name)) {
        auto syntax = pkg->getSyntax();
        if (syntax) {
            auto fullPath = m_sourceManager.getFullPath(
                m_sourceManager.getFullyOriginalLoc(syntax->sourceRange().start()).buffer());
            if (!fullPath.empty()) {
                return fullPath.string();
            }
        }
    }

    return std::nullopt;
}

bool ServerCompilation::setActiveInstance(const std::string& hierPath) {
    auto* sym = lookupHierSymbol(m_analysis->compilation, hierPath);
    if (!sym) {
        return false;
    }

    setActiveGenerateScopes(*sym, m_activeGenerateScopesByArray);

    if (auto* port = sym->as_if<ast::InterfacePortSymbol>()) {
        if (auto* enclosing = getEnclosingInstance(*port)) {
            setActiveInstanceChain(*enclosing, m_activeInstancesByModule, m_sourceManager);
        }
        auto connection = resolveInterfaceConnection(*port);
        for (auto* connected : connection.sourcePath) {
            setActiveGenerateScopes(*connected, m_activeGenerateScopesByArray);
            if (auto* connectedPort = connected->as_if<ast::InterfacePortSymbol>()) {
                if (auto* enclosing = getEnclosingInstance(*connectedPort)) {
                    setActiveInstanceChain(*enclosing, m_activeInstancesByModule, m_sourceManager);
                }
            }
        }
        if (connection.resolvedEndpoint &&
            std::ranges::find(connection.sourcePath, connection.resolvedEndpoint) ==
                connection.sourcePath.end()) {
            setActiveGenerateScopes(*connection.resolvedEndpoint, m_activeGenerateScopesByArray);
        }
        sym = connection.resolvedEndpoint;
    }

    if (auto* target = sym ? sym->as_if<ast::InstanceSymbol>() : nullptr) {
        setActiveInstanceChain(*target, m_activeInstancesByModule, m_sourceManager);
        return true;
    }

    if (!sym)
        return false;
    if (auto* enclosing = getEnclosingInstance(*sym)) {
        setActiveInstanceChain(*enclosing, m_activeInstancesByModule, m_sourceManager);
        return true;
    }
    return false;
}

std::optional<hier::QualifiedInstance> ServerCompilation::getActiveInstance(
    const std::string& moduleName) {
    auto& byModule = m_analysis->instances.moduleToInstances;
    if (byModule.find(moduleName) == byModule.end()) {
        return std::nullopt;
    }
    // Prefer the stored path verbatim, even if the latest recompile dropped the
    // corresponding instance symbol; otherwise fall back to the preferred instance.
    if (auto activeInstance = getActiveInstanceSelection(moduleName)) {
        return activeInstance;
    }
    if (auto* inst = getActiveInstanceSymbol(moduleName)) {
        return hier::toQualifiedInstance(*inst, m_sourceManager);
    }
    return std::nullopt;
}

void ServerCompilation::syncActiveInstances() {
    std::unordered_map<std::string, hier::QualifiedInstance> nextActiveInstances;
    for (const auto& [moduleName, instances] : m_analysis->instances.moduleToInstances) {
        if (instances.empty()) {
            continue;
        }

        if (auto activeInstance = getActiveInstanceSelection(moduleName)) {
            auto stillExists = std::ranges::any_of(instances, [&](const auto* instance) {
                return instance->getHierarchicalPath() == activeInstance->instPath;
            });
            if (stillExists) {
                nextActiveInstances[moduleName] = std::move(*activeInstance);
                continue;
            }
        }

        if (instances.size() == 1) {
            nextActiveInstances[moduleName] = hier::toQualifiedInstance(*instances[0],
                                                                        m_sourceManager);
        }
    }

    m_activeInstancesByModule = std::move(nextActiveInstances);
}

std::vector<hier::HierItem_t> ServerCompilation::getScope(const std::string& hierPath) {
    auto scopeChildren = tryGetScopeChildren(m_analysis->compilation, m_sourceManager, hierPath);
    return scopeChildren.value_or(std::vector<hier::HierItem_t>{});
}

std::vector<hier::ScopeStep> ServerCompilation::getScopes(const std::string& hierPath,
                                                          const lsp::RequestContext& ctx) {
    std::vector<hier::ScopeStep> result;
    auto& compilation = m_analysis->compilation;

    ctx.throwIfCancelled("before resolving root hierarchy scope");
    auto rootChildren = tryGetScopeChildren(compilation, m_sourceManager, std::string_view{});
    if (!rootChildren) {
        return result;
    }
    result.push_back({.path = "", .children = std::move(*rootChildren)});

    if (hierPath.empty()) {
        return result;
    }

    for (const auto* sym : lookupScopeChain(compilation, hierPath)) {
        ctx.throwIfCancelled("while resolving hierarchy scopes");
        result.push_back({
            .path = sym->getHierarchicalPath(),
            .children = getChildrenForScopeSymbol(*sym, m_sourceManager),
        });
    }

    return result;
}

hier::HierarchySearchResult ServerCompilation::searchHierarchy(const std::string& query) {
    constexpr size_t maxResults = 100;
    hier::HierarchySearchResult result{};

    auto& compilation = m_analysis->compilation;

    if (!m_hierarchySearchIndexed) {
        auto roots = tryGetScopeChildren(compilation, m_sourceManager, std::string_view{});
        auto collect = [&](auto&& self, const std::vector<hier::HierItem_t>& items,
                           std::string_view parentPath, bool concatenateNames,
                           std::string_view containerName) -> void {
            for (const auto& item : items) {
                rfl::visit(
                    [&](const auto& entry) {
                        using Entry = std::decay_t<decltype(entry)>;
                        std::string path;
                        if (parentPath.empty()) {
                            path = entry.instName;
                        }
                        else {
                            path = fmt::format("{}{}{}", parentPath, concatenateNames ? "" : ".",
                                               entry.instName);
                        }

                        auto labelStart = path.rfind('.');
                        auto label = labelStart == std::string::npos ? path
                                                                     : path.substr(labelStart + 1);
                        std::optional<std::string> description;
                        if constexpr (std::is_same_v<Entry, hier::Var>) {
                            description = entry.type;
                            if (entry.value)
                                *description += fmt::format(" = {}", *entry.value);
                        }
                        else if constexpr (std::is_same_v<Entry, hier::Scope>) {
                            description = entry.type.value_or("scope");
                        }
                        else {
                            description = entry.declName;
                        }

                        m_hierarchySearchItems.push_back({
                            .name = std::move(label),
                            .path = path,
                            .kind = entry.kind,
                            .description = std::move(description),
                            .containerName = containerName.empty()
                                                 ? std::nullopt
                                                 : std::optional(std::string(containerName)),
                        });

                        if constexpr (std::is_same_v<Entry, hier::Scope>) {
                            std::string childContainerName(containerName);
                            if (entry.kind == hier::SlangKind::InterfacePort ||
                                entry.kind == hier::SlangKind::InterfacePortArray) {
                                if (entry.type)
                                    childContainerName = *entry.type;
                            }
                            else {
                                if (!childContainerName.empty() &&
                                    !entry.instName.starts_with("[")) {
                                    childContainerName += ".";
                                }
                                childContainerName += entry.instName;
                            }
                            self(self, entry.children, path,
                                 entry.kind == hier::SlangKind::ScopeArray ||
                                     entry.kind == hier::SlangKind::InterfacePortArray,
                                 childContainerName);
                        }
                        else if constexpr (std::is_same_v<Entry, hier::Instance>) {
                            if (!entry.children.empty()) {
                                self(self, entry.children, path,
                                     entry.kind == hier::SlangKind::InstanceArray, entry.declName);
                            }
                            else if (entry.hasChildren) {
                                if (auto* symbol = lookupHierSymbol(compilation, path)) {
                                    auto children = getChildrenForScopeSymbol(*symbol,
                                                                              m_sourceManager);
                                    self(self, children, path, false, entry.declName);
                                }
                            }
                        }
                    },
                    item);
            }
        };
        if (roots)
            collect(collect, *roots, std::string_view{}, false, std::string_view{});
        m_hierarchySearchIndexed = true;
    }

    auto lowercase = [](std::string_view text) {
        std::string lowered;
        lowered.reserve(text.size());
        for (char c : text)
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return lowered;
    };
    auto lowerQuery = lowercase(query);
    auto fuzzyMatches = [](std::string_view needle, std::string_view haystack) {
        auto next = needle.begin();
        for (char c : haystack) {
            if (next != needle.end() && *next == c)
                ++next;
        }
        return next == needle.end();
    };

    struct Match {
        const hier::HierarchySearchItem* item;
        int rank;
    };
    std::vector<Match> matches;
    for (const auto& item : m_hierarchySearchItems) {
        auto lowerLabel = lowercase(item.name);
        auto lowerPath = lowercase(item.path);
        int rank;
        if (lowerQuery.empty())
            rank = 0;
        else if (lowerLabel == lowerQuery)
            rank = 0;
        else if (lowerLabel.starts_with(lowerQuery))
            rank = 1;
        else if (lowerLabel.find(lowerQuery) != std::string::npos)
            rank = 2;
        else if (lowerPath.find(lowerQuery) != std::string::npos)
            rank = 3;
        else if (fuzzyMatches(lowerQuery, lowerPath))
            rank = 4;
        else
            continue;
        matches.push_back({.item = &item, .rank = rank});
    }

    auto resultCount = std::min(matches.size(), maxResults);
    std::ranges::partial_sort(
        matches, matches.begin() + resultCount, [](const Match& left, const Match& right) {
            return std::tuple(left.rank, left.item->path.size(), left.item->path) <
                   std::tuple(right.rank, right.item->path.size(), right.item->path);
        });
    result.totalResults = matches.size();
    result.matches.reserve(resultCount);
    for (const auto& match : matches | std::views::take(resultCount))
        result.matches.push_back(*match.item);
    return result;
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
    // remainder against that module's *shallow* compilation (which rebuilds per-edit).
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
            // Build a path rooted at the module name in its own shallow comp:
            //   "<moduleName>.<remainder>"
            std::string remainder = hierPath.substr(dot + 1);
            auto modulePath = fmt::format("{}.{}", freshDef->name, remainder);
            auto it = m_documents.find(
                m_sourceManager.getFullPath(freshDef->location.buffer()).string());
            if (it == m_documents.end()) {
                break;
            }
            auto& shallowComp = *it->second->getAnalysis()->getCompilation();
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

    // Instances: prefer the instantiation site, fall back to the definition.
    std::optional<lsp::ShowDocumentParams> params;
    if (auto* inst = sym->as_if<slang::ast::InstanceSymbol>()) {
        auto* syntax = inst->getSyntax();
        if (!syntax) {
            syntax = inst->getDefinition().getSyntax();
        }
        if (!syntax) {
            return std::nullopt;
        }
        params = makeShowDocumentParams(syntax->sourceRange(), m_sourceManager);
    }
    else {
        // Non-instance: try .location first. Interface ports and other parameterized
        // symbols often have synthesized locations though, so fall back to the syntax
        // range when location-based fails (makeShowDocumentParams already walks
        // macro expansions / fully-original locs for both paths).
        params = makeShowDocumentParams(sym->location, sym->name.length(), m_sourceManager);
        if (!params || !params->selection) {
            if (auto* syntax = sym->getSyntax()) {
                params = makeShowDocumentParams(syntax->sourceRange(), m_sourceManager);
            }
        }
    }

    if (!params || !params->selection) {
        return std::nullopt;
    }
    return lsp::Location{.uri = params->uri, .range = *params->selection};
}

std::vector<std::string> ServerCompilation::getInstances(
    const lsp::TextDocumentPositionParams& params) {
    auto path = std::string(params.textDocument.uri.getPath());
    auto it = m_documents.find(path);
    if (it == m_documents.end()) {
        ERROR("Unknown doc: {}", path);
        return {};
    }
    auto& doc = it->second;
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
