//------------------------------------------------------------------------------
// ShallowAnalysis.cpp
// Implementation of document analysis class
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "document/ShallowAnalysis.h"

#include "util/Converters.h"
#include "util/Formatting.h"
#include "util/Logging.h"
#include <memory>
#include <string_view>

#include "slang/ast/ASTContext.h"
#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"
#include "slang/parsing/Token.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
namespace server {
using namespace slang;
ShallowAnalysis::ShallowAnalysis(const SourceManager& sourceManager, slang::BufferID buffer,
                                 std::shared_ptr<SyntaxTree> tree, slang::Bag options,
                                 const std::vector<std::shared_ptr<SyntaxTree>>& dependentTrees) :
    m_sourceManager(sourceManager), m_buffer(buffer), m_tree(tree), m_symbolIndexer(buffer),
    m_dependentTrees(dependentTrees), m_symbolTreeVisitor(m_sourceManager), m_syntaxes(*tree) {

    if (!m_tree) {
        ERROR("DocumentAnalysis initialized with null syntax tree");
        return;
    }

    // Syntaxes are already indexed in the constructor

    auto path = m_sourceManager.getFullPath(m_buffer).string();

    if (m_syntaxes.collected.size() == 0) {
        ERROR("No syntaxes found in document {}", path);
    }

    // Index macros
    m_macros.clear();
    for (auto& macro : m_tree->getDefinedMacros()) {
        m_macros[macro->name.valueText()] = macro;
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

    // Index syntax/token -> symbol
    m_compilation->getRoot().visit(m_symbolIndexer);
}

std::vector<lsp::DocumentSymbol> ShallowAnalysis::getDocSymbols() {
    if (!m_tree) {
        return {};
    }
    return m_symbolTreeVisitor.get_symbols(m_tree, true);
}

const parsing::Token* ShallowAnalysis::getTokenAt(SourceLocation loc) const {
    return m_syntaxes.getTokenAt(loc);
}

const syntax::NameSyntax* ShallowAnalysis::findNameSyntax(const syntax::SyntaxNode& node) const {
    if (node.parent == nullptr) {
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
    return inst.body.lookupName(header.modport->member.valueText());
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
        ERROR("No compilation available for getReferenceAt");
        return nullptr;
    }
    auto syntax = m_syntaxes.getSyntaxAt(declTok);
    // Note: SuperHandle nodes can cause issues in symbol lookup
    if (!syntax || syntax->kind == syntax::SyntaxKind::SuperHandle) {
        return nullptr;
    }

    // Handle macro args- syntax needs to live for this function
    std::shared_ptr<SyntaxTree> tokTree;
    if (syntax->kind == syntax::SyntaxKind::TokenList &&
        syntax->parent->kind == syntax::SyntaxKind::MacroActualArgument) {
        // parse the token list, and use those name syntaxes for lookups
        // TODO: be more precise; handle args that produce lhs ids

        std::string_view tokList = std::string_view{syntax->getFirstToken().rawText().data(),
                                                    syntax->toString().size()};
        tokTree = SyntaxTree::fromText(tokList);
        tokTree->root().parent = syntax->parent;
        OffsetFinder visitor(declTok->location().offset() -
                             syntax->getFirstToken().location().offset());
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
                return nullptr;
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

    // Handle special syntax cases
    switch (syntax->kind) {
        case syntax::SyntaxKind::PackageImportItem: {
            if (auto pkg = m_compilation->getPackage(declTok->valueText())) {
                return pkg;
            }
            ERROR("No package found for token {}", declTok->valueText());
            return nullptr;
        }
        case syntax::SyntaxKind::DotMemberClause: {
            return handleInterfacePortHeader(declTok, syntax, scope);
        }
        default:
            break;
    }

    // Try getting a definition as a last resort
    auto def = m_compilation->tryGetDefinition(declTok->valueText(), *scope);
    if (def.definition) {
        return def.definition;
    }

    return nullptr;
}

const ast::Symbol* ShallowAnalysis::getSymbolAt(SourceLocation loc) const {
    auto node = m_syntaxes.getWordTokenAt(loc);
    if (!node) {
        return nullptr;
    }
    return getSymbolAtToken(node);
}

const ast::Scope* ShallowAnalysis::getScopeAt(SourceLocation loc) const {
    auto syntax = m_syntaxes.getSyntaxAt(loc);
    if (!syntax) {
        return nullptr;
    }
    return m_symbolIndexer.getScopeForSyntax(*syntax);
}

std::optional<DefinitionInfo> ShallowAnalysis::getDefinitionInfoAt(const lsp::Position& position) {

    // Get location, token, and syntax node at position
    auto loc = m_sourceManager.getSourceLocation(m_buffer, position.line, position.character);
    if (!loc) {
        return std::nullopt;
    }
    const parsing::Token* declTok = m_syntaxes.getWordTokenAt(loc.value());
    if (!declTok) {
        return std::nullopt;
    }
    const syntax::SyntaxNode* declSyntax = m_syntaxes.getSyntaxAt(declTok);
    if (!declSyntax) {
        return std::nullopt;
    }

    std::optional<parsing::Token> nameToken;
    const syntax::SyntaxNode* symSyntax = nullptr;

    // Directives refer directly to syntaxes; others refer to symbols
    // TODO: handle macro args better. getReferenceAt looks them up, but they may not refer to
    // anything, like if being used to make a lhs id name.
    switch (declSyntax->kind) {
        case syntax::SyntaxKind::MacroUsage: {
            // look in macro list
            auto macro = m_macros.find(
                declSyntax->as<syntax::MacroUsageSyntax>().directive.valueText().substr(1));
            if (macro == m_macros.end()) {
                return std::nullopt;
            }
            symSyntax = macro->second;
            nameToken = macro->second->name;
        } break;
        default: {
            auto sym = getSymbolAtToken(declTok);
            if (!sym) {
                return std::nullopt;
            }
            symSyntax = sym->getSyntax();

            if (!symSyntax) {
                ERROR("Failed to get syntax for symbol {} of kind {}", sym->name,
                      toString(sym->kind));
                return std::nullopt;
            }

            // For some symbols we want to return the parent to get the data type
            if (sym->kind == ast::SymbolKind::Modport ||
                sym->kind == ast::SymbolKind::ModportPort) {
                symSyntax = symSyntax->parent;
            }
            nameToken = findNameToken(symSyntax, sym->name);
            if (!nameToken) {
                ERROR("Failed to find name token for symbol '{}' of kind {} = {}", sym->name,
                      toString(sym->kind), symSyntax->toString());

                // TODO: figure out why this fails sometimes with all generates
                nameToken = symSyntax->getFirstToken();
            }
        } break;
    }

    auto ret = DefinitionInfo{
        symSyntax,
        *nameToken,
        SourceRange::NoLocation,
    };

    // fill in original range if behind a macro
    if (ret.nameToken && m_sourceManager.isMacroLoc(ret.nameToken.location())) {
        auto locs = m_sourceManager.getMacroExpansions(ret.nameToken.location());
        // TODO: maybe include more expansion infos?
        auto macroInfo = m_sourceManager.getMacroInfo(locs.back());
        auto text = macroInfo ? m_sourceManager.getText(macroInfo->expansionRange) : "";
        if (text.empty()) {
            ERROR("Couldn't get original range for symbol {}", ret.nameToken.valueText());
        }
        else {
            ret.macroUsageRange = macroInfo->expansionRange;
        }
    }

    return ret;
}

std::vector<lsp::LocationLink> ShallowAnalysis::getDocDefinition(const lsp::Position& position) {
    auto info = getDefinitionInfoAt(position);
    if (!info) {
        return {};
    }
    return {getDefinition(*info)};
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

std::optional<lsp::Hover> ShallowAnalysis::getDocHover(const lsp::Position& position,
                                                       bool noDebug) {
    auto loc = m_sourceManager.getSourceLocation(m_buffer, position.line, position.character);
    if (!loc) {
        return std::nullopt;
    }
    auto maybeInfo = getDefinitionInfoAt(position);
    if (!maybeInfo) {
#ifdef SLANG_DEBUG
        if (!noDebug) {
            // Shows debug info for the token under cursor when debugging
            auto tok = m_syntaxes.getTokenAt(loc.value());
            if (tok == nullptr) {
                return std::nullopt;
            }
            return lsp::Hover{
                .contents = lsp::MarkupContent{.kind = lsp::MarkupKind::make<"markdown">(),
                                               .value = getDebugHover(*tok)}};
        }
#endif
        return std::nullopt;
    }
    auto info = *maybeInfo;

    // Adjust the ranges for some syntaxes
    const SyntaxNode* nodePtr = info.node;

    auto md = svCodeBlockString(*nodePtr);

    if (info.macroUsageRange != SourceRange::NoLocation) {
        auto text = m_sourceManager.getText(info.macroUsageRange);
        md += fmt::format("\n Expanded from\n {}", svCodeBlockString(text));
    }
    return lsp::Hover{.contents = markdown(md)};
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

    auto node = m_syntaxes.getSyntaxAt(&tok);
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

        try {
            value += fmt::format("*{}* ", toString(nodePtr->kind));
            auto range = nodePtr->sourceRange();
            std::string svText = "No Source";
            if (range != SourceRange::NoLocation) {
                svText = nodePtr->toString();
            }
            value += fmt::format("`{}`  \n ",
                                 svText.substr(0, svText.size() > 20 ? 20 : svText.size()));
        }
        catch (std::logic_error& e) {
            value += fmt::format("Error: {}  \n ", e.what());
            break;
        }

        auto sym = m_symbolIndexer.getSymbol(nodePtr);

        if (sym) {
            // We reached a symbol
            value += fmt::format("`{} : {}`  \n", sym->name, toString(sym->kind));
            break;
        }
    }
    return value;
}

const std::vector<lsp::LocationLink> ShallowAnalysis::getDefinition(
    const DefinitionInfo& info) const {
    auto targetRange = info.macroUsageRange != SourceRange::NoLocation ? info.macroUsageRange
                                                                       : info.nameToken.range();
    auto path = m_sourceManager.getFullPath(targetRange.start().buffer());
    if (path.empty()) {
        ERROR("No path found for symbol {}", info.nameToken ? info.nameToken.valueText() : "");
        return {};
    }
    auto lspRange = toRange(targetRange, m_sourceManager);

    return {lsp::LocationLink{
        .targetUri = URI::fromFile(path),
        // This is supposed to be the full source range- however the hover view already provides
        // that, leading to a worse UI
        .targetRange = lspRange,
        .targetSelectionRange = lspRange,
    }};
}

} // namespace server
