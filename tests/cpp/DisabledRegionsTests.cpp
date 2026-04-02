#include <catch2/catch_test_macros.hpp>

#include "slang/parsing/Token.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

/// Recursively collects the disabled tokens from the conditional `DirectiveSyntax` nodes.
static void collectDisabledRegions(const slang::syntax::SyntaxNode& node,
                                   std::vector<slang::SourceRange>& result) {
    using namespace slang;
    using namespace slang::syntax;
    using namespace slang::parsing;
    for (uint32_t i = 0; i < node.getChildCount(); i++) {
        if (auto child = node.childNode(i)) {
            collectDisabledRegions(*child, result);
        }
        else {
            const auto token = const_cast<SyntaxNode&>(node).childTokenPtr(i);
            if (!token)
                continue;

            for (const auto& trivia : token->trivia()) {
                if (trivia.kind != TriviaKind::Directive)
                    continue;

                auto* syntax = trivia.syntax();
                if (!syntax)
                    continue;

                // Conditional Branch Directives (ie: `ifdef)
                else if (ConditionalBranchDirectiveSyntax::isKind(syntax->kind)) {
                    const auto& branch = syntax->as<ConditionalBranchDirectiveSyntax>();

                    const auto& tokens = branch.disabledTokens;
                    if (!tokens.empty()) {
                        const auto start = tokens[0].location();
                        const auto end = tokens.back().range().end();
                        result.push_back({start, end});
                    }
                }

                // Unconditional Branch Directives (ie: `else)
                else if (UnconditionalBranchDirectiveSyntax::isKind(syntax->kind)) {
                    const auto& branch = syntax->as<UnconditionalBranchDirectiveSyntax>();

                    const auto& tokens = branch.disabledTokens;
                    if (!tokens.empty()) {
                        const auto start = tokens[0].location();
                        const auto end = tokens.back().range().end();
                        result.push_back({start, end});
                    }
                }
            }
        }
    }
}

TEST_CASE("DisabledRegions_IfdefFalse") {
    using namespace slang;

    SourceManager sm;

    Bag options;

    auto tree = syntax::SyntaxTree::fromText(R"(
`ifdef FOO
logic a;
`else
logic b;
`endif
)",
                                             sm, "test", "", options);

    std::vector<SourceRange> disabled;
    collectDisabledRegions(tree->root(), disabled);

    REQUIRE(disabled.size() == 1);

    auto text = sm.getText(disabled[0]);
    INFO("Disabled text:\n" << text);

    CHECK(text.find("logic a;") != std::string::npos);
}

TEST_CASE("DisabledRegions_IfdefTrue") {
    using namespace slang;
    SourceManager sm;
    Bag options;

    auto tree = syntax::SyntaxTree::fromText(R"(
`define FOO
`ifdef FOO
logic a;
`else
logic b;
`endif
logic c;
)",
                                             sm, "test", "", options);

    std::vector<SourceRange> disabled;
    collectDisabledRegions(tree->root(), disabled);

    REQUIRE(disabled.size() == 1);

    auto text = sm.getText(disabled[0]);
    INFO("Disabled text:\n" << text);

    CHECK(text.find("logic b;") != std::string::npos);
}

TEST_CASE("DisabledRegions_Elsif") {
    using namespace slang;

    SourceManager sm;
    Bag options;

    auto tree = syntax::SyntaxTree::fromText(R"(
`define BAR
`ifdef FOO
logic a;
`elsif BAR
logic b;
`else
logic c;
`endif
logic d;
)",
                                             sm, "test", "", options);

    std::vector<SourceRange> disabled;
    collectDisabledRegions(tree->root(), disabled);

    REQUIRE(disabled.size() == 2);

    std::string combined;
    for (auto& r : disabled)
        combined += sm.getText(r);

    INFO(combined);

    CHECK(combined.find("logic a;") != std::string::npos);
    CHECK(combined.find("logic c;") != std::string::npos);
}
