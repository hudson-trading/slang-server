//------------------------------------------------------------------------------
// Completions.cpp
// Completions for the LSP server.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#include "completions/Completions.h"

#include "lsp/SnippetString.h"
#include "util/Converters.h"
#include "util/Formatting.h"
#include "util/Logging.h"
#include <exception>
#include <fmt/format.h>
#include <rfl/Result.hpp>

#include "slang/ast/Lookup.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"

namespace server::completions {
using namespace slang;

//------------------------------------------------------------------------------
// Macros
//------------------------------------------------------------------------------

lsp::CompletionItem getMacroCompletion(std::string name) {
    return lsp::CompletionItem{
        .label = "`" + name,
        .labelDetails =
            lsp::CompletionItemLabelDetails{
                .detail = " Macro",
            },
        .kind = lsp::CompletionItemKind::Constant,
        .filterText = name,
    };
}

lsp::CompletionItem getMacroCompletion(const slang::syntax::DefineDirectiveSyntax& macro) {
    lsp::CompletionItem ret = getMacroCompletion(std::string{macro.name.valueText()});
    resolveMacro(macro, ret);
    return ret;
}

void resolveMacro(const syntax::DefineDirectiveSyntax& macro, lsp::CompletionItem& ret) {
    SnippetString output;
    output.appendText(macro.name.valueText());
    if (macro.formalArguments) {

        output.appendText("(");
        auto& args = macro.formalArguments->args;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                output.appendText(", ");
            }
            output.appendPlaceholder(args[i]->name.valueText());
        }
        output.appendText(")");
    }
    ret.insertText = output.getValue();
    ret.insertTextFormat = lsp::InsertTextFormat::Snippet;
    ret.documentation = svCodeBlock(macro);
}

//------------------------------------------------------------------------------
// Modules, Interfaces, Packages
//------------------------------------------------------------------------------
lsp::CompletionItem getModuleCompletion(std::string name, lsp::SymbolKind kind) {
    auto kindStr = [kind]() -> std::string {
        switch (kind) {
            case lsp::SymbolKind::Module:
                return " Module";
            case lsp::SymbolKind::Package:
                return " Package";
            case lsp::SymbolKind::Interface:
                return " Interface";
            default:
                return " Symbol";
        }
    }();

    return lsp::CompletionItem{
        .label = name,
        .labelDetails =
            lsp::CompletionItemLabelDetails{
                .detail = kindStr,
            },
        .kind = lsp::CompletionItemKind::Module,
        .filterText = name,
    };
}

class PortVisitor : public slang::syntax::SyntaxVisitor<PortVisitor> {
public:
    std::vector<std::string_view> names;
    size_t maxLen = 0;

    void handle(const slang::syntax::DeclaratorSyntax& port) {
        names.push_back(port.name.valueText());
        maxLen = std::max(maxLen, port.name.valueText().length());
    }

    void handle(const slang::syntax::ExplicitNonAnsiPortSyntax& portDecl) {
        names.push_back(portDecl.name.valueText());
        maxLen = std::max(maxLen, portDecl.name.valueText().length());
    }
};

class ParamVisitor : public slang::syntax::SyntaxVisitor<ParamVisitor> {
public:
    std::vector<std::string_view> names;
    std::vector<std::string> defaults;
    size_t maxLen = 0;

    void handle(const slang::syntax::DeclaratorSyntax& param) {
        names.push_back(param.name.valueText());
        defaults.push_back(param.initializer ? param.initializer->expr->toString() : "");
        ltrim(defaults.back());
        maxLen = std::max(maxLen, param.name.valueText().length());
    }

    void handle(const slang::syntax::TypeAssignmentSyntax& param) {
        names.push_back(param.name.valueText());
        defaults.push_back(param.assignment ? param.assignment->type->toString() : "");
        ltrim(defaults.back());
        maxLen = std::max(maxLen, param.name.valueText().length());
    }
};

