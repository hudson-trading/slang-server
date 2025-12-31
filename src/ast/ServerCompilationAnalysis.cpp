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

#include "slang/ast/Compilation.h"
#include "slang/text/SourceManager.h"

namespace server {

ServerCompilationAnalysis::ServerCompilationAnalysis(
    std::vector<std::shared_ptr<SlangDoc>>& documents, Bag& options, SourceManager& sourceManager) :
    compilation(options) {
    // Collect all buffer IDs from all syntax trees
    {
        std::vector<BufferID> allBuffers;
        std::unordered_set<BufferID> includedBuffers;
        for (auto& doc : documents) {
            auto tree = doc->getSyntaxTree();
            compilation.addSyntaxTree(tree);
            for (auto bufId : tree->getSourceBufferIds()) {
                allBuffers.push_back(bufId);
            }
            for (auto inc : tree->getIncludeDirectives()) {
                includedBuffers.insert(inc.buffer.id);
            }
        }
        // add to vec
        for (auto bufId : includedBuffers) {
            allBuffers.push_back(bufId);
        }
        // Retain buffer data to prevent deallocation while this compilation exists
        m_retainedBuffers = sourceManager.retainBuffers(allBuffers);
    }

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
    compilation.freeze();
    m_driverAnalysis.analyze(compilation);
    compilation.unfreeze();
    INFO("Driver analysis found {} diagnostics", m_driverAnalysis.getDiagnostics().size());
    for (auto& diag : m_driverAnalysis.getDiagnostics()) {
        diagEngine.issue(diag);
    }
}

} // namespace server
