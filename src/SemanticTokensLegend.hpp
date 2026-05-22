#include "lsp/LspServer.h"

static lsp::SemanticTokensLegend makeSemanticTokensLegend() {
    return lsp::SemanticTokensLegend{
        .tokenTypes =
            {
                "namespace",     "type",      "class",    "enum",     "interface",  "struct",
                "typeParameter", "parameter", "variable", "property", "enumMember", "event",
                "function",      "method",    "macro",    "keyword",  "modifier",   "comment",
                "string",        "number",    "regexp",   "operator", "decorator",
            },
        .tokenModifiers =
            {
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
            },
    };
}