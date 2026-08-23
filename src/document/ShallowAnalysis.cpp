//------------------------------------------------------------------------------
// ShallowAnalysis.cpp
// Implementation of document analysis class
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "document/ShallowAnalysis.h"

#include "document/InlayHintCollector.h"
#include "lsp/LspTypes.h"
#include "util/Converters.h"
#include "util/Logging.h"
#include "util/SlangExtensions.h"
#include <algorithm>
#include <fmt/format.h>
#include <memory>
#include <string_view>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTContext.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/Type.h"
#include "slang/diagnostics/AnalysisDiags.h"
#include "slang/driver/Driver.h"
#include "slang/parsing/Token.h"
#include "slang/parsing/TokenKind.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxFacts.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"
#include "slang/util/Util.h"
namespace server {
using namespace slang;

// Helper to check if two symbols represent the same semantic entity.
// After normalization in getSymbolAtToken, most symbols should have pointer equality.
// This fallback handles edge cases where different instance bodies create different
// symbol objects for the same source-level declaration (e.g., parameters).
static bool symbolsMatch(const ast::Symbol* a, const ast::Symbol* b) {
    if (!a || !b) {
        return false;
    }
    if (a == b) {
        return true;
    }
    // Match by location as a fallback
    if (a->location == b->location) {
        return true;
    }
    return false;
}
ShallowAnalysis::ShallowAnalysis(SourceManager& sourceManager, slang::BufferID buffer,
                                 std::shared_ptr<syntax::SyntaxTree> tree, slang::Bag options,
                                 const std::vector<std::shared_ptr<syntax::SyntaxTree>>& allTrees) :
    syntaxes(*tree), m_sourceManager(sourceManager), m_buffer(buffer), m_tree(tree),
    m_allTrees(allTrees), m_analysisOptions(options.getOrDefault<analysis::AnalysisOptions>()),
    m_symbolTreeVisitor(m_sourceManager), m_symbolIndexer(buffer) {

    if (!m_tree) {
        ERROR("DocumentAnalysis initialized with null syntax tree");
        return;
    }

    // Syntaxes are already indexed in the constructor

    auto path = m_sourceManager.getFullPath(m_buffer).string();

    if (syntaxes.collected.size() == 0) {
        ERROR("No syntaxes found in document {}", path);
    }

    // Index macros — last active definition for each name
    for (auto& macro : m_tree->getDefinedMacros()) {
        macros[macro->name.valueText()] = macro;
    }

    // Index macro references (usages and undefs) with their active definitions
    for (auto& ref : m_tree->getPreprocessorMetadata().macroRefs) {
        macroUsageDefinitions[ref.syntax] = ref.definition;
    }

    // Set up options for shallow compilation
    auto cOptions = options.getOrDefault<ast::CompilationOptions>();
    cOptions.flags |= ast::CompilationFlags::AllowTopLevelIfacePorts;
    cOptions.flags |= ast::CompilationFlags::CheckUninstantiated;
    cOptions.flags |= ast::CompilationFlags::AllowInvalidTop;
    // Check the edited module and two levels of instantiated children.
    cOptions.maxInstanceDepth = 3;

    // Add definitions from this tree (even if they aren't valid tops)
    cOptions.topModules.clear();
    m_compilation = std::make_unique<ast::Compilation>(cOptions);
    for (auto& depTree : m_allTrees) {
        m_compilation->addSyntaxTree(depTree);
    }

    // Elaborate and index
    // - token -> symbol defs
    // - syntax -> scopes
    m_compilation->getRoot().visit(m_symbolIndexer);
}

const Diagnostics& ShallowAnalysis::getSemanticDiagnostics() {
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

std::vector<lsp::DocumentSymbol> ShallowAnalysis::getDocSymbols() {
    if (!m_tree) {
        return {};
    }
    return m_symbolTreeVisitor.getSymbols(m_tree, true);
}

const parsing::Token* ShallowAnalysis::getTokenAt(SourceLocation loc) const {
    return syntaxes.getTokenAt(loc);
}

const syntax::NameSyntax* ShallowAnalysis::findNameSyntax(const syntax::SyntaxNode& node) const {
    if (node.parent == nullptr) {
        return nullptr;
    }
    // Untaken ifdefs go token -> tokenlist -> ifdef directive.
    // This should apply for other directives as well
    if (syntax::DirectiveSyntax::isKind(node.kind)) {
        return nullptr;
    }
    auto scopedParent = node.parent->as_if<syntax::ScopedNameSyntax>();
    if (scopedParent && scopedParent->right == &node) {
        return findNameSyntax(*node.parent);
    }
    else if (syntax::NameSyntax::isKind(node.kind)) {
        return &node.as<syntax::NameSyntax>();
    }

    return findNameSyntax(*node.parent);
}

bool ShallowAnalysis::isOverSelector(const parsing::Token* node,
                                     const ast::LookupResult& result) const {
    if (result.selectors.empty()) {
        return false;
    }

    for (const auto& sel : result.selectors) {
        if (auto member = std::get_if<ast::LookupResult::MemberSelector>(&sel)) {
            if (node->valueText().data() == member->name.data()) {
                return true;
            }
        }
        if (auto index = std::get_if<const syntax::ElementSelectSyntax*>(&sel)) {
            if ((*index)->sourceRange().contains(node->location())) {
                return true;
            }
        }
    }
    return false;
}

const ast::Symbol* ShallowAnalysis::handleScopedNameLookup(const syntax::NameSyntax* nameSyntax,
                                                           const ast::ASTContext& context) const {
    auto scopedParent = nameSyntax->parent->as_if<syntax::ScopedNameSyntax>();
    if (!scopedParent || nameSyntax->kind != syntax::SyntaxKind::IdentifierName) {
        return nullptr;
    }
    ast::LookupResult result;
    ast::Lookup::name(*scopedParent, context, ast::LookupFlags::None, result);
    if (!result.found) {
        return nullptr;
    }

    if (!result.path.empty()) {
        return result.path.front().symbol.get();
    }
    return nullptr;
}

const ast::Symbol* ShallowAnalysis::handleInterfacePortHeader(const parsing::Token* node,
                                                              const syntax::SyntaxNode* syntax,
                                                              const ast::Scope* scope) const {

    auto& header = syntax->parent->as<syntax::InterfacePortHeaderSyntax>();
    auto iface = m_compilation->tryGetDefinition(header.nameOrKeyword.valueText(), *scope);

    if (node == &header.nameOrKeyword) {
        return iface.definition;
    }

    if (!iface.definition || !header.modport) {
        return nullptr;
    }

    auto& idef = iface.definition->as<ast::DefinitionSymbol>();
    auto& inst = ast::InstanceSymbol::createDefault(*m_compilation, idef);

    // TODO: avoid creating a default instance each time
    return inst.body.lookupName(header.modport->member.valueText());
}

const ast::Scope* ShallowAnalysis::getScopeFromSym(const ast::Symbol* symbol) const {
    if (!symbol) {
        return nullptr;
    }

    if (symbol->isScope()) {
        return &symbol->as<ast::Scope>();
    }

    if (symbol->isType()) {
        auto& type = unwrapErrorType(symbol->as<ast::Type>());
        if (type.isScope()) {
            return &type.as<ast::Scope>();
        }
    }
    else if (ast::ValueSymbol::isKind(symbol->kind)) {
        auto& type = unwrapErrorType(symbol->as<ast::ValueSymbol>().getType());
        if (type.isScope()) {
            return &type.as<ast::Scope>();
        }
    }
    else if (ast::InstanceSymbol::isKind(symbol->kind)) {
        return &symbol->as<ast::InstanceSymbol>().body.as<ast::Scope>();
    }
    else if (ast::InterfacePortSymbol::isKind(symbol->kind)) {
        auto& port = symbol->as<ast::InterfacePortSymbol>();
        auto [connSym, modport] = port.getConnection();
        auto scope = getScopeFromSym(connSym);

        auto instanceArray = connSym ? connSym->as_if<ast::InstanceArraySymbol>() : nullptr;
        if ((!connSym || (instanceArray && instanceArray->elements.empty())) && port.interfaceDef) {
            auto parentScope = port.getParentScope();
            if (!parentScope)
                return nullptr;

            auto [it, inserted] = m_interfaceFallbackScopes.try_emplace(port.interfaceDef, nullptr);
            if (inserted) {
                auto& fallback = ast::InstanceSymbol::createDefault(parentScope->getCompilation(),
                                                                    *port.interfaceDef);
                fallback.name = "";
                it->second = &fallback.body;
            }
            scope = it->second;
            if (!port.modport.empty()) {
                auto fallbackModport = scope->find(port.modport);
                if (fallbackModport && fallbackModport->kind == ast::SymbolKind::Modport)
                    return &fallbackModport->as<ast::Scope>();
            }
            return scope;
        }

        if (!modport) {
            return scope;
        }

        if (scope) {
            auto realModport = scope->find(modport->name);
            if (realModport && realModport->kind == ast::SymbolKind::Modport) {
                return &realModport->as<ast::Scope>();
            }
        }
        return modport;
    }

    return nullptr;
}

const ast::Symbol* ShallowAnalysis::getAssignmentPatternTargetSymbol(
    const syntax::AssignmentPatternExpressionSyntax& pattern) const {
    auto getSymbolFromSyntax = [&](const syntax::SyntaxNode& target) {
        auto* token = syntaxes.getTokenAt(target.getLastToken().location());
        return getSymbolAtToken(token);
    };

    if (pattern.type)
        return getSymbolFromSyntax(*pattern.type);

    auto* parent = pattern.parent.get();
    if (!parent)
        return nullptr;

    if (auto* assignment = parent->as_if<syntax::BinaryExpressionSyntax>();
        assignment && assignment->right.get() == &pattern &&
        syntax::SyntaxFacts::isAssignmentOperator(assignment->kind)) {
        return getSymbolFromSyntax(*assignment->left);
    }

    if (auto* value = parent->as_if<syntax::EqualsValueClauseSyntax>();
        value && value->expr.get() == &pattern && value->parent) {
        if (auto* declarator = value->parent->as_if<syntax::DeclaratorSyntax>())
            return getSymbolAtToken(&declarator->name);
    }

    if (auto* item = parent->as_if<syntax::AssignmentPatternItemSyntax>();
        item && item->expr.get() == &pattern && item->parent && item->parent->parent) {
        auto* outerPattern =
            item->parent->parent->as_if<syntax::AssignmentPatternExpressionSyntax>();
        if (!outerPattern)
            return nullptr;

        auto* outerTarget = getAssignmentPatternTargetSymbol(*outerPattern);
        if (!outerTarget)
            return nullptr;

        const ast::Type* outerType = nullptr;
        if (outerTarget->isType())
            outerType = &outerTarget->as<ast::Type>();
        else if (ast::ValueSymbol::isKind(outerTarget->kind))
            outerType = &outerTarget->as<ast::ValueSymbol>().getType();

        if (outerType) {
            if (auto* elementType = unwrapErrorType(*outerType).getArrayElementType())
                return elementType;
        }

        auto* key = item->key->as_if<syntax::IdentifierNameSyntax>();
        if (!key)
            return nullptr;
        auto* outerScope = getScopeFromSym(outerTarget);
        return outerScope ? outerScope->find(key->identifier.valueText()) : nullptr;
    }

    const syntax::SyntaxNode* node = parent;
    if (auto* sequence = node->as_if<syntax::SimpleSequenceExprSyntax>();
        sequence && sequence->expr.get() == &pattern) {
        node = sequence->parent.get();
    }
    if (auto* property = node ? node->as_if<syntax::SimplePropertyExprSyntax>() : nullptr;
        property && property->expr.get() == parent) {
        node = property->parent.get();
    }

    auto* argument = node ? node->as_if<syntax::ArgumentSyntax>() : nullptr;
    auto* arguments = argument && argument->parent
                          ? argument->parent->as_if<syntax::ArgumentListSyntax>()
                          : nullptr;
    auto* invocation = arguments && arguments->parent
                           ? arguments->parent->as_if<syntax::InvocationExpressionSyntax>()
                           : nullptr;
    if (!argument || !arguments || !invocation)
        return nullptr;

    auto* targetToken = invocation->left->getLastTokenPtr();
    auto* target = getSymbolAtToken(targetToken);
    auto* subroutine = target ? target->as_if<ast::SubroutineSymbol>() : nullptr;
    if (!subroutine)
        return nullptr;

    auto formals = subroutine->getArguments();
    if (auto* named = argument->as_if<syntax::NamedArgumentSyntax>()) {
        auto formal = std::ranges::find(formals, named->name.valueText(),
                                        [](const auto* arg) { return arg->name; });
        return formal != formals.end() ? *formal : nullptr;
    }

    for (size_t index = 0; index < arguments->parameters.size(); index++) {
        if (arguments->parameters[index] == argument)
            return index < formals.size() ? formals[index] : nullptr;
    }
    return nullptr;
}

const ast::Scope* ShallowAnalysis::getAssignmentPatternTargetScope(
    const syntax::AssignmentPatternExpressionSyntax& pattern) const {
    return getScopeFromSym(getAssignmentPatternTargetSymbol(pattern));
}

const ast::Scope* ShallowAnalysis::getAssignmentPatternKeyScope(SourceLocation loc) const {
    return getAssignmentPatternScopeAt(loc, false);
}

const ast::Scope* ShallowAnalysis::getAssignmentPatternCompletionScope(SourceLocation loc) const {
    return getAssignmentPatternScopeAt(loc, true);
}

const ast::Scope* ShallowAnalysis::getAssignmentPatternScopeAt(SourceLocation loc,
                                                               bool allowIncompleteKey) const {
    auto* syntax = syntaxes.getSyntaxAt(loc);
    if (!syntax) {
        auto* previous = syntaxes.getTokenBefore(loc);
        syntax = syntaxes.getTokenParent(previous);
        if (!syntax)
            return nullptr;
    }

    const syntax::AssignmentPatternExpressionSyntax* expression = nullptr;
    const syntax::AssignmentPatternSyntax* pattern = nullptr;
    const syntax::SyntaxNode* child = syntax;
    bool isKey = false;
    bool isValue = false;

    for (auto* node = syntax; node; child = node, node = node->parent) {
        if (auto* item = node->as_if<syntax::AssignmentPatternItemSyntax>()) {
            isKey = child == item->key.get() || (child == item && loc <= item->colon.location());
            isValue = !isKey;
        }
        if (syntax::AssignmentPatternSyntax::isKind(node->kind))
            pattern = &node->as<syntax::AssignmentPatternSyntax>();
        if (auto* candidate = node->as_if<syntax::AssignmentPatternExpressionSyntax>()) {
            expression = candidate;
            break;
        }
    }

    if (!expression || !pattern || isValue)
        return nullptr;

    if (!isKey && allowIncompleteKey) {
        auto* previous = syntaxes.getTokenBefore(loc);
        isKey = previous && (previous->kind == parsing::TokenKind::ApostropheOpenBrace ||
                             previous->kind == parsing::TokenKind::OpenBrace ||
                             previous->kind == parsing::TokenKind::Comma);

        if (!isKey && pattern->kind == syntax::SyntaxKind::SimpleAssignmentPattern) {
            for (auto* node = syntax; node && node != pattern; node = node->parent) {
                if (node->parent.get() == pattern && syntax::ExpressionSyntax::isKind(node->kind)) {
                    isKey = true;
                    break;
                }
            }
        }
    }

    return isKey ? getAssignmentPatternTargetScope(*expression) : nullptr;
}

/// @brief Visitor that finds and stores a specific token and its syntax node at an offset
struct OffsetFinder {
    OffsetFinder(uint32_t targetOffset) : targetOffset(targetOffset) {}

    void visit(const syntax::SyntaxNode& node) {
        for (uint32_t i = 0; i < node.getChildCount(); i++) {
            auto child = node.childNode(i);
            if (child) {
                visit(*child);
            }
            else {
                auto token = const_cast<slang::syntax::SyntaxNode&>(node).childTokenPtr(i);
                if (token && token->location().offset() == targetOffset) {
                    foundSyntax = &node;
                    foundToken = token;
                    return;
                }
            }
        }
    }

    uint32_t targetOffset;
    const syntax::SyntaxNode* foundSyntax = nullptr;
    const parsing::Token* foundToken = nullptr;
};

const ShallowAnalysis::GenvarElaboration* ShallowAnalysis::getGenvarElaborationAtToken(
    const parsing::Token* declTok) const {
    if (!declTok)
        return nullptr;

    auto* syntax = syntaxes.getTokenParent(declTok);
    if (!syntax)
        return nullptr;

    while (syntax && syntax->kind != syntax::SyntaxKind::LoopGenerate)
        syntax = syntax->parent;
    if (!syntax)
        return nullptr;

    auto& loop = syntax->as<syntax::LoopGenerateSyntax>();
    auto& elaboration = m_genvarElaborations[&loop];
    if (elaboration.initialized)
        return elaboration.source ? &elaboration : nullptr;
    elaboration.initialized = true;

    for (auto* scope : m_symbolIndexer.getScopesForSyntax(loop)) {
        auto* array = scope->asSymbol().as_if<ast::GenerateBlockArraySymbol>();
        if (!array)
            continue;

        auto* genvar = array->find(loop.identifier.valueText());
        if ((!genvar || genvar->kind != ast::SymbolKind::Genvar) && array->getParentScope())
            genvar = array->getParentScope()->find(loop.identifier.valueText());
        if (!genvar || genvar->kind != ast::SymbolKind::Genvar)
            continue;

        if (!elaboration.source)
            elaboration.source = &genvar->as<ast::GenvarSymbol>();

        for (auto* entry : array->entries) {
            auto* symbol = entry->find(loop.identifier.valueText());
            auto* parameter = symbol ? symbol->as_if<ast::ParameterSymbol>() : nullptr;
            if (parameter && parameter->isFromGenvar() &&
                std::ranges::find(elaboration.parameters, parameter) ==
                    elaboration.parameters.end()) {
                elaboration.parameters.push_back(parameter);
            }
        }
    }

    return elaboration.source ? &elaboration : nullptr;
}

std::span<const ast::ParameterSymbol* const> ShallowAnalysis::getGenvarIterationParametersAtToken(
    const parsing::Token* declTok) const {
    auto* elaboration = getGenvarElaborationAtToken(declTok);
    if (!elaboration)
        return {};
    return {elaboration->parameters.data(), elaboration->parameters.size()};
}

slang::SmallVector<const ast::Symbol*, 2> ShallowAnalysis::getSymbolsAtToken(
    const parsing::Token* declTok) const {
    slang::SmallVector<const ast::Symbol*, 2> symbols;
    auto addSymbol = [&](const ast::Symbol* symbol) {
        auto addOne = [&](const ast::Symbol* candidate) {
            if (!candidate)
                return;

            if (candidate->kind == ast::SymbolKind::Genvar) {
                if (auto* elaboration = getGenvarElaborationAtToken(declTok))
                    candidate = elaboration->source;
            }

            if (std::ranges::find(symbols, candidate) == symbols.end())
                symbols.push_back(candidate);
        };

        if (!symbol)
            return;

        switch (symbol->kind) {
            case ast::SymbolKind::InstanceBody:
                addOne(&symbol->as<ast::InstanceBodySymbol>().getDefinition());
                break;
            case ast::SymbolKind::Port: {
                // The internal symbol carries the declared type and is the symbol used by
                // references inside the instance.
                auto& port = symbol->as<ast::PortSymbol>();
                addOne(port.internalSymbol ? port.internalSymbol : symbol);
                break;
            }
            case ast::SymbolKind::ModportPort: {
                // A named modport port has both an underlying typed symbol and a directional
                // declaration in the modport.
                auto& port = symbol->as<ast::ModportPortSymbol>();
                addOne(symbol);
                addOne(port.internalSymbol);
                break;
            }
            case ast::SymbolKind::MethodPrototype: {
                auto& prototype = symbol->as<ast::MethodPrototypeSymbol>();
                addOne(symbol);
                if (symbol->getSyntax() &&
                    symbol->getSyntax()->kind == syntax::SyntaxKind::ModportSubroutinePort) {
                    addOne(prototype.getSubroutine());
                }
                break;
            }
            default:
                addOne(symbol);
                break;
        }
    };

    if (!declTok) {
        return symbols;
    }

    auto syntax = syntaxes.getTokenParent(declTok);
    // Note: SuperHandle nodes can cause issues in symbol lookup
    if (!syntax || syntax->kind == syntax::SyntaxKind::SuperHandle) {
        return symbols;
    }

    // Handle macro args
    std::shared_ptr<syntax::SyntaxTree> tokTree; // syntax needs to live for this function
    if (syntax->kind == syntax::SyntaxKind::MacroActualArgument) {
        // parse the token list, and use those name syntaxes for lookups
        // TODO: be more precise; handle args that produce lhs ids
        // they may not refer to
        // anything, like if being used to make a lhs id name.

        auto& macroArgSyntax = syntax->as<syntax::MacroActualArgumentSyntax>();

        auto firstToken = macroArgSyntax.getFirstToken();
        auto lastToken = macroArgSyntax.getLastToken();
        size_t startOffset = firstToken.location().offset();
        size_t endOffset = lastToken.location().offset() + lastToken.rawText().size();
        auto macroArgText = m_sourceManager.getSourceText(SourceRange{
            SourceLocation(m_buffer, startOffset), SourceLocation(m_buffer, endOffset)});

        // These will overwrite the same assigned source, but it's ok since they are temporary,
        // and the source manager should be thread safe (for when we do threaded async)
        tokTree = syntax::SyntaxTree::fromText(macroArgText, m_sourceManager);
        tokTree->root().parent = macroArgSyntax.parent;
        OffsetFinder visitor(declTok->location().offset() -
                             macroArgSyntax.getFirstToken().location().offset());
        visitor.visit(tokTree->root());
        if (visitor.foundSyntax && visitor.foundToken) {
            syntax = visitor.foundSyntax;
            declTok = visitor.foundToken;
        }
        else {
            ERROR("Failed to grab syntax/token pair for macro arg '{}'", declTok->rawText());
        }
        // set declTok to the new one. they should have the same raw text
    }
    else if (syntax->kind == syntax::SyntaxKind::PackageExportDeclaration ||
             syntax->kind == syntax::SyntaxKind::PackageImportItem) {
        auto pkg = m_compilation->getPackage(syntax->getFirstToken().valueText());
        if (!pkg) {
            return symbols;
        }
        if (syntax->getFirstToken() == *declTok) {
            addSymbol(pkg);
        }
        else {
            addSymbol(pkg->find(declTok->valueText()));
        }
        return symbols;
    }
    else if (auto* patternScope = getAssignmentPatternKeyScope(declTok->location())) {
        auto* member = patternScope->find(declTok->valueText());
        if (member && ast::FieldSymbol::isKind(member->kind)) {
            addSymbol(member);
            return symbols;
        }
    }
    else if (auto indexed = m_symbolIndexer.getSymbols(declTok); !indexed.empty()) {
        if (syntax->kind == syntax::SyntaxKind::NamedPortConnection &&
            !syntax->as<syntax::NamedPortConnectionSyntax>().openParen) {
            if (indexed.size() > 1) {
                symbols.append(indexed.begin(), indexed.end());
            }
            else {
                for (auto* scope : m_symbolIndexer.getScopesForSyntax(*syntax))
                    addSymbol(scope->lookupName(declTok->valueText()));
                addSymbol(indexed.front());
            }
            return symbols;
        }

        // Keep the selected facade kind while retaining distinct elaborations of that kind.
        auto legacyKind = indexed.back()->kind;
        for (auto* symbol : indexed) {
            if (symbol->kind == legacyKind)
                addSymbol(symbol);
        }
        return symbols;
    }

    auto recoverMembers = [&](const syntax::SyntaxNode& left, std::string_view name) {
        auto leftToken = syntaxes.getTokenAt(left.getLastToken().location());
        for (auto* receiver : getSymbolsAtToken(leftToken)) {
            if (auto* receiverScope = getScopeFromSym(receiver))
                addSymbol(receiverScope->find(name));
        }
    };

    if (syntax->kind == syntax::SyntaxKind::MemberAccessExpression) {
        auto& access = syntax->as<syntax::MemberAccessExpressionSyntax>();
        if (access.name == *declTok) {
            recoverMembers(*access.left, access.name.valueText());
            if (!symbols.empty())
                return symbols;
        }
    }
    else if (syntax->kind == syntax::SyntaxKind::IdentifierName && syntax->parent) {
        auto scoped = syntax->parent->as_if<syntax::ScopedNameSyntax>();
        if (scoped && scoped->right == syntax &&
            scoped->separator.kind == parsing::TokenKind::Dot) {
            recoverMembers(*scoped->left, declTok->valueText());
            if (!symbols.empty())
                return symbols;
        }
    }

    auto scopes = m_symbolIndexer.getScopesForSyntax(*syntax);

    if (syntax->kind == syntax::SyntaxKind::LoopGenerate &&
        syntax->as<syntax::LoopGenerateSyntax>().identifier == *declTok) {
        if (auto* elaboration = getGenvarElaborationAtToken(declTok))
            addSymbol(elaboration->source);
        if (!symbols.empty())
            return symbols;
    }

    if (scopes.empty()) {
        INFO("No scope found for syntax {}, using root scope", syntax->toString());
        scopes.push_back(&m_compilation->getRoot().as<ast::Scope>());
    }

    auto resolveInScope = [&](const ast::Scope* scope) -> const ast::Symbol* {
        // Perform name lookup; this should be most gotos.
        auto nameSyntax = findNameSyntax(*syntax);
        if (!nameSyntax) {
            if (declTok->kind != parsing::TokenKind::Identifier ||
                syntax->kind == syntax::SyntaxKind::AttributeSpec) {
                return nullptr;
            }
            if (syntax->kind == syntax::SyntaxKind::DotMemberClause)
                return handleInterfacePortHeader(declTok, syntax, scope);
            if (auto def = m_compilation->tryGetDefinition(declTok->valueText(), *scope);
                def.definition) {
                return def.definition;
            }
            return m_compilation->getPackage(declTok->valueText());
        }

        auto scopedName = nameSyntax->as_if<slang::syntax::ScopedNameSyntax>();
        if (scopedName && scopedName->separator == *declTok) {
            return nullptr;
        }

        ast::ASTContext context(*scope, ast::LookupLocation::max);
        ast::LookupResult result;
        ast::Lookup::name(*nameSyntax, context, ast::LookupFlags::None, result);
        if (result.found) {
            if (isOverSelector(declTok, result)) {
                const slang::ast::Symbol* cur = result.found;
                size_t remainingInterfaceDimensions = 0;
                bool hasInterfaceDimensions = false;

                auto loadInterfaceDimensions = [&] {
                    if (hasInterfaceDimensions)
                        return true;

                    auto portSyntax = cur->getSyntax();
                    auto declarator = portSyntax ? portSyntax->as_if<syntax::DeclaratorSyntax>()
                                                 : nullptr;
                    if (!declarator)
                        return false;

                    remainingInterfaceDimensions = declarator->dimensions.size();
                    hasInterfaceDimensions = true;
                    return true;
                };

                // Proper selector resolution is in
                // Expression::bindLookupResult, however that modifies the compilation at the
                // moment
                for (auto& sel : result.selectors) {
                    if (auto member = std::get_if<ast::LookupResult::MemberSelector>(&sel)) {
                        if (ast::InterfacePortSymbol::isKind(cur->kind) &&
                            (!loadInterfaceDimensions() || remainingInterfaceDimensions != 0)) {
                            return nullptr;
                        }

                        const ast::Scope* scope = getScopeFromSym(cur);
                        if (!scope) {
                            INFO("No scope found for sym {} : {}", cur->getHierarchicalPath(),
                                 toString(cur->kind));
                            return nullptr;
                        }
                        cur = scope->find(member->name);
                        hasInterfaceDimensions = false;

                        if (member->nameRange == declTok->range()) {
                            return cur;
                        }
                    }
                    else {
                        if (ast::InterfacePortSymbol::isKind(cur->kind)) {
                            // Preserve the port while consuming unevaluated interface dimensions.
                            auto* select = std::get<const syntax::ElementSelectSyntax*>(sel);
                            if (!loadInterfaceDimensions() || remainingInterfaceDimensions == 0 ||
                                !select->selector ||
                                select->selector->kind != syntax::SyntaxKind::BitSelect) {
                                return nullptr;
                            }
                            remainingInterfaceDimensions--;
                            continue;
                        }

                        const ast::Type* type = nullptr;
                        if (cur->isType()) {
                            type = &cur->as<ast::Type>();
                        }
                        else if (cur->isValue()) {
                            type = &cur->as<ast::ValueSymbol>().getType();
                        }

                        if (type && type->isArray()) {
                            cur = type->getArrayElementType();
                        }
                        else {
                            return nullptr;
                        }
                        hasInterfaceDimensions = false;
                    }
                    if (!cur) {
                        WARN("No members found in scope {}",
                             scope->asSymbol().getHierarchicalPath());
                        return nullptr;
                    }
                }
                if (hasInterfaceDimensions && remainingInterfaceDimensions != 0)
                    return nullptr;
                return cur;
            }
            // with IdentifierSelectNameSyntax (instance arrays) result.found will be the final
            // element, when we may want the array. In the case of the selectors being the token, we
            // do want the element for completions
            if (syntax->kind == syntax::SyntaxKind::IdentifierSelectName &&
                (result.found->kind == slang::ast::SymbolKind::Instance ||
                 result.found->kind == slang::ast::SymbolKind::GenerateBlock)) {
                auto& idSelect = syntax->as<syntax::IdentifierSelectNameSyntax>();
                if (idSelect.identifier == *declTok) {
                    return result.path.at(result.path.size() - 1 - idSelect.selectors.size())
                        .symbol.get();
                }
            }
            return result.found;
        }

        // Try scoped name lookup with the same flags
        if (auto scopedResult = handleScopedNameLookup(nameSyntax, context)) {
            return scopedResult;
        }

        if (declTok->kind != parsing::TokenKind::Identifier ||
            syntax->kind == syntax::SyntaxKind::AttributeSpec) {
            return nullptr;
        }

        if (syntax->kind == syntax::SyntaxKind::DotMemberClause) {
            return handleInterfacePortHeader(declTok, syntax, scope);
        }
        // Try getting a definition as a last resort.
        auto def = m_compilation->tryGetDefinition(declTok->valueText(), *scope);
        if (def.definition) {
            return def.definition;
        }

        return m_compilation->getPackage(declTok->valueText());
    };

    for (auto* scope : scopes)
        addSymbol(resolveInScope(scope));
    return symbols;
}

const ast::Symbol* ShallowAnalysis::getSymbolAtToken(const parsing::Token* declTok) const {
    auto symbols = getSymbolsAtToken(declTok);
    if (declTok) {
        auto* syntaxNode = syntaxes.getTokenParent(declTok);
        if (syntaxNode && syntaxNode->kind == syntax::SyntaxKind::NamedPortConnection &&
            !syntaxNode->as<syntax::NamedPortConnectionSyntax>().openParen) {
            if (symbols.size() > 1)
                return symbols[1];
            return symbols.empty() ? nullptr : symbols.front();
        }
    }
    if (auto it = std::ranges::find_if(symbols,
                                       [](const ast::Symbol* symbol) {
                                           return symbol->kind == ast::SymbolKind::ModportPort;
                                       });
        it != symbols.end()) {
        return *it;
    }
    return symbols.empty() ? nullptr : symbols.back();
}

const ast::Symbol* ShallowAnalysis::getDefinition(std::string_view name) const {
    auto def = m_compilation->tryGetDefinition(name, m_compilation->getRoot());
    return def.definition;
}

const ast::Symbol* ShallowAnalysis::getSymbolAt(SourceLocation loc) const {
    auto node = syntaxes.getWordTokenAt(loc);
    if (!node) {
        return nullptr;
    }
    return getSymbolAtToken(node);
}

const ast::Scope* ShallowAnalysis::getScopeAt(SourceLocation loc) const {
    auto syntax = syntaxes.getSyntaxAt(loc);
    if (!syntax) {
        return nullptr;
    }
    return m_symbolIndexer.getScopeForSyntax(*syntax);
}

std::vector<lsp::InlayHint> ShallowAnalysis::getInlayHints(lsp::Range range,
                                                           const Config::InlayHints& config) {
    // query inlay hints within range
    InlayHintCollector collector(*this, range, config);
    collector.collectHints();
    return collector.result;
}

void ShallowAnalysis::addLocalReferences(std::vector<lsp::Location>& references,
                                         SourceLocation targetLocation,
                                         std::string_view targetName) const {
    // Get the token and symbol at the target location (may be in a different buffer)

    auto it = syntaxes.collected.begin();
    auto end = syntaxes.collected.end();

    const ast::Symbol* targetSymbol = nullptr;
    // First loop: find the target symbol by matching the token location
    for (; it != end; ++it) {
        const auto* token = *it;
        if (token->valueText() != targetName) {
            continue;
        }

        // Check if this is the token at the target location
        auto tokenSymbols = getSymbolsAtToken(token);
        auto targetIt = std::ranges::find_if(tokenSymbols, [&](const ast::Symbol* symbol) {
            return symbol->location == targetLocation;
        });
        if (targetIt != tokenSymbols.end()) {
            targetSymbol = *targetIt;
            break;
        }
    }

    if (!targetSymbol) {
        return;
    }

    // Second loop: continue from where we left off to find all references
    auto path = m_sourceManager.getFullPath(m_buffer);
    for (; it != end; ++it) {
        const auto* token = *it;
        if (token->valueText() != targetName) {
            continue;
        }

        auto tokenSymbols = getSymbolsAtToken(token);
        if (std::ranges::none_of(tokenSymbols, [&](const ast::Symbol* symbol) {
                return symbolsMatch(symbol, targetSymbol);
            })) {
            continue;
        }

        // Skip tokens that are part of a module declaration's named block clause
        // ie:
        // module MODULE_NAME;
        //   ...
        // endmodule : MODULE_NAME <-- Skip this token
        //
        // However it does not skip references for begin...end blocks.
        auto* tokenParent = syntaxes.getTokenParent(token);
        auto parentKind = tokenParent && tokenParent->parent ? tokenParent->parent->kind
                                                             : syntax::SyntaxKind::Unknown;
        if (tokenParent && tokenParent->kind == syntax::SyntaxKind::NamedBlockClause &&
            parentKind != syntax::SyntaxKind::SequentialBlockStatement &&
            parentKind != syntax::SyntaxKind::ParallelBlockStatement &&
            parentKind != syntax::SyntaxKind::GenerateBlock) {
            continue;
        }

        references.push_back(lsp::Location{
            .uri = URI::fromFile(path),
            .range = toRange(token->range(), m_sourceManager),
        });
    }
}

std::vector<lsp::DocumentLink> ShallowAnalysis::getDocLinks() const {
    std::vector<lsp::DocumentLink> links;
    for (auto& inc : m_tree->getIncludeDirectives()) {
        // check buffer is in ours
        if (inc.syntax->fileName.location().buffer() != m_buffer) {
            continue;
        }
        links.push_back(lsp::DocumentLink{
            .range = toRange(inc.syntax->fileName.range(), m_sourceManager),
            .target = URI::fromFile(m_sourceManager.getFullPath(inc.buffer.id)),
        });
    }
    return links;
}

bool ShallowAnalysis::hasValidBuffers() {
    for (auto& tree : m_allTrees) {
        if (!server::hasValidBuffers(m_sourceManager, tree)) {
            return false;
        }
    }
    return true;
}

markup::Paragraph ShallowAnalysis::getDebugHover(const SourceLocation& loc) const {
    markup::Paragraph para;
    auto tok = syntaxes.getTokenAt(loc);
    // Token info header
    if (tok) {
        para.appendBold("Token:").appendCode(toString(tok->kind)).newLine();
    }

    // Walk up the syntax tree
    auto node = syntaxes.getSyntaxAt(loc);
    for (auto nodePtr = node; nodePtr; nodePtr = nodePtr->parent) {
        // In case of bad memory
        if (nodePtr->kind > syntax::SyntaxKind::XorAssignmentExpression) {
            break;
        }

        auto kindStr = toString(nodePtr->kind);
        if (kindStr.empty()) {
            para.appendText("Unknown syntax kind");
            break;
        }

        // Show syntax kind
        para.appendBold(kindStr).appendText(": ");

        // Show source text preview
        auto range = nodePtr->sourceRange();
        if (range != SourceRange::NoLocation) {
            auto svText = nodePtr->toString();
            if (svText.size() > 40) {
                svText = svText.substr(0, 18) + "..." + svText.substr(svText.size() - 18);
            }
            para.appendCode(svText);
        }
        else {
            para.appendText("(no source)");
        }
        para.newLine();

        // Check if we've reached a symbol
        auto sym = m_symbolIndexer.getSymbol(nodePtr);
        if (sym) {
            para.appendText("  - ")
                .appendText(toString(sym->kind))
                .appendText(" ")
                .appendCode(sym->name)
                .newLine()
                .newLine();
        }
    }

    return para;
}

Diagnostics ShallowAnalysis::getAnalysisDiags() {
    getAnalysisManager();

    if (!m_cachedAnalysisDiags) {
        return {};
    }

    return *m_cachedAnalysisDiags;
}

const slang::analysis::AnalysisManager* ShallowAnalysis::getAnalysisManager() {
    if (m_driverAnalysis) {
        return m_driverAnalysis.get();
    }

    (void)getSemanticDiagnostics();

    if (!m_compilation || m_compilation->getRoot().topInstances.empty()) {
        m_cachedAnalysisDiags = Diagnostics{};
        return nullptr;
    }

    auto manager = std::make_unique<slang::analysis::AnalysisManager>(m_analysisOptions);

    m_compilation->freeze();
    manager->analyze(*m_compilation);
    m_compilation->unfreeze();

    // filter out unused def/decl diags, since shallow analysis will likely not have all references.
    m_cachedAnalysisDiags = manager->getDiagnostics().filter(
        {diag::UnusedDefinition, diag::UnusedPackageParameter, diag::UnusedPackageSubroutine,
         diag::UnusedPackageTypedef, diag::UnusedPackageVar});

    m_driverAnalysis = std::move(manager);

    return m_driverAnalysis.get();
}

std::vector<const slang::analysis::ValueDriver*> ShallowAnalysis::getDrivers(
    const slang::ast::ValueSymbol& symbol) {
    auto* manager = getAnalysisManager();
    if (!manager) {
        return {};
    }

    return manager->getDrivers(symbol);
}

} // namespace server
