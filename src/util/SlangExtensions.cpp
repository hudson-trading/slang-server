//------------------------------------------------------------------------------
// SlangExtensions.cpp
// Functions that only deal with slang objects and could potentially go upstream
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "util/SlangExtensions.h"

#include "slang/ast/types/AllTypes.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"

namespace server {

using namespace slang;

bool hasValidBuffers(const SourceManager& sm, const std::shared_ptr<syntax::SyntaxTree>& tree) {
    if (!tree)
        return false;

    // Check the main buffer and included buffers
    auto buffers = tree->getSourceBufferIds();
    for (auto buf : buffers) {
        if (buf.valid() && !sm.isLatestData(buf)) {
            return false;
        }
    }

    return true;
}

const ast::Type& unwrapErrorType(const ast::Type& type) {
    auto& canonicalType = type.getCanonicalType();
    if (canonicalType.kind == ast::SymbolKind::ErrorType) {
        if (auto* child = canonicalType.as<ast::ErrorType>().child)
            return child->getCanonicalType();
    }
    return canonicalType;
}

const ast::Type* getTypeParameterTargetType(const ast::Type& type) {
    auto* alias = type.as_if<ast::TypeAliasType>();
    if (!alias)
        return nullptr;

    auto* aliasSyntax = alias->getSyntax();
    if (!aliasSyntax || !aliasSyntax->parent ||
        aliasSyntax->parent->kind != syntax::SyntaxKind::TypeParameterDeclaration) {
        return nullptr;
    }

    const auto& targetType = alias->targetType.getType();
    return targetType.isError() ? nullptr : &targetType;
}

} // namespace server
