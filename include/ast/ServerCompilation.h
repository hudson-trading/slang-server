//------------------------------------------------------------------------------
// ServerCompilation.h
// Server compilation class that tracks document dependencies
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "ConeTracer.h"
#include "HierarchicalView.h"
#include "InstanceIndexer.h"
#include "ReferenceIndexer.h"
#include "document/SlangDoc.h"
#include "util/Converters.h"
#include <filesystem>
#include <memory>
#include <vector>

#include "slang/util/Bag.h"

namespace server {
using namespace slang;

/// @brief A server compilation that is a thin wrapper around a slang compilation, and can be
/// updated as changes come in. Updates to the documents trigger recompilation, but not a reparse of
/// all the trees. Trees can be added if new modules are added, but will not be removed.
class ServerCompilation {
public:
    std::unique_ptr<slang::ast::Compilation> comp;
    /// @brief Constructs a new ServerCompilation instance
    /// @param documents Vector of weak pointers to SlangDocuments this compilation is based on
    /// @param options Copy of the options bag for this compilation
    ServerCompilation(std::vector<std::shared_ptr<SlangDoc>> documents, Bag options,
                      SourceManager& sourceManager);

    ~ServerCompilation() = default;

    /// Update the compilation based by requesting all syntax trees from the documents
    void refresh();

    InstanceIndexer& getInstances() { return m_instances; }

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

    /// Populate incoming / outgoing (drivers / loads) call hierarchy LSP responses
    template<typename P, typename R>
    std::optional<std::vector<R>> getCallHierarchyCalls(const P& params) {
        static constexpr bool isDriver =
            std::is_same<P, lsp::CallHierarchyIncomingCallsParams>::value;
        auto cone = getCone<isDriver>(params.item.name);

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
        auto cone = getCone<isDrivers>(path);
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

    /// Index of buffer -> definitions and definition -> instances given a compilation. Used for
    /// navigating a compilation
    InstanceIndexer m_instances;

    /// Index of value symbol -> uses (e.g. processes or continuous assignments)
    std::optional<ReferenceIndexer> m_references = std::nullopt;

    /// Reference to the source manager for this compilation,
    /// owned by the driver
    SourceManager& m_sourceManager;

    template<bool isDrivers>
    struct ConeSelector;

    /// Get cone leaves (drivers or loads depending on template parameter) for a given RTL path
    template<bool isDrivers>
    std::set<ConeLeaf> getCone(const std::string& path) {
        using ConeSelector_t = typename ConeSelector<isDrivers>::type;
        slang::ast::LookupResult result;
        slang::ast::ASTContext context(comp->getRoot(), slang::ast::LookupLocation::max);
        slang::ast::Lookup::name(comp->parseName(path), context, slang::ast::LookupFlags::None,
                                 result);
        if (!result.found) {
            throw std::runtime_error(
                fmt::format("Could not find path in compiled design: {}", path));
        }

        if (!m_references) {
            m_references.emplace();
            m_references->reset(&comp->getRoot());
        }

        auto it = m_references->symbolToUses.find(
            ConeLeaf::concreteSymbol(result.found)->as_if<slang::ast::ValueSymbol>());
        if (it == m_references->symbolToUses.end()) {
            throw std::runtime_error(fmt::format("Could not find reference to: {}", path));
        }

        ConeSelector_t coneTracer(result.found);
        for (const auto symbol : it->second) {
            std::cerr << "CONE: " << symbol->getSyntax()->toString()
                      << std::endl; // NCOMMIT -- remove
            symbol->visit(coneTracer);
        }

        return coneTracer.getLeaves();
    }
};

template<>
struct ServerCompilation::ConeSelector<true> {
    using type = DriversTracer;
};

template<>
struct ServerCompilation::ConeSelector<false> {
    using type = LoadsTracer;
};

} // namespace server
