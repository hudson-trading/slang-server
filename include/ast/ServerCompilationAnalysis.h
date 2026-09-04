//------------------------------------------------------------------------------
// ServerCompilationAnalysis.h
// Contains the analysis state from a refreshed compilation
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "ConeTracer.h"
#include "InstanceIndexer.h"
#include "ReferenceIndexer.h"
#include "document/SlangDoc.h"
#include <algorithm>
#include <memory>
#include <set>
#include <vector>

#include "slang/analysis/AnalysisOptions.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/util/Bag.h"

namespace server {
using namespace slang;

/// @brief Contains the analysis state surrounding a compilation, recreated on every refresh (file
/// save when compilation is set). This includes the compilation itself, references to used buffers,
/// instance indexer, etc.
class ServerCompilationAnalysis {
public:
    ServerCompilationAnalysis(std::vector<std::shared_ptr<SlangDoc>>& documents, Bag& options,
                              SourceManager& sourceManager);

    slang::ast::Compilation compilation;

    /// Index of buffer -> definitions and definition -> instances given a compilation. Used for
    /// navigating a compilation via the sidebar
    InstanceIndexer instances;

    /// Issue all semantic diagnostics from the compilation to the diagnostic engine
    void issueDiagnosticsTo(slang::DiagnosticEngine& diagEngine);

    /// Get cone leaves (drivers or loads depending on template parameter) for a given RTL path
    template<bool isDrivers>
    std::set<ConeLeaf> getCone(const std::string& path) {
        slang::ast::LookupResult result;
        slang::ast::ASTContext context(compilation.getRoot(), slang::ast::LookupLocation::max);
        slang::ast::Lookup::name(compilation.parseName(path), context,
                                 slang::ast::LookupFlags::None, result);
        if (!result.found) {
            throw std::runtime_error(
                fmt::format("Could not find path in compiled design: {}", path));
        }

        if constexpr (isDrivers) {
            return getDriverCone(path, result);
        }
        else {
            return getLoadCone(path, result);
        }
    }

private:
    std::set<ConeLeaf> getDriverCone(const std::string& path,
                                     const slang::ast::LookupResult& result) {
        if (!m_analysisManager) {
            m_analysisManager.emplace(m_analysisOptions);
            compilation.freeze();
            m_analysisManager->analyze(compilation);
            compilation.unfreeze();
        }

        const slang::ast::ValueSymbol* lookupSymbol =
            result.found->as_if<slang::ast::ValueSymbol>();
        const slang::ast::ValueSymbol* resultSymbol =
            ConeLeaf::concreteSymbol(result.found)->as_if<slang::ast::ValueSymbol>();
        if (!lookupSymbol || !resultSymbol) {
            throw std::runtime_error(fmt::format("Path is not a value symbol: {}", path));
        }

        auto drivers = m_analysisManager->getDriversForInstance(*resultSymbol);
        if (lookupSymbol != resultSymbol) {
            for (auto* driver : m_analysisManager->getDriversForInstance(*lookupSymbol)) {
                if (std::ranges::find(drivers, driver) == drivers.end()) {
                    drivers.push_back(driver);
                }
            }
        }
        if (drivers.empty()) {
            throw std::runtime_error(fmt::format("Could not find reference to: {}", path));
        }

        std::set<ConeLeaf> leaves;
        DriversTracer tracer(resultSymbol);
        for (auto* driver : drivers) {
            // slang sets the containingSymbol on an input port driver to the
            // instance body. Visit the parent InstanceSymbol instead so we
            // include the port's connection expression in the driver.
            if (driver->isInputPort()) {
                auto* body = driver->containingSymbol->as_if<ast::InstanceBodySymbol>();
                if (body && body->parentInstance) {
                    body->parentInstance->visit(tracer);
                    continue;
                }
            }

            // Side-effect drivers use the instance symbol as the containing
            // symbol, while the actual use of the symbol was in the instance
            // body. The drivers tracer does not descend from an instance
            // symbol into the instance body, to prevent a symbol that is
            // connected to an instance's output port from inheriting that
            // output's drivers within the instance body.
            if (driver->flags.has(analysis::DriverFlags::FromSideEffect)) {
                if (auto* instance = driver->containingSymbol->as_if<ast::InstanceSymbol>()) {
                    instance->body.visit(tracer);
                    continue;
                }
            }
            driver->containingSymbol->visit(tracer);
        }

        auto tracedLeaves = tracer.getLeaves();
        leaves.insert(tracedLeaves.begin(), tracedLeaves.end());
        return leaves;
    }

    std::set<ConeLeaf> getLoadCone(const std::string& path,
                                   const slang::ast::LookupResult& result) {
        if (!m_references) {
            m_references.emplace();
            m_references->reset(&compilation.getRoot());
        }

        auto it = m_references->symbolToUses.find(
            ConeLeaf::concreteSymbol(result.found)->as_if<slang::ast::ValueSymbol>());
        if (it == m_references->symbolToUses.end()) {
            throw std::runtime_error(fmt::format("Could not find reference to: {}", path));
        }

        LoadsTracer coneTracer(result.found);
        for (const auto symbol : it->second) {
            symbol->visit(coneTracer);
        }

        return coneTracer.getLeaves();
    }

    /// Retained buffer data to prevent deallocation while this compilation exists
    std::vector<std::shared_ptr<void>> m_retainedBuffers;

    /// Analysis options from the bag, used for driver analysis
    slang::analysis::AnalysisOptions m_analysisOptions;

    std::optional<slang::analysis::AnalysisManager> m_analysisManager = std::nullopt;
    /// Index of value symbol -> uses (e.g. processes or continuous assignments)
    std::optional<ReferenceIndexer> m_references = std::nullopt;
};

} // namespace server
