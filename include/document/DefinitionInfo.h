//------------------------------------------------------------------------------
// DefinitionInfo.h
// Resolved definition target for hover and go-to-definition requests
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "Config.h"
#include "document/ShallowAnalysis.h"
#include "lsp/LspTypes.h"
#include <string>
#include <variant>
#include <vector>

#include "slang/ast/Symbol.h"
#include "slang/parsing/Token.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/text/SourceLocation.h"

namespace slang {
class SourceManager;
}

namespace server {

struct SystemTaskDoc;

namespace markup {
class Document;
}

struct DefinitionInfo {
    enum class Kind {
        Symbol,
        Macro,
        SystemSubroutine,
    };

    struct SyntaxTarget {
        // The syntax that the token refers to.
        const slang::syntax::SyntaxNode* node;
        // The exact name id in the syntax node, or the first token in the syntax if it wasn't
        // found.
        slang::parsing::Token nameToken;
        // Optional original source range; exists if it's behind a macro expansion.
        slang::SourceRange macroUsageRange = slang::SourceRange::NoLocation;

        bool operator==(const SyntaxTarget& other) const {
            return node == other.node && nameToken.location() == other.nameToken.location() &&
                   macroUsageRange == other.macroUsageRange;
        }

        /// Append the formatted code (with doc comments) for this syntax to `doc`.
        void renderCode(markup::Document& doc, const Config::HoverConfig& hovers) const;

        /// Append "Expanded from <text>" if this target is behind a macro expansion.
        void renderMacroExpansion(markup::Document& doc, const slang::SourceManager& sm) const;

        /// Goto-definition link pointing at the name token (or macro usage range, if applicable).
        std::vector<lsp::LocationLink> getDefinition(const slang::SourceManager& sm) const;
    };

    struct SymbolTarget {
        SyntaxTarget syntax;
        const slang::ast::Symbol* symbol;
        const std::shared_ptr<ShallowAnalysis> analysis;

        const slang::parsing::Token& nameToken() const { return syntax.nameToken; }

        bool operator==(const SymbolTarget& other) const {
            return syntax == other.syntax && symbol == other.symbol;
        }

        lsp::MarkupContent getHover(const slang::SourceManager& sm, slang::BufferID docBuffer,
                                    const Config::HoverConfig& hovers) const;

        std::vector<lsp::LocationLink> getDefinition(const slang::SourceManager& sm) const;
    };

    struct CommandLineDefineTarget {
        slang::parsing::Token nameToken;
        // For command-line defines: the config/build file that defined this directive.
        std::string defineSourceFile;

        bool operator==(const CommandLineDefineTarget& other) const {
            return nameToken.location() == other.nameToken.location() &&
                   defineSourceFile == other.defineSourceFile;
        }
    };

    struct MacroTarget {
        using Definition = std::variant<SyntaxTarget, CommandLineDefineTarget>;

        Definition definition;
        // Expanded text for macro usages (what the macro expands to at this call site).
        std::string macroExpansionText;

        const slang::parsing::Token& nameToken() const {
            return std::visit([](const auto& definition)
                                  -> const slang::parsing::Token& { return definition.nameToken; },
                              definition);
        }

        const SyntaxTarget* syntaxTarget() const { return std::get_if<SyntaxTarget>(&definition); }

        const CommandLineDefineTarget* commandLineDefine() const {
            return std::get_if<CommandLineDefineTarget>(&definition);
        }

        bool operator==(const MacroTarget& other) const {
            return definition == other.definition && macroExpansionText == other.macroExpansionText;
        }

        lsp::MarkupContent getHover(const slang::SourceManager& sm, slang::BufferID docBuffer,
                                    const Config::HoverConfig& hovers) const;

        std::vector<lsp::LocationLink> getDefinition(const slang::SourceManager& sm) const;
    };

    struct SystemSubroutineTarget {
        slang::parsing::Token token;
        const SystemTaskDoc* doc;
        bool isTask;

        const slang::parsing::Token& nameToken() const { return token; }

        bool operator==(const SystemSubroutineTarget& other) const {
            return token.location() == other.token.location() && doc == other.doc &&
                   isTask == other.isTask;
        }

        lsp::MarkupContent getHover(const slang::SourceManager& sm, slang::BufferID docBuffer,
                                    const Config::HoverConfig& hovers) const;

        std::vector<lsp::LocationLink> getDefinition(const slang::SourceManager& sm) const;
    };

    using Target = std::variant<SymbolTarget, MacroTarget, SystemSubroutineTarget>;

    // The thing this token resolves to.
    Target target;

    Kind kind() const {
        switch (target.index()) {
            case 0:
                return Kind::Symbol;
            case 1:
                return Kind::Macro;
            default:
                return Kind::SystemSubroutine;
        }
    }

    const slang::parsing::Token& nameToken() const {
        return std::visit(
            [](const auto& target) -> const slang::parsing::Token& { return target.nameToken(); },
            target);
    }

    const SyntaxTarget* syntaxTarget() const {
        if (auto* sym = std::get_if<SymbolTarget>(&target)) {
            return &sym->syntax;
        }
        if (auto* macro = std::get_if<MacroTarget>(&target)) {
            return macro->syntaxTarget();
        }
        return nullptr;
    }

    const SymbolTarget* symbolTarget() const { return std::get_if<SymbolTarget>(&target); }

    const slang::ast::Symbol* symbol() const {
        if (auto* sym = symbolTarget()) {
            return sym->symbol;
        }
        return nullptr;
    }

    MacroTarget* macro() { return std::get_if<MacroTarget>(&target); }

    const MacroTarget* macro() const { return std::get_if<MacroTarget>(&target); }

    const SystemSubroutineTarget* systemSubroutine() const {
        return std::get_if<SystemSubroutineTarget>(&target);
    }

    bool operator==(const DefinitionInfo& other) const { return target == other.target; }

    bool operator!=(const DefinitionInfo& other) const { return !(*this == other); }

    /// Render the hover markup for this definition.
    lsp::MarkupContent getHover(const slang::SourceManager& sm, slang::BufferID docBuffer,
                                const Config::HoverConfig& hovers) const;

    /// Resolve goto-definition links for this definition. May return multiple in the future
    /// (e.g. for symbols with several declarations).
    std::vector<lsp::LocationLink> getDefinition(const slang::SourceManager& sm) const;
};

} // namespace server
