//------------------------------------------------------------------------------
// CompletionContext.cpp
// Syntax-aware completion context detection implementation
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "completions/CompletionContext.h"

#include "document/SlangDoc.h"

#include "slang/ast/Scope.h"
#include "slang/syntax/SyntaxFacts.h"
#include "slang/syntax/SyntaxKind.h"

namespace server {

using namespace slang;
using namespace slang::syntax;
using namespace slang::ast;

namespace {

/// Check if a syntax kind represents an expression (value context)
bool isExpressionContext(SyntaxKind kind) {
    switch (kind) {
        // Port connections - values
        case SyntaxKind::OrderedPortConnection:
        case SyntaxKind::NamedPortConnection:
        // Function/task arguments
        case SyntaxKind::OrderedArgument:
        case SyntaxKind::NamedArgument:
        // Parameter assignments
        case SyntaxKind::OrderedParamAssignment:
        case SyntaxKind::NamedParamAssignment:
        // Conditional/control flow
        case SyntaxKind::ConditionalExpression:
        case SyntaxKind::ConditionalStatement:
        // Various expressions
        case SyntaxKind::ParenthesizedExpression:
        case SyntaxKind::InvocationExpression:

        // Value initializers
        case SyntaxKind::EqualsValueClause:
            return true;
        default:
            return false;
    }
}

/// Check if a syntax kind represents a port list context (type position, no module instantiation)
bool isPortListContext(SyntaxKind kind) {
    switch (kind) {
        case SyntaxKind::AnsiPortList:
        case SyntaxKind::NonAnsiPortList:
        case SyntaxKind::WildcardPortList:
            return true;
        default:
            return false;
    }
}

/// Check if a syntax kind represents a module/class item context
bool isModuleMemberContext(SyntaxKind kind) {
    switch (kind) {
        case SyntaxKind::ModuleDeclaration:
        case SyntaxKind::InterfaceDeclaration:
        case SyntaxKind::ProgramDeclaration:
        case SyntaxKind::PackageDeclaration:
        case SyntaxKind::ClassDeclaration:
        case SyntaxKind::GenerateBlock:
        case SyntaxKind::GenerateRegion:
            return true;
        default:
            return false;
    }
}

/// Check if a syntax kind represents a procedural block context (task/function/always/initial)
bool isProceduralBlockContext(SyntaxKind kind) {
    switch (kind) {
        case SyntaxKind::FunctionDeclaration:
        case SyntaxKind::TaskDeclaration:
        case SyntaxKind::AlwaysBlock:
        case SyntaxKind::AlwaysCombBlock:
        case SyntaxKind::AlwaysFFBlock:
        case SyntaxKind::AlwaysLatchBlock:
        case SyntaxKind::InitialBlock:
        case SyntaxKind::FinalBlock:
        case SyntaxKind::SequentialBlockStatement:
        case SyntaxKind::ParallelBlockStatement:
            return true;
        default:
            return false;
    }
}

} // anonymous namespace

CompletionContext CompletionContext::fromLocation(SlangDoc& doc, SourceLocation loc) {
    CompletionContext ctx;
    ctx.scope = doc.getScopeAt(loc);
    ctx.syntax = doc.getAnalysis().syntaxes.getSyntaxAt(loc);

    if (!ctx.syntax) {
        // No syntax node at location - assume module item context if we have a scope
        ctx.kind = ctx.scope ? CompletionContextKind::ModuleMember : CompletionContextKind::Unknown;
        return ctx;
    }

    // Walk up the parent chain to determine context
    for (auto* node = ctx.syntax; node; node = node->parent) {
        auto kind = node->kind;

        // Check for expression contexts
        if (SyntaxFacts::isAssignmentOperator(kind) || isExpressionContext(kind)) {
            ctx.kind = CompletionContextKind::Expression;
            return ctx;
        }

        // Check for procedural block contexts (task/function/always/initial)
        if (isProceduralBlockContext(kind)) {
            ctx.kind = CompletionContextKind::Procedural;
            return ctx;
        }

        // Check for port list contexts - want types but not module instantiations
        if (isPortListContext(kind)) {
            ctx.kind = CompletionContextKind::PortList;
            return ctx;
        }

        // Check for module/class item contexts
        if (isModuleMemberContext(kind)) {
            ctx.kind = CompletionContextKind::ModuleMember;
            return ctx;
        }
    }

    // Default to module item if we have a scope
    ctx.kind = ctx.scope ? CompletionContextKind::ModuleMember : CompletionContextKind::Unknown;
    return ctx;
}

} // namespace server
