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
#include <fmt/format.h>
#include <memory>
#include <string_view>

#include "slang/ast/ASTContext.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/Type.h"
#include "slang/driver/Driver.h"
#include "slang/parsing/Token.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"
#include "slang/util/Util.h"
namespace server {
using namespace slang;
ShallowAnalysis::ShallowAnalysis(SourceManager& sourceManager, slang::BufferID buffer,
                                 std::shared_ptr<SyntaxTree> tree, slang::Bag options,
                                 const std::vector<std::shared_ptr<SyntaxTree>>& dependentTrees) :
    syntaxes(*tree), m_sourceManager(sourceManager), m_buffer(buffer), m_tree(tree),
    m_dependentTrees(dependentTrees), m_symbolTreeVisitor(m_sourceManager),
    m_symbolIndexer(buffer) {

    if (!m_tree) {
        ERROR("DocumentAnalysis initialized with null syntax tree");
        return;
    }

    // Syntaxes are already indexed in the constructor

    auto path = m_sourceManager.getFullPath(m_buffer).string();

    if (syntaxes.collected.size() == 0) {
        ERROR("No syntaxes found in document {}", path);
    }

    // Index macros
    // TODO: these should be tagged in the preprocessor instead, since users may `undef them
    for (auto& macro : m_tree->getDefinedMacros()) {
        macros[macro->name.valueText()] = macro;
    }

    // Set up options for shallow compilation
    auto cOptions = options.getOrDefault<ast::CompilationOptions>();
    cOptions.flags |= ast::CompilationFlags::AllowTopLevelIfacePorts;
    cOptions.flags |= ast::CompilationFlags::AllGenerateBranches;
    cOptions.flags |= ast::CompilationFlags::AllowInvalidTop;

    // Add definitions from this tree (even if they aren't valid tops)
    cOptions.topModules.clear();
    m_compilation = std::make_unique<ast::Compilation>(cOptions);
    for (auto& depTree : m_dependentTrees) {
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
    if (!m_compilation) {
        return nullptr;
    }

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

    if (!m_compilation) {
        ERROR("No compilation available for getSymbolAtToken");
        return nullptr;
    }
    auto syntax = syntaxes.getSyntaxAt(declTok);
    // Note: SuperHandle nodes can cause issues in symbol lookup
    if (!syntax || syntax->kind == syntax::SyntaxKind::SuperHandle) {
        return nullptr;
    }

    // Handle macro args
    std::shared_ptr<SyntaxTree> tokTree; // syntax needs to live for this function
    if (syntax->kind == syntax::SyntaxKind::TokenList &&
        syntax->parent->kind == syntax::SyntaxKind::MacroActualArgument) {
        // parse the token list, and use those name syntaxes for lookups
        // TODO: be more precise; handle args that produce lhs ids
        // they may not refer to
        // anything, like if being used to make a lhs id name.

        auto& macroArgSyntax = syntax->parent->as<syntax::MacroActualArgumentSyntax>();

        auto firstToken = macroArgSyntax.getFirstToken();
        auto lastToken = macroArgSyntax.getLastToken();
        size_t startOffset = firstToken.location().offset();
        size_t endOffset = lastToken.location().offset() + lastToken.rawText().size();

        std::string_view macroArg{firstToken.rawText().data(), endOffset - startOffset};

        // These will overwrite the same assigned source, but it's ok since they are temporary,
        // and the source manager should be thread safe (for when we do threaded async)
        tokTree = SyntaxTree::fromText(macroArg, m_sourceManager);
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
        // Check for symbol declarations
        return sym;
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
            return result.found;
        }

        // Try scoped name lookup with the same flags
        if (auto scopedResult = handleScopedNameLookup(nameSyntax, context, scope)) {
            return scopedResult;
        }

        ERROR("No symbol found for name syntax {} in scope {}", syntax->toString(),
              scope->asSymbol().getHierarchicalPath());
    }

    if (declTok->kind != parsing::TokenKind::Identifier ||
        syntax->kind == syntax::SyntaxKind::AttributeSpec) {
        return nullptr;
    }
    WARN("No sym found for token {} in scope {}", declTok->valueText(),
         scope->asSymbol().getHierarchicalPath());

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
    for (auto& tree : m_dependentTrees) {
        for (auto& buffer : tree->getSourceBufferIds()) {
            if (!m_sourceManager.isValid(buffer)) {
                return false;
            }
        }
    }
    for (auto& buffer : m_tree->getSourceBufferIds()) {
        if (!m_sourceManager.isValid(buffer)) {
            return false;
        }
    }
    return true;
}

std::string ShallowAnalysis::getDebugHover(const parsing::Token& tok) const {
    std::string value = fmt::format("`{}` Token\n", toString(tok.kind));

    auto node = syntaxes.getSyntaxAt(&tok);
    for (auto nodePtr = node; nodePtr; nodePtr = nodePtr->parent) {
        // In case of bad memory
        if (nodePtr->kind > syntax::SyntaxKind::XorAssignmentExpression) {
            break;
        }
        auto kindStr = toString(nodePtr->kind);
        if (kindStr.empty()) {
            value += "*Unknown*";
            break;
        }

        value += fmt::format("*{}* ", toString(nodePtr->kind));
        auto range = nodePtr->sourceRange();
        std::string svText = "No Source";
        if (range != SourceRange::NoLocation) {
            svText = nodePtr->toString();
        }
        value += fmt::format("`{}`  \n ", svText.substr(0, std::min((size_t)20, svText.size())));

        auto sym = m_symbolIndexer.getSymbol(nodePtr);

        if (sym) {
            // We reached a symbol
            value += fmt::format("`{} : {}`  \n", sym->name, toString(sym->kind));
            break;
        }
    }
    return value;
}

} // namespace server
