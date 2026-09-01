//------------------------------------------------------------------------------
// ServerCompilation.h
// Server compilation class that tracks document dependencies
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "ActiveDesignContext.h"
#include "HierarchicalView.h"
#include "ServerCompilationAnalysis.h"
#include "document/SlangDoc.h"
#include "lsp/LspClient.h"
#include "lsp/RequestContext.h"
#include "util/Converters.h"
#include <filesystem>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/util/Bag.h"

namespace server {
using namespace slang;

struct ActiveGenerateLoop {
    std::string activePath;
    std::vector<std::string> iterationPaths;
};

/// @brief A single endpoint of a driver/load cone.
struct ConeEntry {
    // The hierarchical RTL path of the signal
    std::string path;
    // The declaration location of the signal
    lsp::Location location;
};

/// @brief A server compilation that is set via top level or a .f file.
/// Manages the specification of the compilation, as well as the analysis state that gets
/// refreshed on file saves.
class ServerCompilation {
public:
    /// @brief Constructs a new ServerCompilation instance
    /// @param documents Vector of weak pointers to SlangDocuments this compilation is based on
    /// @param options Copy of the options bag for this compilation
    /// @param client LSP client used for user-facing notifications
    /// @param top Optional top module name (owned by this compilation)
    ServerCompilation(std::vector<std::shared_ptr<SlangDoc>> documents, Bag options,
                      SourceManager& sourceManager, lsp::LspClient& client,
                      std::optional<std::string> top = std::nullopt);

    ~ServerCompilation() = default;

    /// Update the compilation based by requesting all syntax trees from the documents
    void refresh();

    InstanceIndexer& getInstances() { return m_analysis->instances; }

    /// Get instances by module; Used for the 'instances' view. Includes declaration metadata.
    std::vector<hier::InstanceSet> getScopesByModule();

    /// Get instances of a specific module
    const std::vector<const slang::ast::InstanceSymbol*>& getInstancesOfModule(
        const std::string& moduleName) const;

    /// Return the file path that the active compilation resolved for a symbol name, if any.
    std::optional<std::string> getPreferredSymbolPath(std::string_view name) const;

    /// Set the active instance and generate scope reached by `hierPath`. The path may be
    /// canonical (`top.bus8`) or an alias that resolves through an interface port
    /// (`top.tap.data_bus`); slang's lookup resolves both. Returns false if the path isn't
    /// contained by an instance.
    bool setActiveInstance(const std::string& hierPath);

    /// Return the active instance for a module, falling back to a deterministic default.
    std::optional<hier::QualifiedInstance> getActiveInstance(const std::string& moduleName);

    /// Return the elaborated active instance symbol for a module, if any.
    const slang::ast::InstanceSymbol* getActiveInstanceSymbol(std::string_view moduleName) const;

    /// Capture the selected full-design instances needed by a shallow compilation.
    ActiveDesignContext createActiveDesignContext(
        std::span<const std::string_view> definitionNames) const;

    /// Return the active iteration and available choices for a generate loop in a module.
    std::optional<ActiveGenerateLoop> getActiveGenerateLoop(
        std::string_view moduleName, const slang::syntax::LoopGenerateSyntax& loop) const;

    /// Retrun the children of the scope at the given hierarchical path
    std::vector<hier::HierItem_t> getScope(const std::string& hierPath);

    /// Return root-to-focus scope steps with populated child lists for each path segment.
    std::vector<hier::ScopeStep> getScopes(const std::string& hierPath,
                                           const lsp::RequestContext& ctx = {});

    /// Search the elaborated hierarchy for instances, scopes, ports, parameters, and signals.
    hier::HierarchySearchResult searchHierarchy(const std::string& query);

    /// Resolve the document location for a hierarchical path on-demand.
    std::optional<lsp::Location> getHierLocation(const std::string& hierPath);

    /// Return instances for given doc position
    std::vector<std::string> getInstances(const lsp::TextDocumentPositionParams&);

    /// Prepare cone tracing using LSP call hierarchy API
    std::vector<lsp::CallHierarchyItem> getDocPrepareCallHierarchy(
        const lsp::CallHierarchyPrepareParams& params);

