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
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
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
class Paragraph;
} // namespace markup

struct DefinitionInfo {
    struct SyntaxTarget {
        // The syntax that the token refers to.
        const slang::syntax::SyntaxNode* node;
        // The exact name id in the syntax node, or the first token in the syntax if it wasn't
        // found.
        slang::parsing::Token nameToken;
        // Optional original source range; exists if it's behind a macro expansion.
        slang::SourceRange macroUsageRange;

        SyntaxTarget(const slang::syntax::SyntaxNode* node, slang::parsing::Token nameToken,
                     slang::SourceRange macroUsageRange = slang::SourceRange::NoLocation) :
            node(node), nameToken(nameToken), macroUsageRange(macroUsageRange) {}
        SyntaxTarget() : SyntaxTarget(nullptr, {}, slang::SourceRange::NoLocation) {}

        static SyntaxTarget fromNode(const slang::syntax::SyntaxNode* node,
                                     slang::parsing::Token nameToken,
                                     const slang::SourceManager& sourceManager);

        bool operator==(const SyntaxTarget& other) const {
            return node == other.node && nameToken.location() == other.nameToken.location() &&
                   macroUsageRange == other.macroUsageRange;
        }

        /// Append the formatted code and macro usage for this syntax to `paragraph`.
        void renderCode(markup::Paragraph& paragraph, const slang::SourceManager& sm,
                        bool rawDocComments) const;

        /// Goto-definition link pointing at the name token (or macro usage range, if applicable).
        std::vector<lsp::LocationLink> getDefinition(const slang::SourceManager& sm) const;
    };

    struct SymbolTarget {
        // A semantic symbol can have more than one declaration site that is useful to display.
        // For example, a modport port has both its directional declaration and the declaration of
        // the underlying typed symbol.
        std::vector<SyntaxTarget> syntaxes;
        const slang::ast::Symbol* symbol;
        std::shared_ptr<ShallowAnalysis> analysis;
        bool renderInputPortDriver = false;
        size_t generatedSignalCount = 1;

        const slang::parsing::Token& nameToken() const { return syntaxes.front().nameToken; }

        bool operator==(const SymbolTarget& other) const {
            return syntaxes == other.syntaxes && symbol == other.symbol &&
                   renderInputPortDriver == other.renderInputPortDriver &&
                   generatedSignalCount == other.generatedSignalCount;
        }

        markup::Document getHover(const slang::SourceManager& sm, slang::BufferID docBuffer,
                                  const Config::HoverConfig& hovers) const;

        std::vector<lsp::LocationLink> getDefinition(const slang::SourceManager& sm) const;
    };

    struct PortConnectionTarget {
        SymbolTarget outer;
        SymbolTarget inner;

        const slang::parsing::Token& nameToken() const { return outer.nameToken(); }

        bool operator==(const PortConnectionTarget&) const = default;

        markup::Document getHover(const slang::SourceManager& sm, slang::BufferID docBuffer,
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

        MacroTarget(Definition definition, const slang::syntax::SyntaxNode& referenceSyntax,
                    const ShallowAnalysis& analysis);

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

        markup::Document getHover(const slang::SourceManager& sm, slang::BufferID docBuffer,
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

        markup::Document getHover(const slang::SourceManager& sm, slang::BufferID docBuffer,
                                  const Config::HoverConfig& hovers) const;

        std::vector<lsp::LocationLink> getDefinition(const slang::SourceManager& sm) const;
    };

    using Target =
        std::variant<SymbolTarget, PortConnectionTarget, MacroTarget, SystemSubroutineTarget>;

    // The things this token resolves to, in semantic / elaboration order.
    std::vector<Target> targets;
    std::reference_wrapper<const slang::SourceManager> sourceManager;

    DefinitionInfo(const slang::SourceManager& sourceManager, Target target) :
        sourceManager(sourceManager) {
        targets.push_back(std::move(target));
    }
    DefinitionInfo(const slang::SourceManager& sourceManager, std::vector<Target> targets) :
        targets(std::move(targets)), sourceManager(sourceManager) {}

    const Target& primaryTarget() const { return targets.front(); }
    Target& primaryTarget() { return targets.front(); }

    const slang::parsing::Token& nameToken() const {
        return std::visit(
            [](const auto& target) -> const slang::parsing::Token& { return target.nameToken(); },
            primaryTarget());
    }

    const slang::ast::Symbol* symbol() const {
        if (auto* symbol = std::get_if<SymbolTarget>(&primaryTarget()))
            return symbol->symbol;
        if (auto* port = std::get_if<PortConnectionTarget>(&primaryTarget()))
            return port->outer.symbol;
        return nullptr;
    }

    MacroTarget* macro() { return std::get_if<MacroTarget>(&primaryTarget()); }

    const MacroTarget* macro() const { return std::get_if<MacroTarget>(&primaryTarget()); }

    const SystemSubroutineTarget* systemSubroutine() const {
        return std::get_if<SystemSubroutineTarget>(&primaryTarget());
    }

    bool operator==(const DefinitionInfo& other) const { return targets == other.targets; }

    bool operator!=(const DefinitionInfo& other) const { return !(*this == other); }

    /// Render the hover markup for this definition.
    lsp::MarkupContent getHover(slang::BufferID docBuffer, const Config::HoverConfig& hovers) const;

    /// Resolve and deduplicate goto-definition links for every target and declaration site.
    std::vector<lsp::LocationLink> getDefinitionLspLinks() const;

    /// Resolve and deduplicate legacy goto-definition locations.
    std::vector<lsp::Location> getDefinitionLspLocs() const;
};

} // namespace server
