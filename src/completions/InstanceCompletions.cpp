//------------------------------------------------------------------------------
// InstanceCompletions.cpp
// Module, interface, package, and class completions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "completions/InstanceCompletions.h"

#include "Indexer.h"
#include "lsp/SnippetString.h"
#include "util/Converters.h"
#include "util/Formatting.h"
#include "util/Logging.h"
#include <fmt/format.h>
#include <unordered_set>

#include "slang/parsing/Token.h"
#include "slang/parsing/TokenKind.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxVisitor.h"

namespace server::completions {
using namespace slang;

namespace {

class PortVisitor : public syntax::SyntaxVisitor<PortVisitor> {
public:
    std::vector<std::string_view> names;
    size_t maxLen = 0;

    void handle(const syntax::DeclaratorSyntax& port) {
        names.push_back(port.name.valueText());
        maxLen = std::max(maxLen, port.name.valueText().length());
    }

    void handle(const syntax::ExplicitNonAnsiPortSyntax& portDecl) {
        names.push_back(portDecl.name.valueText());
        maxLen = std::max(maxLen, portDecl.name.valueText().length());
    }
};

void collectParams(const syntax::ParameterPortListSyntax& paramList,
                   std::vector<std::string_view>& names, std::vector<std::string>& defaults,
                   size_t& maxLen) {
    bool lastLocal = false;
    for (auto declaration : paramList.declarations) {
        if (declaration->keyword)
            lastLocal = declaration->keyword.kind == parsing::TokenKind::LocalParamKeyword;
        if (lastLocal)
            continue;

        if (declaration->kind == syntax::SyntaxKind::ParameterDeclaration) {
            auto& paramSyntax = declaration->as<syntax::ParameterDeclarationSyntax>();
            for (auto decl : paramSyntax.declarators) {
                names.push_back(decl->name.valueText());
                std::string defaultVal = decl->initializer ? decl->initializer->expr->toString()
                                                           : "";
                ltrim(defaultVal);
                defaults.push_back(std::move(defaultVal));
                maxLen = std::max(maxLen, decl->name.valueText().length());
            }
        }
        else {
            auto& paramSyntax = declaration->as<syntax::TypeParameterDeclarationSyntax>();
            for (auto decl : paramSyntax.declarators) {
                names.push_back(decl->name.valueText());
                std::string defaultVal = decl->assignment ? decl->assignment->type->toString() : "";
                ltrim(defaultVal);
                defaults.push_back(std::move(defaultVal));
                maxLen = std::max(maxLen, decl->name.valueText().length());
            }
        }
    }
}

class InstanceCompletionQueryImpl final : public InstanceCompletionQuery {
public:
    InstanceCompletionQueryImpl(lsp::Range replacementRange, const parsing::Token* moduleToken) :
        InstanceCompletionQuery(std::move(replacementRange)), moduleToken(moduleToken) {}

    CompletionQueryKind kind() const final { return CompletionQueryKind::InstantiationSuffix; }

