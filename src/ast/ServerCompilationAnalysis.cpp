//------------------------------------------------------------------------------
// ServerCompilationAnalysis.cpp
// Implementation of server compilation analysis class
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "ast/ServerCompilationAnalysis.h"

#include "util/Logging.h"
#include <algorithm>
#include <unordered_set>

#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/Compilation.h"
#include "slang/text/SourceManager.h"

namespace server {

ServerCompilationAnalysis::ServerCompilationAnalysis(
    const std::unordered_map<std::string, std::shared_ptr<SlangDoc>>& documents, Bag& options,
    SourceManager& sourceManager) :
    compilation(options),
    m_analysisOptions(options.getOrDefault<slang::analysis::AnalysisOptions>()) {
    std::vector<BufferID> bufferIds;
    std::unordered_set<BufferID> seenBuffers;

    for (auto& [_, doc] : documents) {
        auto tree = doc->getSyntaxTree();
        compilation.addSyntaxTree(tree);

        for (auto bufferId : tree->getSourceBufferIds()) {
            if (seenBuffers.insert(bufferId).second)
                bufferIds.push_back(bufferId);
        }
    }

    // Retain buffer data to prevent deallocation while this compilation exists
    m_retainedBuffers = sourceManager.retainBuffers(bufferIds);

    // reset and rebuild indexed info
    auto& root = compilation.getRoot();
    instances.reset(&root);

    // symbol references are not indexed until cone tracing is requested
    m_references.reset();
}

slang::analysis::AnalysisManager& ServerCompilationAnalysis::getAnalysisManager() {
    if (!m_driverAnalysis) {
        m_driverAnalysis = std::make_unique<slang::analysis::AnalysisManager>(m_analysisOptions);
        compilation.freeze();
        m_driverAnalysis->analyze(compilation);
        compilation.unfreeze();
    }
    return *m_driverAnalysis;
}

void ServerCompilationAnalysis::issueDiagnosticsTo(slang::DiagnosticEngine& diagEngine) {
    // Semantic diagnostics from compilation
    for (auto& diag : compilation.getSemanticDiagnostics()) {
        diagEngine.issue(diag);
    }

    // Driver analysis diagnostics (multi-driven, unused, etc)
    auto& driverAnalysis = getAnalysisManager();
    INFO("Driver analysis found {} diagnostics", driverAnalysis.getDiagnostics().size());
    for (auto& diag : driverAnalysis.getDiagnostics()) {
        diagEngine.issue(diag);
    }
}

std::vector<const slang::analysis::ValueDriver*> ServerCompilationAnalysis::getDrivers(
    const slang::ast::ValueSymbol& symbol) {
    if (!m_analysisQueries) {
        auto& manager = getAnalysisManager();
        m_analysisQueries = std::make_unique<slang::analysis::AnalysisQueries>(compilation,
                                                                               manager);
    }
    return m_analysisQueries->getDrivers(symbol);
}

const slang::ast::Symbol& ServerCompilationAnalysis::lookupConeSymbol(const std::string& path) {
    slang::ast::LookupResult result;
    slang::ast::ASTContext context(compilation.getRoot(), slang::ast::LookupLocation::max);
    slang::ast::Lookup::name(compilation.parseName(path), context, slang::ast::LookupFlags::None,
                             result);
    if (!result.found) {
        throw std::runtime_error(fmt::format("Could not find path in compiled design: {}", path));
    }

    return *result.found;
}

std::set<ConeLeaf> ServerCompilationAnalysis::getDriverCone(const std::string& path) {
    const auto& symbol = lookupConeSymbol(path);
    const slang::ast::ValueSymbol* lookupSymbol = symbol.as_if<slang::ast::ValueSymbol>();
    const slang::ast::ValueSymbol* resultSymbol =
        ConeLeaf::concreteSymbol(&symbol)->as_if<slang::ast::ValueSymbol>();
    if (!lookupSymbol || !resultSymbol) {
        throw std::runtime_error(fmt::format("Path is not a value symbol: {}", path));
    }

    auto drivers = getDrivers(*resultSymbol);
    if (lookupSymbol != resultSymbol) {
        for (auto* driver : getDrivers(*lookupSymbol)) {
            if (std::ranges::find(drivers, driver) == drivers.end()) {
                drivers.push_back(driver);
            }
        }
    }
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

    return tracer.getLeaves();
}

std::set<ConeLeaf> ServerCompilationAnalysis::getLoadCone(const std::string& path) {
    const auto& symbol = lookupConeSymbol(path);
    if (!m_references) {
        m_references.emplace();
        m_references->reset(&compilation.getRoot());
    }

    auto it = m_references->symbolToUses.find(
        ConeLeaf::concreteSymbol(&symbol)->as_if<slang::ast::ValueSymbol>());
    if (it == m_references->symbolToUses.end()) {
        throw std::runtime_error(fmt::format("Could not find reference to: {}", path));
    }

    LoadsTracer coneTracer(&symbol);
    for (const auto* use : it->second) {
        use->visit(coneTracer);
    }

    return coneTracer.getLeaves();
}

} // namespace server
