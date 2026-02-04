//------------------------------------------------------------------------------
// CompletionDispatch.cpp
// Completion dispatch implementation for handling completion requests
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "completions/CompletionDispatch.h"

#include "completions/CompletionContext.h"
#include "completions/Completions.h"
#include "lsp/LspTypes.h"
#include "util/Converters.h"
#include "util/Formatting.h"
#include "util/Logging.h"
#include <filesystem>

#include "slang/ast/Compilation.h"
#include "slang/ast/Lookup.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/util/Util.h"

namespace fs = std::filesystem;
namespace ast = slang::ast;

namespace server {

CompletionDispatch::CompletionDispatch(const Indexer& indexer, SourceManager& sourceManager,
                                       slang::Bag& options) :
    m_indexer(indexer), m_sourceManager(sourceManager), m_options(options) {
}

void CompletionDispatch::getInvokedCompletions(std::vector<lsp::CompletionItem>& results,
                                               std::shared_ptr<SlangDoc> doc,
                                               const SourceLocation& loc) {
    // Invoked happens when an identifier is starting to be typed or when the user presses a
    // shortcut

    auto ctx = CompletionContext::fromLocation(*doc, loc);

    auto scope = ctx.scope;

    // Track scope for later resolution
    if (scope) {
        m_lastScope = scope->asSymbol().getHierarchicalPath();
        m_lastDoc = doc;
    }
    INFO("Invoked completions with context: {}", toString(ctx.kind));

    completions::addIndexedCompletions(results, m_indexer, ctx);

    if (scope) {
        bool isLhs = (ctx.kind == CompletionContextKind::PortList ||
                      ctx.kind == CompletionContextKind::Procedural ||
                      ctx.kind == CompletionContextKind::ModuleMember);
        completions::addMemberCompletions(results, scope, isLhs, scope);
    }

    INFO("Returning {} completions in {} context", results.size(), toString(ctx.kind));
}

void CompletionDispatch::getTriggerCompletions(char triggerChar, char prevChar,
                                               std::shared_ptr<SlangDoc> doc,
                                               slang::SourceLocation loc,
                                               std::vector<lsp::CompletionItem>& results) {
    if (triggerChar == '#') {
        // This branch will get hit if the resolve request was not responded to in time, and the
        // user continues with the module inst
        auto moduleToken = doc->getTokenAt(loc - 3);

        if (!moduleToken) {
            WARN("No module token found at location {}", loc);
            WARN("With line {}", doc->getPrevText(toPosition(loc, m_sourceManager)));
            return;
        }
        auto name = moduleToken->valueText();
        auto symbolLoc = m_indexer.getFirstSymbolLoc(name);
        if (!symbolLoc) {
            ERROR("No module found for {}", name);
            WARN("With line {}", doc->getPrevText(toPosition(loc, m_sourceManager)));
            return;
        }

        auto completion = completions::getInstanceCompletion(std::string{name}, symbolLoc->kind);
        resolveModuleCompletion(completion, *symbolLoc->uri, true);
        results.push_back(completion);
    }
    else if (triggerChar == ':' && prevChar == ':') {
        // We only want '::', a single colon can be used for wire slicing

        // The triggerChar is the second ':', so we need to look before the first ':'
        auto packageToken = doc->getTokenAt(loc - 3);
        if (!packageToken) {
            WARN("No package token found before '::'");
            return;
        }

        auto packageName = std::string{packageToken->valueText()};
        INFO("Looking for package members in package: {}", packageName);

        // completions::getPackageMemberCompletions(results, *doc, packageName);
        // Get the package from the compilation
        if (!doc->getSyntaxTree() || !doc->getCompilation()) {
            ERROR("No syntax tree or compilation available for document {}", doc->getPath());
            return;
        }

        auto& compilation = doc->getCompilation();
        auto pkg = compilation->getPackage(packageName);
        if (!pkg) {
            ERROR("No package found for {}", packageName);
            return;
        }
        m_lastDoc = doc;
        m_lastScope = pkg->getHierarchicalPath();
        auto originalScope = doc->getScopeAt(loc);
        completions::addMemberCompletions(results, pkg, false, originalScope);
    }
    else if (triggerChar == '`') {
        // Add local macros
        for (auto& macro : doc->getSyntaxTree()->getDefinedMacros()) {
            if (macro->name.location() == slang::SourceLocation::NoLocation) {
                // Only show macros that are defined before the cursor
                continue;
            }
            results.push_back(completions::getMacroCompletion(*macro));
        }
        // Add global macros
        for (const auto& name : m_indexer.getAllMacroNames()) {
            results.push_back(completions::getMacroCompletion(name));
        }
    }
    else if (triggerChar == '.') {
        // Member completions
        auto exprToken = doc->getTokenAt(loc - 2);
        if (!exprToken) {
            WARN("No expression token found before '.'");
            return;
        }
        auto sym = doc->getAnalysis().getSymbolAtToken(exprToken);
        if (!sym) {
            WARN("No symbol found for token {}", exprToken->valueText());
            return;
        }
        if (ast::DefinitionSymbol::isKind(sym->kind)) {
            auto& def = sym->as<ast::DefinitionSymbol>();
            if (def.definitionKind == ast::DefinitionKind::Interface) {
                for (auto& modport : def.modports) {
                    results.push_back(lsp::CompletionItem{.label = std::string(modport),
                                                          .kind = lsp::CompletionItemKind::Field,
                                                          .documentation = svCodeBlock(
                                                              fmt::format("modport {}", modport))});
                }
            }
            else {
                WARN("Definition {} is not an interface, can't get hierarchical completions",
                     def.name);
            }
            return;
        }
        auto scope = ShallowAnalysis::getScopeFromSym(sym);
        if (!scope) {
            WARN("No scope found for sym {}: {}", sym->getHierarchicalPath(), toString(sym->kind));
            return;
        }
        m_lastDoc = doc;
        m_lastScope = scope ? scope->asSymbol().getHierarchicalPath() : "";
        INFO("Getting hier completions for symbol {} in scope {}", sym->name,
             sym->getHierarchicalPath());
        for (auto& member : scope->members()) {
            results.push_back(completions::getHierarchicalCompletion(*sym, member));
        }
    }
    else {
        // Scope-based completions
        getInvokedCompletions(results, doc, loc);
    }
}

void CompletionDispatch::resolveModuleCompletion(lsp::CompletionItem& item,
                                                 std::optional<fs::path> modulePath,
                                                 bool excludeName) {
    auto name = item.label;
    if (modulePath == std::nullopt) {
        auto files = m_indexer.getFilesForSymbol(name);
        if (files.size() == 0) {
            WARN("No files found for module {}", name);
            return;
        }
        if (files.size() > 1) {
            WARN("Multiple files found for module {}: {}", name, rfl::json::write(files));
        }
        modulePath = files[0];
    }
    auto& file = modulePath.value();

    auto maybeTree = slang::syntax::SyntaxTree::fromFile(file.string(), m_sourceManager, m_options);
    if (!maybeTree) {
        WARN("Failed to load syntax tree for module {} from {}", name, file.string());
        return;
    }
    completions::resolveModule(*maybeTree.value(), name, item, excludeName);
}

void CompletionDispatch::resolveMacroCompletion(lsp::CompletionItem& item) {
    // Parse the file to get the macro args
    auto path = m_indexer.getFilesForMacro(item.label.substr(1));

    if (path.size() == 0) {
        WARN("No macro files found for {}", item.label);
        return;
    }

    auto maybeTree = slang::syntax::SyntaxTree::fromFile(path[0].string(), m_sourceManager,
                                                         m_options);

    if (!maybeTree) {
        return;
    }
    auto tree = maybeTree.value();
    for (auto macro : tree->getDefinedMacros()) {
        if (macro->name.valueText() == item.label.substr(1)) {
            completions::resolveMacro(*macro, item);
            return;
        }
    };
    WARN("Didn't find macro for {} in {}", item.label, path[0].string());
}

void CompletionDispatch::getCompletionItemResolve(lsp::CompletionItem& item) {
    INFO("Resolving completion item: {}", item.label);
    switch (*item.kind) {
        case lsp::CompletionItemKind::Constant: {
            resolveMacroCompletion(item);
            break;
        }
        case lsp::CompletionItemKind::Module: {
            resolveModuleCompletion(item);
            break;
        }
        default: {
            SLANG_ASSERT(m_lastDoc != nullptr);
            auto& comp = m_lastDoc->getCompilation();
            if (!comp) {
                ERROR("No compilation available for completion resolution");
                return;
            }
            const ast::Scope* scope = nullptr;
            for (auto member : comp->getRootNoFinalize().topInstances) {
                if (member->name == m_lastScope) {
                    scope = &(member->body.as<ast::Scope>());
                    break;
                }
            }
            if (scope == nullptr) {
                scope = comp->getPackage(m_lastScope);
            }
            if (scope == nullptr) {
                ERROR("No scope found for last scope {}", m_lastScope);
                return;
            }
            completions::resolveMemberCompletion(*scope, item);
            break;
        }
    }
    return;
}
} // namespace server
