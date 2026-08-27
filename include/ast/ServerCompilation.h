//------------------------------------------------------------------------------
// ServerCompilation.h
// Server compilation class that tracks document dependencies
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "HierarchicalView.h"
#include "ServerCompilationAnalysis.h"
#include "document/SlangDoc.h"
#include "lsp/LspClient.h"
#include "util/Converters.h"
#include <filesystem>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/util/Bag.h"

namespace server {
using namespace slang;

/// @brief A single endpoint of a driver/load cone.
struct ConeEntry {
    // The hierarchical RTL path of the signal
    std::string path;
    // The declaration location of the signal
    lsp::Location location;
};

/// @brief A server compilation that is set via top level or a .f file.
/// Manages the specification of the compilation, as well as the analysis state that gets refreshed
/// on file saves.
///
/// More state here is planned, like the currently focused instance, and a mapping
/// of modules to instance for enriched data like inlayed parameter or signal values.
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

    /// Get instances by module; Used for the 'instances' view. Only contains the module name and
    /// count
    std::vector<hier::InstanceSet> getScopesByModule();

    /// Get instances of a specific module
    const std::vector<const slang::ast::InstanceSymbol*>& getInstancesOfModule(
        const std::string& moduleName) const;

    /// Retrun the children of the scope at the given hierarchical path
    std::vector<hier::HierItem_t> getScope(const std::string& hierPath);

    /// Return root-to-focus scope steps with populated child lists for each path segment.
    std::vector<hier::ScopeStep> getScopes(const std::string& hierPath);

    /// Resolve the source location for a hierarchical instance path.
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
    // Used when going from hierPath -> compilation symbol -> shallow compilation symbol so
    // server-side opens use locations from the up-to-date open document when possible.
    const slang::ast::Symbol* toShallowSymbol(const slang::ast::Symbol& symbol) const;

    /// Build m_moduleToDoc from the declared symbols of each document's syntax tree.
    void indexModuleDocs();

    /// The Slang documents this compilation is based on
    std::vector<std::shared_ptr<SlangDoc>> m_documents;

    /// Maps a declared module (/interface/program/class) name to the document that declares it.
    /// Lets toShallowSymbol() find the owning shallow compilation without scanning all documents.
    /// Rebuilt in the constructor since m_documents is fixed for this compilation's lifetime.
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

    /// The analysis state, rebuilt on refresh()
    std::unique_ptr<ServerCompilationAnalysis> m_analysis;
};

} // namespace server
