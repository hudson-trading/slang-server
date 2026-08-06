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
#include <array>
#include <fmt/format.h>
#include <memory>
#include <string_view>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTContext.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/Type.h"
#include "slang/diagnostics/AnalysisDiags.h"
#include "slang/driver/Driver.h"
#include "slang/parsing/LexerFacts.h"
#include "slang/parsing/Token.h"
#include "slang/parsing/TokenKind.h"
#include "slang/syntax/AllSyntax.h"
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

namespace {
enum class SemanticTokenTypeIndex : lsp::uint {
    Namespace = 0,
    Type = 1,
    Class = 2,
    Parameter = 3,
    Variable = 4,
    Property = 5,
    EnumMember = 6,
    Function = 7,
    Macro = 8,
    Keyword = 9,
    String = 10,
    Number = 11,
    Operator = 12,
};

enum class SemanticTokenModifierIndex : lsp::uint {
    Declaration = 0,
    Readonly = 1,
    DefaultLibrary = 2,
};

constexpr std::array<std::string_view, 13> kSemanticTokenTypes = {
    "namespace", "type",  "class",   "parameter", "variable", "property", "enumMember",
    "function",  "macro", "keyword", "string",    "number",   "operator",
};

constexpr std::array<std::string_view, 3> kSemanticTokenModifiers = {
    "declaration",
    "readonly",
    "defaultLibrary",
};

struct SemanticClassification {
    lsp::uint tokenType = 0;
    lsp::uint tokenModifiers = 0;
};

constexpr lsp::uint toModifierMask(SemanticTokenModifierIndex modifier) {
    return 1u << static_cast<lsp::uint>(modifier);
}

void addModifier(lsp::uint& mask, SemanticTokenModifierIndex modifier) {
    mask |= toModifierMask(modifier);
}

bool isStringToken(parsing::TokenKind kind) {
    return kind == parsing::TokenKind::StringLiteral;
}

bool isNumberToken(parsing::TokenKind kind) {
    switch (kind) {
        case parsing::TokenKind::IntegerLiteral:
        case parsing::TokenKind::IntegerBase:
        case parsing::TokenKind::UnbasedUnsizedLiteral:
        case parsing::TokenKind::RealLiteral:
        case parsing::TokenKind::TimeLiteral:
            return true;
        default:
            return false;
    }
}

bool isMacroToken(parsing::TokenKind kind) {
    switch (kind) {
        case parsing::TokenKind::Directive:
        case parsing::TokenKind::MacroUsage:
        case parsing::TokenKind::MacroQuote:
        case parsing::TokenKind::MacroTripleQuote:
        case parsing::TokenKind::MacroEscapedQuote:
        case parsing::TokenKind::MacroPaste:
        case parsing::TokenKind::EmptyMacroArgument:
            return true;
        default:
            return false;
    }
}

bool isOperatorToken(parsing::TokenKind kind) {
    switch (kind) {
        case parsing::TokenKind::ColonEquals:
        case parsing::TokenKind::ColonSlash:
        case parsing::TokenKind::DoubleColon:
        case parsing::TokenKind::Slash:
        case parsing::TokenKind::Star:
        case parsing::TokenKind::DoubleStar:
        case parsing::TokenKind::StarArrow:
        case parsing::TokenKind::Plus:
        case parsing::TokenKind::DoublePlus:
        case parsing::TokenKind::PlusColon:
        case parsing::TokenKind::PlusDivMinus:
        case parsing::TokenKind::PlusModMinus:
        case parsing::TokenKind::Minus:
        case parsing::TokenKind::DoubleMinus:
        case parsing::TokenKind::MinusColon:
        case parsing::TokenKind::MinusArrow:
        case parsing::TokenKind::MinusDoubleArrow:
        case parsing::TokenKind::Tilde:
        case parsing::TokenKind::TildeAnd:
        case parsing::TokenKind::TildeOr:
        case parsing::TokenKind::TildeXor:
        case parsing::TokenKind::Question:
        case parsing::TokenKind::Hash:
        case parsing::TokenKind::DoubleHash:
        case parsing::TokenKind::HashMinusHash:
        case parsing::TokenKind::HashEqualsHash:
        case parsing::TokenKind::Xor:
        case parsing::TokenKind::XorTilde:
        case parsing::TokenKind::Equals:
        case parsing::TokenKind::DoubleEquals:
        case parsing::TokenKind::DoubleEqualsQuestion:
        case parsing::TokenKind::TripleEquals:
        case parsing::TokenKind::EqualsArrow:
        case parsing::TokenKind::PlusEqual:
        case parsing::TokenKind::MinusEqual:
        case parsing::TokenKind::SlashEqual:
        case parsing::TokenKind::StarEqual:
        case parsing::TokenKind::AndEqual:
        case parsing::TokenKind::OrEqual:
        case parsing::TokenKind::PercentEqual:
        case parsing::TokenKind::XorEqual:
        case parsing::TokenKind::LeftShiftEqual:
        case parsing::TokenKind::TripleLeftShiftEqual:
        case parsing::TokenKind::RightShiftEqual:
        case parsing::TokenKind::TripleRightShiftEqual:
        case parsing::TokenKind::LeftShift:
        case parsing::TokenKind::RightShift:
        case parsing::TokenKind::TripleLeftShift:
        case parsing::TokenKind::TripleRightShift:
        case parsing::TokenKind::Exclamation:
        case parsing::TokenKind::ExclamationEquals:
        case parsing::TokenKind::ExclamationEqualsQuestion:
        case parsing::TokenKind::ExclamationDoubleEquals:
        case parsing::TokenKind::Percent:
        case parsing::TokenKind::LessThan:
        case parsing::TokenKind::LessThanEquals:
        case parsing::TokenKind::LessThanMinusArrow:
        case parsing::TokenKind::GreaterThan:
        case parsing::TokenKind::GreaterThanEquals:
        case parsing::TokenKind::Or:
        case parsing::TokenKind::DoubleOr:
        case parsing::TokenKind::OrMinusArrow:
        case parsing::TokenKind::OrEqualsArrow:
        case parsing::TokenKind::At:
        case parsing::TokenKind::DoubleAt:
        case parsing::TokenKind::And:
        case parsing::TokenKind::DoubleAnd:
        case parsing::TokenKind::TripleAnd:
            return true;
        default:
            return false;
    }
}

std::optional<lsp::uint> tokenTypeFromSymbolKind(ast::SymbolKind kind) {
    switch (kind) {
        case ast::SymbolKind::Package:
        case ast::SymbolKind::ExplicitImport:
        case ast::SymbolKind::WildcardImport:
            return static_cast<lsp::uint>(SemanticTokenTypeIndex::Namespace);
        case ast::SymbolKind::TypeAlias:
        case ast::SymbolKind::NetType:
        case ast::SymbolKind::ClassType:
        case ast::SymbolKind::EnumType:
        case ast::SymbolKind::PackedStructType:
        case ast::SymbolKind::UnpackedStructType:
        case ast::SymbolKind::PackedUnionType:
        case ast::SymbolKind::UnpackedUnionType:
        case ast::SymbolKind::TypeRefType:
            return static_cast<lsp::uint>(SemanticTokenTypeIndex::Type);
        case ast::SymbolKind::Definition:
        case ast::SymbolKind::Instance:
        case ast::SymbolKind::InstanceBody:
        case ast::SymbolKind::InstanceArray:
        case ast::SymbolKind::Checker:
        case ast::SymbolKind::CheckerInstance:
        case ast::SymbolKind::CheckerInstanceBody:
            return static_cast<lsp::uint>(SemanticTokenTypeIndex::Class);
        case ast::SymbolKind::Parameter:
        case ast::SymbolKind::TypeParameter:
        case ast::SymbolKind::Port:
        case ast::SymbolKind::MultiPort:
        case ast::SymbolKind::InterfacePort:
        case ast::SymbolKind::FormalArgument:
        case ast::SymbolKind::Specparam:
            return static_cast<lsp::uint>(SemanticTokenTypeIndex::Parameter);
        case ast::SymbolKind::Variable:
        case ast::SymbolKind::Net:
        case ast::SymbolKind::Genvar:
        case ast::SymbolKind::LocalAssertionVar:
        case ast::SymbolKind::PatternVar:
        case ast::SymbolKind::Iterator:
            return static_cast<lsp::uint>(SemanticTokenTypeIndex::Variable);
        case ast::SymbolKind::Field:
        case ast::SymbolKind::ClassProperty:
        case ast::SymbolKind::ModportPort:
        case ast::SymbolKind::ClockVar:
            return static_cast<lsp::uint>(SemanticTokenTypeIndex::Property);
        case ast::SymbolKind::EnumValue:
            return static_cast<lsp::uint>(SemanticTokenTypeIndex::EnumMember);
        case ast::SymbolKind::Subroutine:
        case ast::SymbolKind::MethodPrototype:
            return static_cast<lsp::uint>(SemanticTokenTypeIndex::Function);
        default:
            return std::nullopt;
    }
}

bool symbolIsReadonly(ast::SymbolKind kind) {
    switch (kind) {
        case ast::SymbolKind::Parameter:
        case ast::SymbolKind::TypeParameter:
        case ast::SymbolKind::Specparam:
            return true;
        default:
            return false;
    }
}

bool isTransparentSyntaxKind(syntax::SyntaxKind kind) {
    switch (kind) {
        case syntax::SyntaxKind::IdentifierName:
        case syntax::SyntaxKind::EmptyIdentifierName:
        case syntax::SyntaxKind::Declarator:
        case syntax::SyntaxKind::ParameterPortList:
        case syntax::SyntaxKind::AnsiPortList:
        case syntax::SyntaxKind::NonAnsiPortList:
        case syntax::SyntaxKind::WildcardPortList:
        case syntax::SyntaxKind::FunctionPortList:
            return true;
        default:
            return false;
    }
}

std::optional<lsp::uint> tokenTypeFromSyntaxContext(const parsing::Token* token,
                                                    const syntax::SyntaxNode* syntax) {
    for (auto* node = syntax; node; node = node->parent) {
        if (isTransparentSyntaxKind(node->kind)) {
            continue;
        }

        switch (node->kind) {
            case syntax::SyntaxKind::ParameterDeclaration:
            case syntax::SyntaxKind::ParameterDeclarationStatement:
            case syntax::SyntaxKind::TypeParameterDeclaration:
            case syntax::SyntaxKind::TypeAssignment:
            case syntax::SyntaxKind::SpecparamDeclarator:
            case syntax::SyntaxKind::PortDeclaration:
            case syntax::SyntaxKind::ImplicitAnsiPort:
            case syntax::SyntaxKind::ExplicitAnsiPort:
            case syntax::SyntaxKind::InterfacePortHeader:
            case syntax::SyntaxKind::NetPortHeader:
            case syntax::SyntaxKind::VariablePortHeader:
                return static_cast<lsp::uint>(SemanticTokenTypeIndex::Parameter);

            case syntax::SyntaxKind::TypedefDeclaration:
            case syntax::SyntaxKind::ForwardTypedefDeclaration:
            case syntax::SyntaxKind::NamedType:
            case syntax::SyntaxKind::EnumType:
                return static_cast<lsp::uint>(SemanticTokenTypeIndex::Type);

            case syntax::SyntaxKind::StructUnionMember:
            case syntax::SyntaxKind::ClassPropertyDeclaration:
                return static_cast<lsp::uint>(SemanticTokenTypeIndex::Property);

            case syntax::SyntaxKind::FunctionPrototype:
            case syntax::SyntaxKind::FunctionDeclaration:
            case syntax::SyntaxKind::TaskDeclaration:
            case syntax::SyntaxKind::ClassMethodDeclaration:
            case syntax::SyntaxKind::ClassMethodPrototype:
                return static_cast<lsp::uint>(SemanticTokenTypeIndex::Function);

            case syntax::SyntaxKind::PackageHeader: {
                auto& header = node->as<syntax::ModuleHeaderSyntax>();
                if (header.name == *token) {
                    return static_cast<lsp::uint>(SemanticTokenTypeIndex::Namespace);
                }
                break;
            }
            case syntax::SyntaxKind::ModuleHeader:
            case syntax::SyntaxKind::InterfaceHeader:
            case syntax::SyntaxKind::ProgramHeader: {
                auto& header = node->as<syntax::ModuleHeaderSyntax>();
                if (header.name == *token) {
                    return static_cast<lsp::uint>(SemanticTokenTypeIndex::Class);
                }
                break;
            }
            case syntax::SyntaxKind::ClassDeclaration: {
                auto& decl = node->as<syntax::ClassDeclarationSyntax>();
                if (decl.name == *token) {
                    return static_cast<lsp::uint>(SemanticTokenTypeIndex::Class);
                }
                break;
            }
            case syntax::SyntaxKind::HierarchyInstantiation:
            case syntax::SyntaxKind::PrimitiveInstantiation:
            case syntax::SyntaxKind::ClassName:
                return static_cast<lsp::uint>(SemanticTokenTypeIndex::Class);

            case syntax::SyntaxKind::DataDeclaration:
            case syntax::SyntaxKind::CheckerDataDeclaration:
            case syntax::SyntaxKind::NetDeclaration:
            case syntax::SyntaxKind::UserDefinedNetDeclaration:
            case syntax::SyntaxKind::LocalVariableDeclaration:
            case syntax::SyntaxKind::ForVariableDeclaration:
            case syntax::SyntaxKind::GenvarDeclaration:
            case syntax::SyntaxKind::LetDeclaration:
                return static_cast<lsp::uint>(SemanticTokenTypeIndex::Variable);

            default:
                break;
        }
    }

    return std::nullopt;
}

std::optional<SemanticClassification> classifyToken(const parsing::Token* token,
                                                    const syntax::SyntaxNode* tokenParent,
                                                    const SymbolIndexer& symbolIndexer) {
    if (!token || token->isMissing() || token->kind == parsing::TokenKind::Placeholder ||
        token->kind == parsing::TokenKind::EndOfFile) {
        return std::nullopt;
    }

    if (parsing::LexerFacts::isKeyword(token->kind)) {
        return SemanticClassification{
            .tokenType = static_cast<lsp::uint>(SemanticTokenTypeIndex::Keyword)};
    }

    if (isStringToken(token->kind)) {
        return SemanticClassification{
            .tokenType = static_cast<lsp::uint>(SemanticTokenTypeIndex::String)};
    }

    if (isNumberToken(token->kind)) {
        return SemanticClassification{
            .tokenType = static_cast<lsp::uint>(SemanticTokenTypeIndex::Number)};
    }

    if (isMacroToken(token->kind)) {
        return SemanticClassification{
            .tokenType = static_cast<lsp::uint>(SemanticTokenTypeIndex::Macro)};
    }

    if (token->kind == parsing::TokenKind::SystemIdentifier) {
        SemanticClassification classification{
            .tokenType = static_cast<lsp::uint>(SemanticTokenTypeIndex::Function)};
        addModifier(classification.tokenModifiers, SemanticTokenModifierIndex::DefaultLibrary);
        return classification;
    }

    if (isOperatorToken(token->kind)) {
        return SemanticClassification{
            .tokenType = static_cast<lsp::uint>(SemanticTokenTypeIndex::Operator)};
    }

    if (token->kind != parsing::TokenKind::Identifier) {
        return std::nullopt;
    }

    if (auto* symbol = symbolIndexer.getSymbol(token)) {
        if (auto tokenType = tokenTypeFromSymbolKind(symbol->kind)) {
            SemanticClassification classification{.tokenType = *tokenType};
            addModifier(classification.tokenModifiers, SemanticTokenModifierIndex::Declaration);
            if (symbolIsReadonly(symbol->kind)) {
                addModifier(classification.tokenModifiers, SemanticTokenModifierIndex::Readonly);
            }
            return classification;
        }
    }

    if (auto tokenType = tokenTypeFromSyntaxContext(token, tokenParent)) {
        return SemanticClassification{.tokenType = *tokenType};
    }

    return std::nullopt;
}

void appendTokenData(std::vector<lsp::uint>& data, lsp::Position& previousStart,
                     lsp::Position start, lsp::uint length,
                     const SemanticClassification& classification) {
    auto deltaLine = start.line - previousStart.line;
    auto deltaCharacter = deltaLine == 0 ? start.character - previousStart.character
                                         : start.character;

    data.push_back(deltaLine);
    data.push_back(deltaCharacter);
    data.push_back(length);
    data.push_back(classification.tokenType);
    data.push_back(classification.tokenModifiers);

    previousStart = start;
}
} // namespace