void resolveModule(const slang::syntax::ModuleHeaderSyntax& header, lsp::CompletionItem& ret,
                   bool excludeName) {

    SnippetString output;
    if (!excludeName) {
        output.appendText(header.name.valueText());
        output.appendText(" #");
    }
    output.appendText("(\n");

    // get params
    size_t maxLen = 0;
    std::vector<std::string_view> names;
    std::vector<std::string> defaults;
    if (header.parameters) {
        ParamVisitor visitor;
        header.parameters->visit(visitor);
        names = std::move(visitor.names);
        defaults = std::move(visitor.defaults);
        maxLen = visitor.maxLen;
    }

    // append params
    for (size_t i = 0; i < names.size(); ++i) {
        auto name = std::string{names[i]};
        auto nameFmt = name + std::string(maxLen - name.length(), ' ');
        output.appendText("\t." + nameFmt + "(");
        if (defaults[i].empty()) {
            output.appendPlaceholder(name);
        }
        else {
            // TODO: ideally we could append hover info, don't think that's supported
            output.appendPlaceholder(fmt::format("{} /* default {} */", name, defaults[i]));
        }
        output.appendText(")");
        if (i < names.size() - 1) {
            output.appendText(",\n");
        }
        else {
            output.appendText("\n ");
        }
    }
    output.appendText(") ");
    output.appendPlaceholder(toCamelCase(header.name.valueText()));
    output.appendText(" (\n");

    // get ports
    maxLen = 0;
    names.clear();
    if (header.ports) {
        PortVisitor visitor;
        header.ports->visit(visitor);
        names = std::move(visitor.names);
        maxLen = visitor.maxLen;
    }

    // append ports
    for (size_t i = 0; i < names.size(); ++i) {
        auto name = std::string{names[i]};
        auto nameFmt = name + std::string(maxLen - name.length(), ' ');
        output.appendText("\t." + nameFmt + "(");
        output.appendPlaceholder(name);
        output.appendText(")");
        if (i < names.size() - 1) {
            output.appendText(",\n");
        }
        else {
            output.appendText("\n");
        }
    }

    output.appendText(");");

    ret.insertText = output.getValue();
    ret.insertTextFormat = lsp::InsertTextFormat::Snippet;
    ret.documentation = svCodeBlock(header);
}

void resolveModule(const slang::syntax::SyntaxTree& tree, std::string_view moduleName,
                   lsp::CompletionItem& ret, bool excludeName) {
    for (auto [syntax, node] : tree.getMetadata().nodeMeta) {
        auto& module = syntax->as<slang::syntax::ModuleDeclarationSyntax>();
        if (module.header->name.valueText() != moduleName)
            continue;

        switch (syntax->kind) {
            case slang::syntax::SyntaxKind::InterfaceDeclaration:
            case slang::syntax::SyntaxKind::ModuleDeclaration: {
                resolveModule(*module.header, ret, excludeName);
            } break;
            default: {
                // Packages and programs- just do the name. For packages we may want to
                // automatically add the ::, but not sure if that will retrigger completions
                ret.documentation = svCodeBlock(*module.header);
                ret.insertText = module.header->name.valueText();
                ret.insertTextFormat = lsp::InsertTextFormat::PlainText;
                continue;
            }
        }
        break;
    }
}

