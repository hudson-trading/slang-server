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
#include "util/Converters.h"
#include <filesystem>
#include <memory>
#include <vector>

#include "slang/util/Bag.h"

namespace server {
using namespace slang;

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
    /// @param top Optional top module name (owned by this compilation)
    ServerCompilation(std::vector<std::shared_ptr<SlangDoc>> documents, Bag options,
                      SourceManager& sourceManager, std::optional<std::string> top = std::nullopt);

    ~ServerCompilation() = default;

    /// Update the compilation based by requesting all syntax trees from the documents
    void refresh();

    InstanceIndexer& getInstances() { return m_analysis->instances; }

    /// Get instances by module; Used for the 'instances' view. Only contains the module name and
    /// count
    std::vector<hier::InstanceSet> getScopesByModule();

    /// Get instances of a specific module
    std::vector<hier::QualifiedInstance> getInstancesOfModule(const std::string& moduleName);

    /// Retrun the children of the scope at the given hierarchical path
    std::vector<hier::HierItem_t> getScope(const std::string& hierPath);

    /// Return instances for given doc position
    std::vector<std::string> getInstances(const lsp::TextDocumentPositionParams&);

    /// Prepare cone tracing using LSP call hierarchy API
    std::optional<std::vector<lsp::CallHierarchyItem>> getDocPrepareCallHierarchy(
        const lsp::CallHierarchyPrepareParams& params);

    /// Deduce WCP variable vs scope
    // TODO -- remove once not needed for cone tracing
    bool isWcpVariable(const std::string& path);

    /// Get document and position params for a given RTL path
    std::optional<lsp::ShowDocumentParams> getHierDocParams(const std::string& path);

    /// Issue all semantic diagnostics from the compilation to the diagnostic engine
    void issueDiagnosticsTo(slang::DiagnosticEngine& diagEngine);

    /// Populate incoming / outgoing (drivers / loads) call hierarchy LSP responses
    template<typename P, typename R>
    std::optional<std::vector<R>> getCallHierarchyCalls(const P& params) {
        static constexpr bool isDriver =
            std::is_same<P, lsp::CallHierarchyIncomingCallsParams>::value;
        auto cone = m_analysis->getCone<isDriver>(params.item.name);

        std::vector<R> result;
        for (const auto leaf : cone) {
            std::string hier = leaf.getHierarchicalPath();
            auto range = leaf.getSourceRange();
            if (range.start().valid()) {
                auto fullPath = std::filesystem::absolute(
                    m_sourceManager.getFileName(range.start()));
                // only different by to / from field name . . . sigh
                if constexpr (std::is_same_v<lsp::CallHierarchyIncomingCallsParams, P>) {
                    result.push_back({.from = {.name = hier, .uri = URI::fromFile(fullPath)},
                                      .fromRanges = {{toRange(range, m_sourceManager)}}});
                }
                else {
                    result.push_back({.to = {.name = hier, .uri = URI::fromFile(fullPath)},
                                      .fromRanges = {{toRange(range, m_sourceManager)}}});
                }
            }
        }

        return std::optional(result);
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
    /// The Slang documents this compilation is based on
    std::vector<std::shared_ptr<SlangDoc>> m_documents;

    /// Copy of compilation options
    Bag m_options;

    /// Owned storage for top module name, used with setTopLevel
    /// CompilationOptions::topModules uses string_view, so we need to own the string here
    std::optional<std::string> m_top;

    /// Reference to the source manager for this compilation,
    /// owned by the driver
    SourceManager& m_sourceManager;

    /// The analysis state, rebuilt on refresh()
    std::unique_ptr<ServerCompilationAnalysis> m_analysis;
};

} // namespace server
