//------------------------------------------------------------------------------
// MemberCompletions.cpp
// Lexical, member-access, and scoped-access completions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#include "completions/MemberCompletions.h"

#include "Indexer.h"
#include "ServerDriver.h"
#include "completions/InstanceCompletions.h"
#include "completions/KeywordCompletions.h"
#include "document/ShallowAnalysis.h"
#include "lsp/LspTypes.h"
#include "lsp/SnippetString.h"
#include "lsp/URI.h"
#include "util/Formatting.h"
#include "util/Logging.h"
#include "util/SlangExtensions.h"
#include <fmt/format.h>
#include <rfl/UnderlyingEnums.hpp>
#include <rfl/from_generic.hpp>
#include <rfl/to_generic.hpp>
#include <unordered_set>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Lookup.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/parsing/TokenKind.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"

namespace server::completions {
using namespace slang;

namespace {

bool hasSourceLocation(const ast::Symbol& symbol) {
    return symbol.location && symbol.location != SourceLocation::NoLocation;
}

std::string getCompletionTypeString(const ast::Symbol& symbol, const ast::Type& type) {
    if (type.isError()) {
        if (auto* value = symbol.as_if<ast::ValueSymbol>()) {
            if (auto result = getDeclaredTypeString(*value))
                return std::move(*result);
        }
    }
    return type.toString();
}

class CompletionSymbolFinder
    : public ast::ASTVisitor<CompletionSymbolFinder, ast::VisitFlags::Symbols> {
public:
    CompletionSymbolFinder(SourceLocation location, ast::SymbolKind kind, std::string_view name,
                           std::string_view symbolPath) :
        location(location), kind(kind), name(name), symbolPath(symbolPath) {}

    template<typename T>
        requires std::is_base_of_v<ast::Symbol, T>
    void handle(const T& symbol) {
        if (!inspect(symbol))
            return;
        if constexpr (std::is_base_of_v<ast::ValueSymbol, T>) {
            auto type = &unwrapErrorType(symbol.getType());
            while (type->isArray())
                type = &unwrapErrorType(*type->getArrayElementType());
            if (type->isStruct() || type->isUnion())
                type->visit(*this);
        }
        visitDefault(symbol);
    }

    void handle(const ast::TransparentMemberSymbol& symbol) {
        if (inspect(symbol))
            symbol.wrapped.visit(*this);
    }

    void handle(const ast::TypeAliasType& symbol) {
        if (inspect(symbol))
            symbol.getDeclaredType()->getType().visit(*this);
    }

    void handle(const ast::TypeParameterSymbol& symbol) {
        if (inspect(symbol))
            symbol.getTypeAlias().visit(*this);
    }

    const ast::Symbol* result = nullptr;

private:
    bool inspect(const ast::Symbol& symbol) {
        if (result || !visited.insert(&symbol).second)
            return false;
        if (symbol.location == location && symbol.kind == kind && symbol.name == name &&
            symbol.getHierarchicalPath() == symbolPath) {
            result = &symbol;
            return false;
        }
        return true;
    }

    SourceLocation location;
    ast::SymbolKind kind;
    std::string_view name;
    std::string_view symbolPath;
    std::unordered_set<const ast::Symbol*> visited;
};

class LexicalCompletionQuery final : public MemberCompletionQuery {
public:
    LexicalCompletionQuery(lsp::Range replacementRange, bool followedByCall,
                           bool followedByInstantiation) :
        MemberCompletionQuery(std::move(replacementRange), followedByCall,
                              followedByInstantiation) {}

    CompletionQueryKind kind() const final { return CompletionQueryKind::Lexical; }

