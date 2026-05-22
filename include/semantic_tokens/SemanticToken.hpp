#pragma once

#include <cstdint>

#include "slang/parsing/Token.h"

/// TODO: Use slang enum instead of defining types
/// Or maybe generate with python first

enum class SemanticTokenType : uint32_t {
    Namespace = 0,
    Type = 1,
    Class = 2,
    Enum = 3,
    Interface = 4,
    Struct = 5,
    TypeParameter = 6,
    Parameter = 7,
    Variable = 8,
    Property = 9,
    EnumMember = 10,
    Event = 11,
    Function = 12,
    Method = 13,
    Macro = 14,
    Keyword = 15,
    Modifier = 16,
    Comment = 17,
    String = 18,
    Number = 19,
    Regexp = 20,
    Operator = 21,
    Decorator = 22,
};

enum class SemanticTokenModifier : uint32_t {
    Declaration = 0,
    Definition = 1,
    Readonly = 2,
    Static = 3,
    Deprecated = 4,
    Abstract = 5,
    Async = 6,
    Modification = 7,
    Documentation = 8,
    DefaultLibrary = 9,
};

const lsp::SemanticTokensLegend SemanticTokensLegendConfig = {
    .tokenTypes =
        {
            "namespace",     "type",      "class",    "enum",     "interface",  "struct",
            "typeParameter", "parameter", "variable", "property", "enumMember", "event",
            "function",      "method",    "macro",    "keyword",  "modifier",   "comment",
            "string",        "number",    "regexp",   "operator", "decorator",
        },
    .tokenModifiers = {
        "declaration",
        "definition",
        "readonly",
        "static",
        "deprecated",
        "abstract",
        "async",
        "modification",
        "documentation",
        "defaultLibrary",
    }};

static std::string_view semanticTokenTypeToString(uint32_t tokenType) {
    switch (static_cast<SemanticTokenType>(tokenType)) {
        case SemanticTokenType::Namespace:
            return "namespace";
        case SemanticTokenType::Type:
            return "type";
        case SemanticTokenType::Class:
            return "class";
        case SemanticTokenType::Enum:
            return "enum";
        case SemanticTokenType::Interface:
            return "interface";
        case SemanticTokenType::Struct:
            return "struct";
        case SemanticTokenType::TypeParameter:
            return "typeParameter";
        case SemanticTokenType::Parameter:
            return "parameter";
        case SemanticTokenType::Variable:
            return "variable";
        case SemanticTokenType::Property:
            return "property";
        case SemanticTokenType::EnumMember:
            return "enumMember";
        case SemanticTokenType::Event:
            return "event";
        case SemanticTokenType::Function:
            return "function";
        case SemanticTokenType::Method:
            return "method";
        case SemanticTokenType::Macro:
            return "macro";
        case SemanticTokenType::Keyword:
            return "keyword";
        case SemanticTokenType::Modifier:
            return "modifier";
        case SemanticTokenType::Comment:
            return "comment";
        case SemanticTokenType::String:
            return "string";
        case SemanticTokenType::Number:
            return "number";
        case SemanticTokenType::Regexp:
            return "regexp";
        case SemanticTokenType::Operator:
            return "operator";
        case SemanticTokenType::Decorator:
            return "decorator";
    }

    return "unknown";
}