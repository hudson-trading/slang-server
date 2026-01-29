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
#include <memory>
#include <vector>

#include "slang/analysis/AnalysisOptions.h"
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

    template<bool isDrivers>
    struct ConeSelector;

    /// Get cone leaves (drivers or loads depending on template parameter) for a given RTL path
    template<bool isDrivers>
    std::set<ConeLeaf> getCone(const std::string& path) {
        using ConeSelector_t = typename ConeSelector<isDrivers>::type;
        slang::ast::LookupResult result;
        slang::ast::ASTContext context(compilation.getRoot(), slang::ast::LookupLocation::max);
        slang::ast::Lookup::name(compilation.parseName(path), context,
                                 slang::ast::LookupFlags::None, result);
        if (!result.found) {
            throw std::runtime_error(
                fmt::format("Could not find path in compiled design: {}", path));
        }

        if (!m_references) {
            m_references.emplace();
            m_references->reset(&compilation.getRoot());
        }

        auto it = m_references->symbolToUses.find(
            ConeLeaf::concreteSymbol(result.found)->as_if<slang::ast::ValueSymbol>());
        if (it == m_references->symbolToUses.end()) {
            throw std::runtime_error(fmt::format("Could not find reference to: {}", path));
        }

        ConeSelector_t coneTracer(result.found);
        for (const auto symbol : it->second) {
            symbol->visit(coneTracer);
        }

        return coneTracer.getLeaves();
    }

private:
    /// Retained buffer data to prevent deallocation while this compilation exists
    std::vector<std::shared_ptr<void>> m_retainedBuffers;

    /// Analysis options from the bag, used for driver analysis
    slang::analysis::AnalysisOptions m_analysisOptions;

    /// Index of value symbol -> uses (e.g. processes or continuous assignments)
    std::optional<ReferenceIndexer> m_references = std::nullopt;
};

template<>
struct ServerCompilationAnalysis::ConeSelector<true> {
    using type = DriversTracer;
};

template<>
struct ServerCompilationAnalysis::ConeSelector<false> {
    using type = LoadsTracer;
};

} // namespace server