    void getCompletions(std::vector<lsp::CompletionItem>& results, CompletionDispatch& dispatch,
                        const std::shared_ptr<SlangDoc>& doc,
                        const CompletionContext& context) const final {
        INFO("General completions with context: {}", toString(context.kind));
        InstanceCompletionQuery::addCompletions(results, getIndexer(dispatch), context);
        if (context.scope) {
            addCompletions(results, context.scope, context.kind, context.scope, doc->getURI().str(),
                           followedByCall, resolvesCompletionEdits(dispatch));
        }
    }
};

class MemberAccessCompletionQuery final : public MemberCompletionQuery {
public:
    MemberAccessCompletionQuery(lsp::Range replacementRange, const parsing::Token* receiverToken,
                                bool followedByCall) :
        MemberCompletionQuery(std::move(replacementRange), followedByCall),
        receiverToken(receiverToken) {}

    CompletionQueryKind kind() const final { return CompletionQueryKind::MemberAccess; }

    void getCompletions(std::vector<lsp::CompletionItem>& results, CompletionDispatch& dispatch,
                        const std::shared_ptr<SlangDoc>& doc,
                        const CompletionContext& context) const final {
        if (!receiverToken) {
            WARN("No expression token found before '.'");
            return;
        }

        auto* symbol = context.analysis->getSymbolAtToken(receiverToken);
        if (!symbol) {
            WARN("No symbol found for token {}, checking index.", receiverToken->valueText());
            auto symbolLoc = getIndexer(dispatch).getFirstSymbolLoc(receiverToken->valueText());
            if (!symbolLoc) {
                WARN("No symbol found in index for {}", receiverToken->valueText());
                return;
            }
            auto targetDoc = getDriver(dispatch).getDocument(URI::fromFile(*symbolLoc->uri));
            if (!targetDoc)
                return;
            symbol = targetDoc->getAnalysis()->getDefinition(receiverToken->valueText());
            if (!symbol) {
                WARN("No symbol found in compilation for {}", receiverToken->valueText());
                return;
            }
        }

        if (ast::DefinitionSymbol::isKind(symbol->kind)) {
            auto& definition = symbol->as<ast::DefinitionSymbol>();
            if (definition.definitionKind == ast::DefinitionKind::Interface) {
                for (auto& modport : definition.modports) {
                    results.push_back(lsp::CompletionItem{.label = std::string(modport),
                                                          .kind = lsp::CompletionItemKind::Field,
                                                          .documentation = svCodeBlock(
                                                              fmt::format("modport {}", modport))});
                }
                return;
            }
        }

        auto* targetScope = context.analysis->getScopeFromSym(symbol);
        if (!targetScope) {
            WARN("No scope found for sym {}: {}", symbol->getHierarchicalPath(),
                 toString(symbol->kind));
            return;
        }

        INFO("Getting hierarchical completions for symbol {} in scope {}", symbol->name,
             symbol->getHierarchicalPath());
        std::string_view previousLabel;
        for (auto& member : targetScope->members()) {
            if (member.name.empty() || member.name == previousLabel)
                continue;
            previousLabel = member.name;
            results.push_back(getHierarchicalCompletion(*symbol, member, doc->getURI().str(),
                                                        followedByCall,
                                                        resolvesCompletionEdits(dispatch)));
        }
    }

private:
    const parsing::Token* receiverToken;
};

class ScopedAccessCompletionQuery final : public MemberCompletionQuery {
public:
    ScopedAccessCompletionQuery(lsp::Range replacementRange, const parsing::Token* receiverToken,
                                bool followedByCall) :
        MemberCompletionQuery(std::move(replacementRange), followedByCall),
        receiverToken(receiverToken) {}

    CompletionQueryKind kind() const final { return CompletionQueryKind::ScopedAccess; }

