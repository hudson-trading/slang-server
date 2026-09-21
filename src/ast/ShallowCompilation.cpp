//------------------------------------------------------------------------------
// ShallowCompilation.cpp
// Compilation state shared by a source file and its included-file analyses
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "ast/ShallowCompilation.h"

#include "ast/ServerCompilation.h"
#include "util/SlangExtensions.h"

#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/diagnostics/AnalysisDiags.h"

namespace server {
using namespace slang;

ShallowCompilation::ShallowCompilation(
    SourceManager& sourceManager, std::shared_ptr<syntax::SyntaxTree> tree, const Bag& options,
    const std::vector<std::shared_ptr<syntax::SyntaxTree>>& dependencies,
    const ServerCompilation* design) :
    m_sourceManager(sourceManager), m_tree(std::move(tree)), m_allTrees{m_tree},
    m_analysisOptions(options.getOrDefault<analysis::AnalysisOptions>()) {
    m_allTrees.insert(m_allTrees.end(), dependencies.begin(), dependencies.end());

    auto cOptions = options.getOrDefault<ast::CompilationOptions>();
    cOptions.flags |= ast::CompilationFlags::AllowTopLevelIfacePorts;
    cOptions.flags |= ast::CompilationFlags::CheckUninstantiated;
    cOptions.flags |= ast::CompilationFlags::AllowInvalidTop;
    // Check the edited module and two levels of instantiated children.
    cOptions.maxInstanceDepth = 3;
    cOptions.topModules.clear();

    m_compilation = std::make_unique<ast::Compilation>(cOptions);
    for (auto& depTree : m_allTrees)
        m_compilation->addSyntaxTree(depTree);

    if (design) {
        std::vector<std::string_view> definitionNames;
        for (auto* symbol : m_compilation->getDefinitions()) {
            if (auto* definition = symbol->as_if<ast::DefinitionSymbol>())
                definitionNames.push_back(definition->name);
        }
        m_activeDesign = design->createActiveDesignContext(definitionNames);
        m_activeDesign->applyOverrides(*m_compilation);
    }
}

bool ShallowCompilation::hasValidBuffers() const {
    for (auto& tree : m_allTrees) {
        if (!server::hasValidBuffers(m_sourceManager, tree))
            return false;
    }
    return true;
}

const Diagnostics& ShallowCompilation::getSemanticDiagnostics() {
    if (!m_editedDefinitionsElaborated) {
        auto& root = m_compilation->getRoot();
        for (auto* symbol : m_compilation->getDefinitions()) {
            auto* definition = symbol->as_if<ast::DefinitionSymbol>();
            if (!definition || definition->syntaxTree != m_tree.get())
                continue;

            auto& instance = ast::InstanceSymbol::createDefault(*m_compilation, *definition);
            instance.setParent(root);
            m_compilation->forceElaborate(instance.body);
        }
        m_editedDefinitionsElaborated = true;
    }

    return m_compilation->getSemanticDiagnostics();
}

Diagnostics ShallowCompilation::getAnalysisDiags() {
    getAnalysisManager();
    if (!m_cachedAnalysisDiags)
        return {};
    return *m_cachedAnalysisDiags;
}

const analysis::AnalysisManager* ShallowCompilation::getAnalysisManager() {
    if (m_driverAnalysis)
        return m_driverAnalysis.get();

    (void)getSemanticDiagnostics();
    if (m_compilation->getRoot().topInstances.empty()) {
        m_cachedAnalysisDiags = Diagnostics{};
        return nullptr;
    }

    auto manager = std::make_unique<analysis::AnalysisManager>(m_analysisOptions);
    m_compilation->freeze();
    manager->analyze(*m_compilation);
    m_compilation->unfreeze();

    // Shallow analysis does not have all references to definitions and package members.
    m_cachedAnalysisDiags = manager->getDiagnostics().filter(
        {diag::UnusedDefinition, diag::UnusedPackageParameter, diag::UnusedPackageSubroutine,
         diag::UnusedPackageTypedef, diag::UnusedPackageVar});
    m_driverAnalysis = std::move(manager);
    return m_driverAnalysis.get();
}

std::vector<const analysis::ValueDriver*> ShallowCompilation::getDrivers(
    const ast::ValueSymbol& symbol) {
    if (m_activeDesign) {
        auto* designSymbol = getDesignSymbol(symbol);
        if (auto* designValue = designSymbol ? designSymbol->as_if<ast::ValueSymbol>() : nullptr)
            return m_activeDesign->getAnalysis().getDrivers(*designValue);
    }

    auto* manager = getAnalysisManager();
    if (!manager)
        return {};
    if (!m_analysisQueries)
        m_analysisQueries = std::make_unique<analysis::AnalysisQueries>(*m_compilation, *manager);
    return m_analysisQueries->getDrivers(symbol);
}

} // namespace server