    void getCompletions(std::vector<lsp::CompletionItem>& results, CompletionDispatch& dispatch,
                        const std::shared_ptr<SlangDoc>&, const CompletionContext&) const final {
        if (!moduleToken) {
            WARN("No module token found before instantiation suffix");
            return;
        }

        auto name = moduleToken->valueText();
        auto symbolLoc = getIndexer(dispatch).getFirstSymbolLoc(name);
        if (!symbolLoc) {
            ERROR("No module found for {}", name);
            return;
        }

        auto completion = getCompletion(std::string(name), symbolLoc->kind);
        resolve(dispatch, completion, *symbolLoc->uri, true);
        results.push_back(std::move(completion));
    }

private:
    const parsing::Token* moduleToken;
};

} // namespace

std::unique_ptr<CompletionQuery> InstanceCompletionQuery::create(
    lsp::Range replacementRange, const parsing::Token* moduleToken) {
    return std::make_unique<InstanceCompletionQueryImpl>(std::move(replacementRange), moduleToken);
}

lsp::CompletionItem InstanceCompletionQuery::getCompletion(std::string name,
                                                           syntax::SyntaxKind kind) {
    std::string detail;
    switch (kind) {
        case syntax::SyntaxKind::ModuleDeclaration:
            detail = " Module";
            break;
        case syntax::SyntaxKind::InterfaceDeclaration:
            detail = " Interface";
            break;
        default:
            detail = std::string(toString(kind));
            break;
    }

    return lsp::CompletionItem{
        .label = name,
        .labelDetails =
            lsp::CompletionItemLabelDetails{
                .detail = detail,
            },
        .kind = lsp::CompletionItemKind::Module,
        .filterText = name,
    };
}

void InstanceCompletionQuery::addCompletions(std::vector<lsp::CompletionItem>& results,
                                             const Indexer& indexer,
                                             const CompletionContext& context) {
    std::unordered_set<std::string_view> seenNames;

    indexer.forEachSymbol([&](const std::string& name, const Indexer::GlobalSymbolLoc& entry) {
        if (!seenNames.insert(name).second)
            return;

        std::string detail;
        std::optional<std::string> insertText;
        switch (entry.kind) {
            case syntax::SyntaxKind::ModuleDeclaration:
                if (context.kind != CompletionContextKind::ModuleMember)
                    return;
                detail = " Module";
                break;
            case syntax::SyntaxKind::InterfaceDeclaration:
                detail = " Interface";
                if (context.kind != CompletionContextKind::ModuleMember)
                    insertText = name;
                break;
            case syntax::SyntaxKind::PackageDeclaration:
                detail = " Package";
                break;
            case syntax::SyntaxKind::ClassDeclaration:
                detail = " Class";
                break;
            default:
                return;
        }
        results.push_back(lsp::CompletionItem{
            .label = name,
            .labelDetails =
                lsp::CompletionItemLabelDetails{
                    .detail = detail,
                },
            .kind = lsp::CompletionItemKind::Module,
            .filterText = name,
            .insertText = insertText,
        });
    });
}

void InstanceCompletionQuery::resolveModuleInstance(const syntax::ModuleHeaderSyntax& header,
                                                    lsp::CompletionItem& item, bool excludeName) {
    item.documentation = svCodeBlock(header);
    if (item.insertText)
        return;

    SnippetString output;
    size_t maxLen = 0;
    std::vector<std::string_view> names;
    std::vector<std::string> defaults;
    if (header.parameters)
        collectParams(*header.parameters, names, defaults, maxLen);

    if (!excludeName)
        output.appendText(header.name.valueText());

    if (!names.empty()) {
        if (!excludeName)
            output.appendText(" #");
        output.appendText("(\n");

        for (size_t i = 0; i < names.size(); ++i) {
            auto name = std::string(names[i]);
            auto nameFmt = name + std::string(maxLen - name.length(), ' ');
            output.appendText("\t." + nameFmt + "(");
            if (defaults[i].empty())
                output.appendPlaceholder(name);
            else
                output.appendPlaceholder(fmt::format("{} /* default {} */", name, defaults[i]));
            output.appendText(")");
            output.appendText(i < names.size() - 1 ? ",\n" : "\n ");
        }
        output.appendText(")");
    }

    output.appendText(" ");
    output.appendPlaceholder(toCamelCase(header.name.valueText()));
    output.appendText(" (");

    maxLen = 0;
    names.clear();
    if (header.ports) {
        PortVisitor visitor;
        header.ports->visit(visitor);
        names = std::move(visitor.names);
        maxLen = visitor.maxLen;
    }

    if (!names.empty()) {
        output.appendText("\n");
        for (size_t i = 0; i < names.size(); ++i) {
            auto name = std::string(names[i]);
            auto nameFmt = name + std::string(maxLen - name.length(), ' ');
            output.appendText("\t." + nameFmt + "(");
            output.appendPlaceholder(name);
            output.appendText(")");
            output.appendText(i < names.size() - 1 ? ",\n" : "\n");
        }
    }

    output.appendText(");");
    item.insertText = output.getValue();
    item.insertTextFormat = lsp::InsertTextFormat::Snippet;
}

void InstanceCompletionQuery::resolve(const syntax::SyntaxTree& tree, std::string_view moduleName,
                                      lsp::CompletionItem& item, bool excludeName) {
    for (auto [module, node] : tree.getMetadata().nodeMeta) {
        auto& header = *module->header;
        if (header.name.valueText() != moduleName)
            continue;

        switch (module->kind) {
            case syntax::SyntaxKind::InterfaceDeclaration:
            case syntax::SyntaxKind::ModuleDeclaration:
                resolveModuleInstance(header, item, excludeName);
                break;
            default:
                item.documentation = svCodeBlock(header);
                item.insertText = header.name.valueText();
                item.insertTextFormat = lsp::InsertTextFormat::PlainText;
                continue;
        }
        break;
    }
}

void InstanceCompletionQuery::resolve(CompletionDispatch& dispatch, lsp::CompletionItem& item,
                                      std::optional<std::filesystem::path> modulePath,
                                      bool excludeName) {
    auto name = item.label;
    if (!modulePath) {
        auto files = getIndexer(dispatch).getFilesForSymbol(name);
        if (files.empty()) {
            WARN("No files found for module {}", name);
            return;
        }
        if (files.size() > 1)
            WARN("Multiple files found for module {}: {}", name, rfl::json::write(files));
        modulePath = files[0];
    }

    auto maybeTree = syntax::SyntaxTree::fromFile(modulePath->string(), getSourceManager(dispatch),
                                                  getOptions(dispatch));
    if (!maybeTree) {
        WARN("Failed to load syntax tree for module {} from {}", name, modulePath->string());
        return;
    }

    resolve(*maybeTree.value(), name, item, excludeName);
    updateCompletionEditText(item);
}

} // namespace server::completions
