//------------------------------------------------------------------------------
// SyntaxIndexer.h
// Syntax visitor for collecting macro usages
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <vector>

#include "slang/parsing/Token.h"
#include "slang/parsing/TokenKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/util/FlatMap.h"
namespace server {
/// A visitor class that finds the syntax node at a given location.
class SyntaxIndexer {
private:
    slang::BufferID m_buffer;

public:
    // collected declared tokens in order (tokens in the actual file)
    std::vector<const slang::parsing::Token*> collected;
    // Mapping of tokens to their parent syntax node
    // For macro usages, this will map the
    slang::flat_hash_map<const slang::parsing::Token*, const slang::syntax::SyntaxNode*>
        tokenToParent;

    // Mapping of tokens to their parent syntax node
    // slang::flat_hash_map<const slang::parsing::Token*, const slang::syntax::Token*>
    // tokenToParent;

    /// @brief Constructor that takes a syntax tree and extracts buffer ID from sources
    /// @param tree The syntax tree to analyze
    /// Also macro usage's parent pointers at the syntax's parents that they're trivia for
    SyntaxIndexer(const slang::syntax::SyntaxTree& tree);

    void visit(const slang::syntax::SyntaxNode& node);

    int tokenIndexBefore(slang::SourceLocation loc) const;

    bool editorContains(slang::SourceRange range, slang::SourceLocation loc) const;

    bool isIdToken(const slang::parsing::TokenKind kind) const;

    const slang::parsing::Token* getWordTokenAt(slang::SourceLocation loc) const;

    const slang::parsing::Token* getTokenAt(slang::SourceLocation loc) const;

    const slang::syntax::SyntaxNode* getSyntaxAt(const slang::parsing::Token* tok) const;

    const slang::syntax::SyntaxNode* getSyntaxAt(slang::SourceLocation loc) const;
};
} // namespace server
