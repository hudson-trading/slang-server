//------------------------------------------------------------------------------
// CompletionDispatch.cpp
// Completion dispatch implementation for handling completion requests
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "completions/CompletionDispatch.h"

#include "completions/Completions.h"
#include "lsp/LspTypes.h"
#include "util/Converters.h"
#include "util/Logging.h"
#include <filesystem>

#include "slang/ast/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

namespace fs = std::filesystem;
namespace ast = slang::ast;

namespace server {

CompletionDispatch::CompletionDispatch(const Indexer& indexer, SourceManager& sourceManager,
                                       slang::Bag& options) :
    m_indexer(indexer), m_sourceManager(sourceManager), m_options(options) {
}

void CompletionDispatch::getInvokedCompletions(std::vector<lsp::CompletionItem>& results,
                                               std::shared_ptr<SlangDoc> doc, bool isLhs,
                                               slang::SourceLocation loc) {
    // Invoked happens when an identifier is starting to be typed or when the user presses a
    // shortcut
    // TODO:
    // - Add ids from import pkg::* syntax
    // - filter based on syntax context
    // - filter based on starting character? (already done client-side)

    auto scope = doc->getScopeAt(loc);

    // Add local members first
    if (scope) {
        m_lastScope = scope->asSymbol().getHierarchicalPath();
        m_lastDoc = doc;
        completions::getMemberCompletions(results, scope, isLhs);
    }

    if (isLhs) {
        // Add modules for lhs
        for (auto& [name, info] : m_indexer.symbolToFiles.getAllEntries()) {
            results.push_back(completions::getModuleCompletion(name, info.kind));
        }
        INFO("Returning {} module completions", results.size());
    }
    else {
        // Add packages as completions, because we may grab a value from those
        // Add these after though, since local vars are the common case
        // TODO: add these when we can sort completions
        // for (auto& [name, info] : m_indexer.symbolToFiles.getAllEntries()) {
        //     if (info.kind == lsp::SymbolKind::Package) {
        //         results.push_back(completions::getModuleCompletion(name, info.kind));
        //     }
        // }
    }
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
        auto [start, end] = m_indexer.getEntriesForSymbol(name);
        if (start == end) {
            ERROR("No module found for {}", name);
            WARN("With line {}", doc->getPrevText(toPosition(loc, m_sourceManager)));
            return;
        }
        else if (std::distance(start, end) > 1) {
            WARN("Multiple modules found for {}: {}", name, std::distance(start, end));
        }

        auto& [_, entry] = *start;
        auto completion = completions::getModuleCompletion(std::string{name}, entry.kind);
        resolveModuleCompletion(completion, fs::path(entry.location.uri.getPath()), true);
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
        m_lastDoc = doc;
        m_lastScope = pkg->getHierarchicalPath();
        if (!pkg) {
            ERROR("No package found for {}", packageName);
            return;
        }
        completions::getMemberCompletions(results, pkg, false);
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
        for (auto& [name, _info] : m_indexer.macroToFiles.getAllEntries()) {
            results.push_back(completions::getMacroCompletion(name));
        }
    }
    else {
        // Scope-based completions
        getInvokedCompletions(results, doc, false, loc);
    }
}

void CompletionDispatch::resolveModuleCompletion(lsp::CompletionItem& item,
                                                 std::optional<fs::path> modulePath,
                                                 bool excludeName) {
    auto name = item.label;
    if (modulePath == std::nullopt) {
        auto files = m_indexer.getRelevantFilesForName(name);
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

    auto maybeTree = slang::syntax::SyntaxTree::fromFile(std::string{file}, m_sourceManager,
                                                         m_options);
    if (!maybeTree) {
        WARN("Failed to load syntax tree for module {} from {}", name, file.string());
        return;
    }
    const auto& tree = maybeTree.value();
    completions::resolveModule(*tree, name, item, excludeName);
}

void CompletionDispatch::resolveMacroCompletion(lsp::CompletionItem& item) {
    // Parse the file to get the macro args
    auto path = m_indexer.getFilesForMacro(item.label.substr(1));

    if (path.size() == 0) {
        WARN("No macro files found for {}", item.label);
        return;
    }

    auto maybeTree = slang::syntax::SyntaxTree::fromFile(std::string{path[0]}, m_sourceManager,
                                                         m_options);

    if (!maybeTree) {
        return;
    }
    const auto& tree = maybeTree.value();
    for (auto macro : tree->getDefinedMacros()) {
        if (macro->name.valueText() == item.label.substr(1)) {
            completions::resolveMacro(*macro, item);
            return;
        }
    };
    WARN("Didn't find macro for {} in {}", item.label, path[0].string());
}

void CompletionDispatch::getCompletionItemResolve(lsp::CompletionItem& item) {
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
            // TODO: grab a shared ptr to the old compilation, so we don't have to do lookups
            // again
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
