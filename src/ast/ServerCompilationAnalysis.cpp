//------------------------------------------------------------------------------
// ServerCompilationAnalysis.cpp
// Implementation of server compilation analysis class
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "ast/ServerCompilationAnalysis.h"

#include "util/Logging.h"
#include <unordered_set>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/text/SourceManager.h"

namespace server {

ServerCompilationAnalysis::ServerCompilationAnalysis(
    std::vector<std::shared_ptr<SlangDoc>>& documents, Bag& options, SourceManager& sourceManager) :
    compilation(options),
    m_analysisOptions(options.getOrDefault<slang::analysis::AnalysisOptions>()) {
    std::vector<BufferID> bufferIds;
    std::unordered_set<BufferID> seenBuffers;

    for (auto& doc : documents) {
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

void ServerCompilationAnalysis::issueDiagnosticsTo(slang::DiagnosticEngine& diagEngine) {
    // Semantic diagnostics from compilation
    for (auto& diag : compilation.getSemanticDiagnostics()) {
        diagEngine.issue(diag);
    }

    // Driver analysis diagnostics (multi-driven, unused, etc)
    // Use stored options with numThreads=1 to avoid persistent thread pool
    slang::analysis::AnalysisManager driverAnalysis(m_analysisOptions);
    compilation.freeze();
    driverAnalysis.analyze(compilation);
    compilation.unfreeze();
    INFO("Driver analysis found {} diagnostics", driverAnalysis.getDiagnostics().size());
    for (auto& diag : driverAnalysis.getDiagnostics()) {
        diagEngine.issue(diag);
    }
}

} // namespace server