ShallowAnalysis::ShallowAnalysis(SourceManager& sourceManager, slang::BufferID buffer,
                                 std::shared_ptr<SyntaxTree> tree, slang::Bag options,
                                 const std::vector<std::shared_ptr<SyntaxTree>>& allTrees) :
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

std::vector<lsp::DocumentSymbol> ShallowAnalysis::getDocSymbols() {
    if (!m_tree) {
        return {};
    }
    return m_symbolTreeVisitor.get_symbols(m_tree, true);
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
                                                           const ast::ASTContext& context,
                                                           const ast::Scope* scope) const {
    auto scopedParent = nameSyntax->parent->as_if<syntax::ScopedNameSyntax>();
    if (!scopedParent || nameSyntax->kind != syntax::SyntaxKind::IdentifierName) {
        return nullptr;
    }
    ast::LookupResult result;
    ast::Lookup::name(*scopedParent, context, ast::LookupFlags::None, result);
    if (!result.found) {
        ERROR("No symbol found for scoped name {} in scope {}", scopedParent->toString(),
              scope->asSymbol().getHierarchicalPath());
        return nullptr;
    }

    if (!result.path.empty()) {
        return result.path.front().symbol.get();
    }

    ERROR("No path found for scoped name {} in scope {}", scopedParent->toString(),
          scope->asSymbol().getHierarchicalPath());
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

const ast::Scope* ShallowAnalysis::getScopeFromSym(const ast::Symbol* symbol) {
    if (!symbol) {
        return nullptr;
    }

    if (symbol->isScope()) {
        return &symbol->as<ast::Scope>();
    }

    if (symbol->isType()) {
        auto& type = symbol->as<ast::Type>().getCanonicalType();
        if (type.isScope()) {
            return &type.as<ast::Scope>();
        }
    }
    else if (ast::ValueSymbol::isKind(symbol->kind)) {
        auto& type = symbol->as<ast::ValueSymbol>().getType().getCanonicalType();
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

/// @brief Visitor that finds and stores a specific token and its syntax node at an offset
struct OffsetFinder {
    OffsetFinder(uint32_t targetOffset) : targetOffset(targetOffset) {}

    void visit(const SyntaxNode& node) {
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
    const SyntaxNode* foundSyntax = nullptr;
    const parsing::Token* foundToken = nullptr;
};

const ast::Symbol* ShallowAnalysis::getSymbolAtToken(const parsing::Token* declTok) const {
    if (!declTok) {
        return nullptr;
    }

    auto syntax = syntaxes.getTokenParent(declTok);
    // Note: SuperHandle nodes can cause issues in symbol lookup
    if (!syntax || syntax->kind == syntax::SyntaxKind::SuperHandle) {
        return nullptr;
    }

    // Handle macro args
    std::shared_ptr<SyntaxTree> tokTree; // syntax needs to live for this function
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
        tokTree = SyntaxTree::fromText(macroArgText, m_sourceManager);
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
            return {};
        }
        if (syntax->getFirstToken() == *declTok) {
            return pkg;
        }
        return pkg->find(declTok->valueText());
    }
    else if (auto sym = m_symbolIndexer.getSymbol(declTok)) {
        switch (sym->kind) {
            case ast::SymbolKind::InstanceBody:
                // Module declarations get indexed to their body. We do want to keep the body
                // as the indexed sym for use in the future though with hdl features.
                return &sym->as<ast::InstanceBodySymbol>().getDefinition();
            case ast::SymbolKind::Port: {
                // Named port connections get indexed to the port symbol. Return the internal
                // symbol so that references work across instance boundaries.
                auto& port = sym->as<ast::PortSymbol>();
                if (port.internalSymbol) {
                    return port.internalSymbol;
                }
                return sym;
            }
            default:
                return sym;
        }
    }

    auto scope = m_symbolIndexer.getScopeForSyntax(*syntax);
    if (!scope) {
        INFO("No scope found for syntax {}, using root scope", syntax->toString());
        scope = &m_compilation->getRoot().as<ast::Scope>();
    }

    // Perform name lookup; this should be most gotos
    if (auto nameSyntax = findNameSyntax(*syntax)) {
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

                // Proper selector resolution is in
                // Expression::bindLookupResult, however that modifies the compilation at the
                // moment
                for (auto& sel : result.selectors) {
                    if (auto member = std::get_if<ast::LookupResult::MemberSelector>(&sel)) {
                        const ast::Scope* scope = getScopeFromSym(cur);
                        if (!scope) {
                            INFO("No scope found for sym {} : {}", cur->getHierarchicalPath(),
                                 toString(cur->kind));
                            return nullptr;
                        }
                        cur = scope->find(member->name);

                        if (member->nameRange == declTok->range()) {
                            return cur;
                        }
                    }
                    else {
                        const ast::Type* type = nullptr;
                        if (cur->isType()) {
                            type = &cur->as<ast::Type>();
                        }
                        else if (cur->isValue()) {
                            type = &cur->as<ast::ValueSymbol>().getType();
                        }

                        if (type->isArray()) {
                            cur = type->getArrayElementType();
                        }
                        else {
                            return nullptr;
                        }
                    }
                    if (!cur) {
                        WARN("No members found in scope {}",
                             scope->asSymbol().getHierarchicalPath());
                        return nullptr;
                    }
                }
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
        if (auto scopedResult = handleScopedNameLookup(nameSyntax, context, scope)) {
            return scopedResult;
        }
    }

    if (declTok->kind != parsing::TokenKind::Identifier ||
        syntax->kind == syntax::SyntaxKind::AttributeSpec) {
        return nullptr;
    }

    if (syntax->kind == syntax::SyntaxKind::DotMemberClause) {
        return handleInterfacePortHeader(declTok, syntax, scope);
    }
    // Try getting a definition as a last resort
    auto def = m_compilation->tryGetDefinition(declTok->valueText(), *scope);
    if (def.definition) {
        return def.definition;
    }

    auto pkg = m_compilation->getPackage(declTok->valueText());
    if (pkg) {
        return pkg;
    }

    return nullptr;
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

lsp::SemanticTokens ShallowAnalysis::getSemanticTokens() const {
    lsp::SemanticTokens tokens;
    tokens.data.reserve(syntaxes.collected.size() * 5);

    lsp::Position previousStart{.line = 0, .character = 0};
    for (const auto* token : syntaxes.collected) {
        auto tokenParent = syntaxes.getTokenParent(token);
        auto classification = classifyToken(token, tokenParent, m_symbolIndexer);
        if (!classification) {
            continue;
        }

        auto range = toRange(token->range(), m_sourceManager);
        if (range.start.line != range.end.line || range.end.character <= range.start.character) {
            continue;
        }

        appendTokenData(tokens.data, previousStart, range.start,
                        range.end.character - range.start.character, *classification);
    }

    return tokens;
}

const std::vector<std::string>& ShallowAnalysis::semanticTokenLegendTypes() {
    static const std::vector<std::string> legend = [] {
        std::vector<std::string> values;
        values.reserve(kSemanticTokenTypes.size());
        for (auto value : kSemanticTokenTypes) {
            values.emplace_back(value);
        }
        return values;
    }();
    return legend;
}

const std::vector<std::string>& ShallowAnalysis::semanticTokenLegendModifiers() {
    static const std::vector<std::string> legend = [] {
        std::vector<std::string> values;
        values.reserve(kSemanticTokenModifiers.size());
        for (auto value : kSemanticTokenModifiers) {
            values.emplace_back(value);
        }
        return values;
    }();
    return legend;
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
        const ast::Symbol* tokenSymbol = getSymbolAtToken(token);
        if (!tokenSymbol) {
            continue;
        }

        if (tokenSymbol->location == targetLocation) {
            targetSymbol = tokenSymbol;
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

        const ast::Symbol* tokenSymbol = getSymbolAtToken(token);
        if (!symbolsMatch(tokenSymbol, targetSymbol)) {
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

    (void)m_compilation->getSemanticDiagnostics();

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