lsp::CompletionItem getMemberCompletion(const slang::ast::Symbol& symbol) {

    auto completionKind = [&symbol]() -> lsp::CompletionItemKind {
        switch (symbol.kind) {
            case slang::ast::SymbolKind::Variable:
                return lsp::CompletionItemKind::Variable;
            case slang::ast::SymbolKind::Parameter:
                return lsp::CompletionItemKind::TypeParameter;
            case slang::ast::SymbolKind::TypeAlias: {
                auto& typeAlias = symbol.as<slang::ast::TypeAliasType>();
                if (typeAlias.isEnum()) {
                    return lsp::CompletionItemKind::Enum;
                }
                else {
                    return lsp::CompletionItemKind::Struct;
                }
            }
            case slang::ast::SymbolKind::TypeParameter:
                return lsp::CompletionItemKind::Struct;
            case slang::ast::SymbolKind::Subroutine:
                return lsp::CompletionItemKind::Function;
            case slang::ast::SymbolKind::Port:
            case slang::ast::SymbolKind::InterfacePort:
                return lsp::CompletionItemKind::Interface;
            case slang::ast::SymbolKind::Instance:
            case slang::ast::SymbolKind::InstanceArray:
                return lsp::CompletionItemKind::Class;
            case slang::ast::SymbolKind::EnumValue:
                return lsp::CompletionItemKind::EnumMember;
            case slang::ast::SymbolKind::GenerateBlock:
            case slang::ast::SymbolKind::GenerateBlockArray:
                // Ideally would be "Module" which looks like '{}', but we have to diff between
                // actual module completions
                return lsp::CompletionItemKind::Snippet;
            default:
                return lsp::CompletionItemKind::Property;
        }
    }();

    // Detail str is shown in the dropdown next to the names; show brief type information, fall back
    // to kind. The kind is already revealed in the icon (completionKind above), so we don't need to
    // repeat this.
    std::string detailStr;

    if (slang::ast::SubroutineSymbol::isKind(symbol.kind)) {
        auto& subroutine = symbol.as<slang::ast::SubroutineSymbol>();
        detailStr = subroutineString(subroutine.subroutineKind);
    }
    else if (symbol.kind == slang::ast::SymbolKind::TypeAlias) {
        auto& typeAlias = symbol.as<slang::ast::TypeAliasType>();
        auto& unwrapped = typeAlias.getCanonicalType();
        if (unwrapped.kind != ast::SymbolKind::ErrorType) {
            detailStr = toString(unwrapped.kind);
        }
        else {
            detailStr = "TypeAlias";
        }
    }
    else if (slang::ast::ValueSymbol::isKind(symbol.kind) ||
             slang::ast::PortSymbol::isKind(symbol.kind) ||
             slang::ast::ParameterSymbol::isKind(symbol.kind) ||
             slang::ast::TypeParameterSymbol::isKind(symbol.kind) ||
             slang::ast::InterfacePortSymbol::isKind(symbol.kind)) {

        auto declType = symbol.getDeclaredType();
        // For value symbols, unwrap their type to see in the dropdown, and go one layer up for the
        // syntax to include the type
        if (declType && declType->getTypeSyntax()) {
            auto typeSyntax = declType->getTypeSyntax();
            if (typeSyntax) {
                detailStr = slang::syntax::SyntaxPrinter()
                                .setIncludeComments(false)
                                .print(*typeSyntax)
                                .str();
            }
        }
        else if (slang::ast::PortSymbol::isKind(symbol.kind)) {
            auto& port = symbol.as<slang::ast::PortSymbol>();
            detailStr = portString(port.direction) + " " + port.getType().toString();
        }
        else if (slang::ast::InterfacePortSymbol::isKind(symbol.kind)) {
            auto ifaceConn = symbol.as<slang::ast::InterfacePortSymbol>().getConnection();
            detailStr = fmt::format("{}.{}", ifaceConn.first ? ifaceConn.first->name : "<generic>",
                                    ifaceConn.second ? ifaceConn.second->name : "<generic>");
        }
        else {
            detailStr = toString(symbol.kind);
        }
    }
    else if (slang::ast::InstanceSymbol::isKind(symbol.kind)) {
        auto& defName = symbol.as<slang::ast::InstanceSymbol>().getDefinition().name;
        detailStr = std::string{defName};
    }
    ltrim(detailStr);
    squashSpaces(detailStr);

    return lsp::CompletionItem{
        .label = std::string{symbol.name},
        .labelDetails = lsp::CompletionItemLabelDetails{.detail = " " + detailStr},
        .kind = completionKind,
        .documentation = std::nullopt, // Will be populated during resolve
        .filterText = std::string{symbol.name},
    };
}

