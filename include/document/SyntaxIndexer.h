//------------------------------------------------------------------------------
// SyntaxIndexer.h
// Syntax visitor for collecting macro usages
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "completions/CompletionContext.h"
#include <vector>

#include "slang/parsing/Token.h"
#include "slang/parsing/TokenKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/util/FlatMap.h"

namespace server {

/// Collects syntax info for a Shallow Analysis based on its syntax tree
/// - SourceLocation -> Token
/// - Token -> SyntaxNode
/// - Syntax nodes potentially used in inlay hints
class SyntaxIndexer {
private:
    slang::BufferID m_buffer;

public:
    /// Collected declared tokens in order (tokens in the actual file)
    std::vector<const slang::parsing::Token*> collected;

    /// Mapping of tokens to their parent syntax node
    slang::flat_hash_map<const slang::parsing::Token*, const slang::syntax::SyntaxNode*>
        tokenToParent;

    /// Map from offset to syntax nodes collected for inlay hints
    std::map<uint32_t, slang::not_null<const slang::syntax::SyntaxNode*>> collectedHints;

    /// @brief Constructor that takes a syntax tree and extracts buffer ID from sources
    /// @param tree The syntax tree to analyze
    /// Also macro usage's parent pointers at the syntax's parents that they're trivia for
    SyntaxIndexer(const slang::syntax::SyntaxTree& tree);

    /// Get the word token (identifier, system identifier, directive, macro usage, etc) at the given
    /// location, or nullptr if none
    const slang::parsing::Token* getWordTokenAt(slang::SourceLocation loc) const;

    /// Get the token at the given location, or nullptr if none
    const slang::parsing::Token* getTokenAt(slang::SourceLocation loc) const;

    /// Get the syntax parent of a given token
    const slang::syntax::SyntaxNode* getTokenParent(const slang::parsing::Token* tok) const;

    /// Get the lowest level syntax node containing the location. Returns nullptr if outside the
    /// [first, last] tokens
    const slang::syntax::SyntaxNode* getSyntaxAt(slang::SourceLocation loc) const;

private:
    /// Whether the editor considers this location to be inside the given range
    bool editorContains(slang::SourceRange range, slang::SourceLocation loc) const;
    /// Get the index of the token before the given location, or -1 if before the first token
    int tokenIndexBefore(slang::SourceLocation loc) const;
    /// Whether this token kind is considered as some sort of identifier
    bool isIdToken(const slang::parsing::TokenKind kind) const;
    /// Recursively visit syntax nodes, called in constructor
    void visit(const slang::syntax::SyntaxNode& root);
};
} // namespace server