    /// Deduce WCP variable vs scope
    // TODO -- remove once not needed for cone tracing
    bool isWcpVariable(const std::string& path);

    /// Get document and position params for a given RTL path
    std::optional<lsp::ShowDocumentParams> getHierDocParams(const std::string& path);

    /// Issue all semantic diagnostics from the compilation to the diagnostic engine
    void issueDiagnosticsTo(slang::DiagnosticEngine& diagEngine);

    /// Return the drivers (isDrivers=true) or loads (isDrivers=false) of the signal at the
    /// given path, each with the source location where it appears. Endpoints without a valid
    /// source location are omitted.
    template<bool isDrivers>
    std::vector<ConeEntry> getConeLocations(const std::string& path) {
        auto cone = m_analysis->getCone<isDrivers>(path);
        std::vector<ConeEntry> result;
        for (const auto leaf : cone) {
            auto range = leaf.getDeclarationRange();
            if (range.start().valid()) {
                auto fullPath = std::filesystem::absolute(
                    m_sourceManager.getFileName(range.start()));
                result.push_back({.path = leaf.getHierarchicalPath(),
                                  .location = {.uri = URI::fromFile(fullPath),
                                               .range = toRange(range, m_sourceManager)}});
            }
        }
        return result;
    }

    /// Return list of RTL paths for a driver or load cone
    template<bool isDrivers>
    std::vector<std::string> getConePaths(const std::string& path) {
        auto cone = m_analysis->getCone<isDrivers>(path);
        std::vector<std::string> result;
        std::set<std::string> seen;
        for (const auto leaf : cone) {
            std::string hier = leaf.getHierarchicalPath();
            if (seen.insert(hier).second) {
                result.push_back(hier);
            }
        }

        return result;
    }

private:
    /// Return the stored active selection for a module, if any.
    std::optional<hier::QualifiedInstance> getActiveInstanceSelection(
        std::string_view moduleName) const;

    // Used when going from hierPath -> compilation symbol -> shallow compilation symbol
    // Therefore we get more up to date source locations
    const slang::ast::Symbol* toShallowSymbol(const slang::ast::Symbol& symbol) const;

    /// Build m_moduleToDoc from the declared symbols of each document's syntax tree.
    void indexModuleDocs();

    /// The Slang documents this compilation is based on, keyed by their absolute source path so
    /// path-driven lookups (LSP URIs, symbol source locations) avoid scanning every doc.
    std::unordered_map<std::string, std::shared_ptr<SlangDoc>> m_documents;

    /// Maps a declared module (/interface/program/class) name to the document that declares it.
    /// Lets toShallowSymbol() find the owning shallow compilation without scanning all
    /// documents. Rebuilt in the constructor since m_documents is fixed for this compilation's
    /// lifetime.
    std::unordered_map<std::string, std::shared_ptr<SlangDoc>> m_moduleToDoc;

    /// Copy of compilation options
    Bag m_options;

    /// Owned storage for top module name, used with setTopLevel
    /// CompilationOptions::topModules uses string_view, so we need to own the string here
    std::optional<std::string> m_top;

    /// Reference to the source manager for this compilation,
    /// owned by the driver
    SourceManager& m_sourceManager;

    /// LSP client owned by the server
    lsp::LspClient& m_client;

    /// The analysis state, rebuilt on refresh(). Shared by shallow compilations that are using it
    /// for params while we may be rebuilding a new one.
    std::shared_ptr<ServerCompilationAnalysis> m_analysis;

    /// Active instance and generate scope selection by module name for this compilation.
    std::unordered_map<std::string, hier::QualifiedInstance> m_activeInstancesByModule;

    /// Active iteration by fully qualified generate array path.
    std::unordered_map<std::string, std::string> m_activeGenerateScopesByArray;

    /// Flattened hierarchy entries, populated on the first search after each refresh.
    std::vector<hier::HierarchySearchItem> m_hierarchySearchItems;
    bool m_hierarchySearchIndexed = false;

    /// Retain only still-valid active instances after a refresh and seed unique modules.
    void syncActiveInstances();
};

} // namespace server