void resolveMemberCompletion(const slang::ast::Scope& scope, lsp::CompletionItem& item) {
    bitmask<ast::LookupFlags> flags;
    if (item.kind == lsp::CompletionItemKind::Struct) {
        flags |= ast::LookupFlags::Type | ast::LookupFlags::TypeReference;
    }
    auto sym = ast::Lookup::unqualified(scope, item.label, flags);

    if (!sym) {
        ERROR("No symbol found for completion {}", item.label);
        return;
    }

    auto& symbol = *sym;

    const syntax::SyntaxNode* symSyntax = nullptr;
    std::string docStr;

    if (slang::ast::SubroutineSymbol::isKind(symbol.kind)) {
        auto& subroutine = symbol.as<slang::ast::SubroutineSymbol>();
        if (symbol.getSyntax()) {
            symSyntax = symbol.getSyntax();
        }
        else {
            docStr = fmt::format("{} {}(...)", subroutine.getReturnType().toString(), symbol.name);
        }
    }
    else if (symbol.kind == slang::ast::SymbolKind::TypeAlias) {
        symSyntax = symbol.getSyntax();
    }
    else if (slang::ast::ValueSymbol::isKind(symbol.kind) ||
             slang::ast::PortSymbol::isKind(symbol.kind) ||
             slang::ast::ParameterSymbol::isKind(symbol.kind) ||
             slang::ast::TypeParameterSymbol::isKind(symbol.kind) ||
             slang::ast::InterfacePortSymbol::isKind(symbol.kind)) {

        // TODO: find a more consistent way to pick out the full decl
        if (symbol.getSyntax()) {
            if (symbol.getSyntax()->parent) {
                symSyntax = symbol.getSyntax()->parent;
            }
            else {
                symSyntax = symbol.getSyntax();
            }
        }
    }
    else if (slang::ast::InstanceSymbol::isKind(symbol.kind)) {
        symSyntax = symbol.getSyntax();
    }
    else if (symbol.getSyntax()) {
        symSyntax = symbol.getSyntax();
    }
    if (symSyntax) {
        item.documentation = svCodeBlock(*symSyntax);
    }
    else {
        item.documentation = docStr;
    }
}

/// Get completions for members in a scope, recursing up until hitting a module instance
void getMemberCompletions(std::vector<lsp::CompletionItem>& results, const slang::ast::Scope* scope,
                          bool isLhs) {

    if (!scope) {
        return;
    }

    // Walk up the scope hierarchy until we hit a module instance or run out of scopes
    const slang::ast::Scope* currentScope = scope;
    const slang::ast::Symbol* prevSym = nullptr;
    while (currentScope) {
        // Add members from the current scope
        for (auto& member : currentScope->members()) {
            if (&member == prevSym) {
                // Skip the previous scope, as we should have no reason to reference it
                continue;
            }
            if (member.name.empty()) {
                continue;
            }
            if (isLhs && !slang::ast::Type::isKind(member.kind)) {
                // Skip variables on the lhs, as they are not valid completions
                continue;
            }

            // ports are in there twice as value symbols and port symbols, so skip the value
            if (member.kind == slang::ast::SymbolKind::Variable && results.size() > 0 &&
                results.back().label == member.name) {
                continue;
            }

            // unwrap enum values, explicit imports
            if (slang::ast::TransparentMemberSymbol::isKind(member.kind)) {
                auto& wrapped = member.as<slang::ast::TransparentMemberSymbol>().wrapped;
                results.push_back(completions::getMemberCompletion(wrapped));
            }
            else if (slang::ast::ExplicitImportSymbol::isKind(member.kind)) {
                auto importSym = member.as<slang::ast::ExplicitImportSymbol>().importedSymbol();
                if (importSym) {
                    results.push_back(completions::getMemberCompletion(*importSym));
                }
            }
            else {
                results.push_back(completions::getMemberCompletion(member));
            }
        }

        // Add wildcard imports
        if (auto importData = currentScope->getWildcardImportData()) {
            for (auto import : importData->wildcardImports) {
                auto package = import->getPackage();
                if (package != nullptr) {
                    INFO("Adding wildcard imports from package {}", package->name);
                    getMemberCompletions(results, package, isLhs);
                }
            }
        }

        // Check if this scope belongs to a module instance - if so, stop here
        auto& parentSymbol = currentScope->asSymbol();
        if (parentSymbol.kind == slang::ast::SymbolKind::InstanceBody ||
            parentSymbol.kind == slang::ast::SymbolKind::Package) {
            break;
        }

        // Move up to parent scope
        prevSym = &currentScope->asSymbol();
        currentScope = parentSymbol.getParentScope();
    }
}

} // namespace server::completions
