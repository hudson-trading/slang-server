//------------------------------------------------------------------------------
// SyntaxIndexer.cpp
// Implementation of syntax visitor for collecting macro usages
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "document/SyntaxIndexer.h"

#include "util/Logging.h"

#include "slang/parsing/Token.h"
#include "slang/parsing/TokenKind.h"
#include "slang/syntax/SyntaxFacts.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/util/SmallMap.h"
#include "slang/util/Util.h"

namespace server {
using namespace slang;

SyntaxIndexer::SyntaxIndexer(const slang::syntax::SyntaxTree& tree) {
    SLANG_ASSERT(tree.getSourceBufferIds().size() == 1);
    m_buffer = tree.getSourceBufferIds()[0];
    visit(tree.root());
}

void SyntaxIndexer::visit(const slang::syntax::SyntaxNode& node) {
    switch (node.kind) {
        case syntax::SyntaxKind::MacroUsage: {
            if (node.getFirstToken().location().buffer() == m_buffer) {
                collectedHints.emplace(
                    static_cast<uint32_t>(node.getFirstToken().location().offset()), &node);
            }
            break;
        }
        case syntax::SyntaxKind::InvocationExpression:
        case syntax::SyntaxKind::HierarchyInstantiation:
        case syntax::SyntaxKind::ClassName:
            collectedHints.emplace(static_cast<uint32_t>(node.getFirstToken().location().offset()),
                                   &node);
            break;
        default:
            break;
    }

    for (uint32_t i = 0; i < node.getChildCount(); i++) {
        auto child = node.childNode(i);
        if (child)
            visit(*child);
        else {
            auto token = const_cast<slang::syntax::SyntaxNode&>(node).childTokenPtr(i);
            if (!token)
                continue;
            for (const auto& trivia : token->trivia()) {
                if (trivia.kind == parsing::TriviaKind::Directive) {
                    visit(*trivia.syntax());
                    // Macro args need to lookup in a scope; we could add a new map, but it's better
                    // to keep the same process as normal nodes
                    trivia.syntax()->parent = const_cast<slang::syntax::SyntaxNode*>(&node);
                }
            }
            if (token->location().buffer() == m_buffer &&
                token->kind != parsing::TokenKind::Placeholder) {
                if (collected.size() > 0) {
                    /// TODO: investigate overlaps
                    // SLANG_ASSERT(collected.back()->range().end() <= token->range().start());
                    if (!(collected.back()->range().end() <= token->range().start())) {
                        ERROR("Token '{}': {} overlaps with previous token '{}' : {}",
                              token->rawText(), toString(token->kind), collected.back()->rawText(),
                              toString(collected.back()->kind));
                    }
                }
                collected.push_back(token);

                tokenToParent[token] = &node;
            }
        }
    }
}

int SyntaxIndexer::tokenIndexBefore(slang::SourceLocation loc) const {
    if (loc.buffer() != m_buffer) {
        return -1;
    }

    if (collected.size() == 0) {
        return -1;
    }

    /// collected[low].location <= loc < collected[high].location
    // check invariants at initial state
    if (loc < collected[0]->location()) {
        return -1;
    }
    // don't need to check high, since we only return low

    size_t low = 0;
    size_t high = collected.size();
    while (low + 1 < high) {
        size_t mid = low + (high - low) / 2;
        auto node = collected[mid];
        auto range = node->range();
        if (range.start() <= loc) {
            low = mid;
        }
        else {
            high = mid;
        }
    }
    return static_cast<int>(low);
}

bool SyntaxIndexer::editorContains(slang::SourceRange range, slang::SourceLocation loc) const {
    // TODO: change this to <= since curors are typically between characters (on vscode)
    return range.start() <= loc && loc < range.end();
}

bool SyntaxIndexer::isIdToken(const parsing::TokenKind kind) const {
    return kind == parsing::TokenKind::Identifier || kind == parsing::TokenKind::SystemIdentifier ||
           kind == parsing::TokenKind::Directive || kind == parsing::TokenKind::MacroUsage;
}

const slang::parsing::Token* SyntaxIndexer::getWordTokenAt(slang::SourceLocation loc) const {
    auto atInd = tokenIndexBefore(loc);
    if (atInd == -1) {
        return nullptr;
    }
    if (isIdToken(collected[atInd]->kind) && editorContains(collected[atInd]->range(), loc)) {
        return collected[atInd];
    }
    if (atInd == 0) {
        return nullptr;
    }
    // SomePkg::var
    //        ^^ may be the first loc on this token, but want to match to SomePkg
    if (isIdToken(collected[atInd - 1]->kind) &&
        editorContains(collected[atInd - 1]->range(), loc)) {
        return collected[atInd - 1];
    }
    return nullptr;
}

const slang::parsing::Token* SyntaxIndexer::getTokenAt(slang::SourceLocation loc) const {
    auto atInd = tokenIndexBefore(loc);
    if (atInd == -1) {
        return nullptr;
    }
    auto tok = collected[atInd];
    if (tok->range().contains(loc)) {
        return tok;
    }
    return nullptr;
}

const syntax::SyntaxNode* SyntaxIndexer::getTokenParent(const parsing::Token* tok) const {
    auto it = tokenToParent.find(tok);
    if (it == tokenToParent.end()) {
        return nullptr;
    }
    return it->second;
}

const syntax::SyntaxNode* SyntaxIndexer::getSyntaxBefore(slang::SourceLocation loc) const {
    auto ind = tokenIndexBefore(loc);
    if (ind == -1) {
        return nullptr;
    }
    auto tok = collected[ind];
    auto node = tokenToParent.find(tok);
    if (node == tokenToParent.end()) {
        return nullptr;
    }
    return node->second;
}

// SyntaxContext has syntax node, before and after token, and index into syntax. Only before token
// if it's in a token.

const syntax::SyntaxNode* SyntaxIndexer::getSyntaxAt(slang::SourceLocation loc) const {
    auto ind = tokenIndexBefore(loc);
    if (ind == -1) {
        return nullptr;
    }
    auto beforeToken = collected[ind];
    // After last token
    if (ind + 1 >= static_cast<int>(collected.size())) {
        // return root
        return nullptr;
    }
    // Inside a token
    if (loc < beforeToken->range().end()) {
        return getTokenParent(beforeToken);
    }

    // Find mutual ancestor
    auto beforeSyntax = getTokenParent(beforeToken);
    auto afterSyntax = getTokenParent(collected[ind + 1]);
    SmallSet<const syntax::SyntaxNode*, 16> beforeParents;
    for (auto ptr = beforeSyntax; ptr != nullptr; ptr = ptr->parent) {
        beforeParents.insert(ptr);
    }
    for (auto ptr = afterSyntax; ptr != nullptr; ptr = ptr->parent) {
        if (beforeParents.contains(ptr)) {
            return ptr;
        }
    }
    return nullptr;
    // find surrounding tokens, get common syntax node if we're not in the syntax
}
} // namespace server