    void getCompletions(std::vector<lsp::CompletionItem>& results, CompletionDispatch& dispatch,
                        const std::shared_ptr<SlangDoc>& doc,
                        const CompletionContext& context) const final {
        auto& analysis = context.analysis;
        if (!analysis || !analysis->getCompilation()) {
            ERROR("No analysis or compilation available for document {}", doc->getPath());
            return;
        }
        if (!receiverToken) {
            WARN("No receiver token found before '::'");
            return;
        }

        auto* receiver = analysis->getSymbolAtToken(receiverToken);
        if (!receiver)
            receiver = analysis->getCompilation()->getPackage(receiverToken->valueText());
        auto* targetScope = analysis->getScopeFromSym(receiver);
        if (!targetScope) {
            WARN("No scoped completion target found for {}", receiverToken->valueText());
            return;
        }

        INFO("Looking for scoped members in {}", targetScope->asSymbol().getHierarchicalPath());
        addCompletions(results, targetScope, CompletionContextKind::Expression, context.scope,
                       doc->getURI().str(), followedByCall, resolvesCompletionEdits(dispatch));
    }

private:
    const parsing::Token* receiverToken;
};

} // namespace

std::unique_ptr<CompletionQuery> MemberCompletionQuery::createLexical(
    lsp::Range replacementRange, bool followedByCall, bool followedByInstantiation) {
    return std::make_unique<LexicalCompletionQuery>(std::move(replacementRange), followedByCall,
                                                    followedByInstantiation);
}

std::unique_ptr<CompletionQuery> MemberCompletionQuery::createMemberAccess(
    lsp::Range replacementRange, const parsing::Token* receiverToken, bool followedByCall) {
    return std::make_unique<MemberAccessCompletionQuery>(std::move(replacementRange), receiverToken,
                                                         followedByCall);
}

std::unique_ptr<CompletionQuery> MemberCompletionQuery::createScopedAccess(
    lsp::Range replacementRange, const parsing::Token* receiverToken, bool followedByCall) {
    return std::make_unique<ScopedAccessCompletionQuery>(std::move(replacementRange), receiverToken,
                                                         followedByCall);
}

lsp::CompletionItemKind MemberCompletionQuery::getCompletionKind(const slang::ast::Symbol& symbol) {
    switch (symbol.kind) {
        case slang::ast::SymbolKind::Variable:
        case slang::ast::SymbolKind::Net:
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
        case slang::ast::SymbolKind::ClassType:
        case slang::ast::SymbolKind::GenericClassDef:
            return lsp::CompletionItemKind::Class;
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
};

std::string getInstanceArrayCompletionDetail(const ast::InstanceArraySymbol& array) {
    const ast::Symbol* element = &array;
    std::string dimensions;

    while (auto nestedArray = element->as_if<ast::InstanceArraySymbol>()) {
        if (nestedArray->elements.empty())
            return std::string(toString(array.kind));

        dimensions += fmt::format("[{}]", nestedArray->elements.size());
        element = nestedArray->elements.front();
    }

    if (auto instance = element->as_if<ast::InstanceSymbol>())
        return std::string(instance->getDefinition().name) + dimensions;

    return std::string(toString(element->kind)) + dimensions;
}

std::string getMemberCompletionDetail(const slang::ast::Symbol& symbol) {
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
        auto& unwrapped = unwrapErrorType(typeAlias);
        if (unwrapped.kind != ast::SymbolKind::ErrorType) {
            detailStr = toString(unwrapped.kind);
        }
        else {
            detailStr = "TypeAlias";
        }
    }
    else if (slang::ast::InterfacePortSymbol::isKind(symbol.kind)) {
        auto& port = symbol.as<slang::ast::InterfacePortSymbol>();
        detailStr = port.interfaceDef ? std::string{port.interfaceDef->name} : "interface";
        if (!port.modport.empty()) {
            detailStr += ".";
            detailStr += port.modport;
        }
        if (auto portSyntax = port.getSyntax()) {
            if (auto declarator = portSyntax->as_if<syntax::DeclaratorSyntax>()) {
                for (auto dimension : declarator->dimensions) {
                    detailStr +=
                        syntax::SyntaxPrinter().setIncludeComments(false).print(*dimension).str();
                }
            }
        }
    }
    else if (slang::ast::PortSymbol::isKind(symbol.kind)) {
        auto& port = symbol.as<slang::ast::PortSymbol>();
        auto& typeSymbol = port.internalSymbol ? *port.internalSymbol : symbol;
        detailStr = portString(port.direction) + " " +
                    getCompletionTypeString(typeSymbol, port.getType());
    }
    else if (slang::ast::InstanceSymbol::isKind(symbol.kind)) {
        auto& defName = symbol.as<slang::ast::InstanceSymbol>().getDefinition().name;
        detailStr = std::string{defName};
    }
    else if (slang::ast::InstanceArraySymbol::isKind(symbol.kind)) {
        detailStr = getInstanceArrayCompletionDetail(symbol.as<ast::InstanceArraySymbol>());
    }
    else {
        bool supportsDeclaredTypeDetail = slang::ast::ValueSymbol::isKind(symbol.kind) ||
                                          slang::ast::ParameterSymbol::isKind(symbol.kind) ||
                                          slang::ast::TypeParameterSymbol::isKind(symbol.kind);
        auto declType = symbol.getDeclaredType();
        // For value symbols, unwrap their type to see in the dropdown, and go one layer up for
        // the syntax to include the type
        if (supportsDeclaredTypeDetail && declType && declType->getTypeSyntax()) {
            auto typeSyntax = declType->getTypeSyntax();
            if (typeSyntax) {
                detailStr = slang::syntax::SyntaxPrinter()
                                .setIncludeComments(false)
                                .print(*typeSyntax)
                                .str();
            }
        }
        else if (supportsDeclaredTypeDetail) {
            detailStr = toString(symbol.kind);
        }
    }

    ltrim(detailStr);
    squashSpaces(detailStr);
    return detailStr;
}

lsp::CompletionItem MemberCompletionQuery::getHierarchicalCompletion(
    const slang::ast::Symbol& parentSymbol, const slang::ast::Symbol& symbol,
    std::string_view documentUri, bool labelOnly, bool deferCallableEdit) {

    if (ast::FieldSymbol::isKind(symbol.kind)) {
        auto& field = symbol.as<ast::FieldSymbol>();
        auto detailStr = getCompletionTypeString(field, field.getType());
        auto valSym = parentSymbol.as_if<ast::ValueSymbol>();
        auto descStr = valSym ? valSym->getType().getLexicalPath() : parentSymbol.getLexicalPath();
        auto item = lsp::CompletionItem{
            .label = std::string{symbol.name},
            .labelDetails =
                lsp::CompletionItemLabelDetails{
                    .detail = " " + detailStr,
                    .description = descStr,
                },
            .kind = getCompletionKind(symbol),
            .documentation = std::nullopt,
            .filterText = std::string{symbol.name},
            .data = rfl::to_generic<rfl::UnderlyingEnums>(CompletionData{
                .documentUri = std::string(documentUri),
                .symbolPath = symbol.getHierarchicalPath(),
                .bufferId = symbol.location.buffer().getId(),
                .offset = symbol.location.offset(),
                .symbolKind = symbol.kind,
                .labelOnly = labelOnly,
            }),
        };
        if (!hasSourceLocation(symbol)) {
            resolve(symbol, item);
            item.data.reset();
        }
        return item;
    }
    else {
        // hierarchical completions;
        return getCompletion(symbol, symbol.getParentScope(), documentUri, labelOnly,
                             deferCallableEdit);
    }
}

static void setSubroutineCompletionEdit(const slang::ast::SubroutineSymbol& subroutine,
                                        lsp::CompletionItem& item) {
    SnippetString toInsert(subroutine.name);
    toInsert.appendText("(");
    auto args = subroutine.getArguments();
    for (auto& arg : args) {
        auto argType = getCompletionTypeString(*arg, arg->getType());

        // TODO: We should use textDocument/signatureHelp to show types and default values
        if (arg->getDefaultValue() && arg->getDefaultValue()->syntax) {
            toInsert.appendPlaceholder(fmt::format("{} /* : {} = {} */", arg->name, argType,
                                                   arg->getDefaultValue()->syntax->toString()));
        }
        else {
            toInsert.appendPlaceholder(fmt::format("{} /* {} */", arg->name, argType));
        }

        if (arg != args.back())
            toInsert.appendText(", ");
    }
    toInsert.appendText(")");
    item.insertText = toInsert.getValue();
    item.insertTextFormat = lsp::InsertTextFormat::Snippet;
}

lsp::CompletionItem MemberCompletionQuery::getCompletion(const slang::ast::Symbol& symbol,
                                                         const slang::ast::Scope* currentScope,
                                                         std::string_view documentUri,
                                                         bool labelOnly, bool deferCallableEdit) {

    auto detailStr = getMemberCompletionDetail(symbol);

    // Check if symbol is from a different scope and add lexical path
    std::optional<std::string> descriptionStr;
    auto parentScope = symbol.getParentScope();
    if (parentScope && (!currentScope || parentScope != currentScope)) {
        auto hierPath = parentScope->asSymbol().getLexicalPath();
        if (!hierPath.empty()) {
            descriptionStr = hierPath;
        }
    }

    auto item = lsp::CompletionItem{
        .label = std::string{symbol.name},
        .labelDetails = lsp::CompletionItemLabelDetails{.detail = " " + detailStr,
                                                        .description = descriptionStr},
        .kind = getCompletionKind(symbol),
        .documentation = std::nullopt,
        .filterText = std::string{symbol.name},
        .data = rfl::to_generic<rfl::UnderlyingEnums>(CompletionData{
            .documentUri = std::string(documentUri),
            .symbolPath = symbol.getHierarchicalPath(),
            .bufferId = symbol.location.buffer().getId(),
            .offset = symbol.location.offset(),
            .symbolKind = symbol.kind,
            .labelOnly = labelOnly,
        }),
    };

    if (slang::ast::SubroutineSymbol::isKind(symbol.kind) &&
        (!deferCallableEdit || !hasSourceLocation(symbol)))
        setSubroutineCompletionEdit(symbol.as<slang::ast::SubroutineSymbol>(), item);

    if (!hasSourceLocation(symbol)) {
        resolve(symbol, item, true);
        item.data.reset();
    }
    return item;
}

void MemberCompletionQuery::resolve(const slang::ast::Symbol& symbol, lsp::CompletionItem& item,
                                    bool resolveCallableEdit) {
    const syntax::SyntaxNode* symSyntax = nullptr;
    std::string docStr;

    if (slang::ast::SubroutineSymbol::isKind(symbol.kind)) {
        auto& subroutine = symbol.as<slang::ast::SubroutineSymbol>();
        if (resolveCallableEdit)
            setSubroutineCompletionEdit(subroutine, item);
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

/// Get completions for members in a scope, including the enclosing compilation unit
void MemberCompletionQuery::addCompletions(std::vector<lsp::CompletionItem>& results,
                                           const slang::ast::Scope* scope,
                                           CompletionContextKind contextKind,
                                           const slang::ast::Scope* originalScope,
                                           std::string_view documentUri, bool labelOnly,
                                           bool deferCallableEdit, bool isOriginalCall) {

    if (isOriginalCall && contextKind == CompletionContextKind::ModuleMember) {
        addKeywordCompletions(results);
    }

    if (!scope) {
        ERROR("No scope for member completion");
        return;
    }

    // Only show types (not signals/variables) when at the top level of a module body
    // or in a port list — these are declaration positions.
    bool typesOnly = (contextKind == CompletionContextKind::ModuleMember ||
                      contextKind == CompletionContextKind::PortList);

    // Walk up through the compilation unit, but don't include the root's design hierarchy.
    const slang::ast::Scope* currentScope = scope;
    const slang::ast::Symbol* prevSym = nullptr;
    while (currentScope) {
        // Add members from the current scope
        for (auto& member : currentScope->members()) {
            if (&member == prevSym) {
                // Skip the previous scope, as we should have no reason to reference it
                continue;
            }
            if (member.name.empty() || member.kind == slang::ast::SymbolKind::Package) {
                continue;
            }
            if (contextKind == CompletionContextKind::Expression &&
                (member.kind == slang::ast::SymbolKind::ClassType ||
                 member.kind == slang::ast::SymbolKind::GenericClassDef)) {
                continue;
            }
            if (typesOnly && !slang::ast::Type::isKind(member.kind) &&
                member.kind != slang::ast::SymbolKind::GenericClassDef &&
                member.kind != slang::ast::SymbolKind::TypeParameter) {
                continue;
            }

            // ports are in there twice as value symbols and port symbols, so skip the value
            if ((member.kind == slang::ast::SymbolKind::Variable ||
                 member.kind == slang::ast::SymbolKind::Net) &&
                results.size() > 0 && results.back().label == member.name) {
                continue;
            }

            // unwrap enum values, explicit imports
            if (slang::ast::TransparentMemberSymbol::isKind(member.kind)) {
                auto& wrapped = member.as<slang::ast::TransparentMemberSymbol>().wrapped;
                results.push_back(getCompletion(wrapped, originalScope, documentUri, labelOnly,
                                                deferCallableEdit));
            }
            else if (slang::ast::ExplicitImportSymbol::isKind(member.kind)) {
                auto importSym = member.as<slang::ast::ExplicitImportSymbol>().importedSymbol();
                if (importSym) {
                    results.push_back(getCompletion(*importSym, originalScope, documentUri,
                                                    labelOnly, deferCallableEdit));
                }
            }
            else {
                results.push_back(getCompletion(member, originalScope, documentUri, labelOnly,
                                                deferCallableEdit));
            }
        }

        // Add wildcard imports
        if (isOriginalCall) {
            if (auto importData = currentScope->getWildcardImportData()) {
                for (auto import : importData->wildcardImports) {
                    auto package = import->getPackage();
                    if (package != nullptr) {
                        INFO("Adding wildcard imports from package {}", package->name);
                        addCompletions(results, package, contextKind, originalScope, documentUri,
                                       labelOnly, deferCallableEdit, false);
                    }
                }
            }
        }

        // Package members are only available through imports or scoped access.
        auto& parentSymbol = currentScope->asSymbol();
        if (parentSymbol.kind == slang::ast::SymbolKind::CompilationUnit ||
            parentSymbol.kind == slang::ast::SymbolKind::Package) {
            break;
        }

        // Move up to parent scope
        prevSym = &currentScope->asSymbol();
        currentScope = parentSymbol.getParentScope();
    }
}

void MemberCompletionQuery::resolve(CompletionDispatch& dispatch, lsp::CompletionItem& item) {
    if (!item.data)
        return;

    auto data = rfl::from_generic<CompletionData, rfl::UnderlyingEnums>(*item.data);
    item.data.reset();
    if (!data) {
        WARN("Invalid member completion data for {}: {}", item.label, data.error().what());
        return;
    }

    auto doc = getDriver(dispatch).getDocument(URI(data->documentUri));
    if (!doc)
        return;

    auto analysis = doc->getAnalysis();
    if (!analysis || !analysis->getCompilation())
        return;

    auto& root = analysis->getCompilation()->getRoot();
    const ast::Symbol* symbol = nullptr;
    if (data->bufferId) {
        CompletionSymbolFinder finder(
            SourceLocation(BufferID(data->bufferId, "completion item resolve"), data->offset),
            data->symbolKind, item.label, data->symbolPath);
        root.visit(finder);
        symbol = finder.result;
    }
    if (!symbol) {
        try {
            symbol = root.lookupName(data->symbolPath, ast::LookupLocation::max,
                                     ast::LookupFlags::AllowUnnamedGenerate);
        }
        catch (const std::exception& e) {
            WARN("Invalid symbol path for completion {}: {}", item.label, e.what());
        }
    }
    if (!symbol) {
        WARN("No symbol found for completion {} at {}", item.label, data->symbolPath);
        return;
    }

    resolve(*symbol, item, resolvesCompletionEdits(dispatch));
    if (!data->labelOnly)
        updateCompletionEditText(item);
}

} // namespace server::completions
