//------------------------------------------------------------------------------
// SystemTaskCompletions.cpp
// Completions for SystemVerilog built-in system tasks and functions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#include "completions/SystemTaskCompletions.h"

#include "SystemTaskDocs.h"
#include "lsp/LspTypes.h"
#include "lsp/SnippetString.h"
#include "util/Markdown.h"
#include <cctype>
#include <optional>
#include <rfl/Result.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "slang/ast/SystemSubroutine.h"

namespace server::completions {
using namespace slang;

namespace {

struct SignatureArgs {
    enum class Kind { CallArgs, WithClause };

    Kind kind;
    std::string_view args;
};

static std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

static bool isNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

static bool hasNameBoundaryBefore(std::string_view value, size_t pos) {
    return pos == 0 || !isNameChar(value[pos - 1]);
}

static bool hasNameBoundaryAfter(std::string_view value, size_t pos) {
    return pos == value.size() || !isNameChar(value[pos]);
}

static std::string getSystemSubroutineInsertText(std::string_view label) {
    // Strip the leading `$`: the editor's default word pattern excludes `$`, so the prefix
    // sits outside the replacement range when accepting. Leaving it in produces `$$display(...)`.
    if (!label.empty() && label.front() == '$')
        label.remove_prefix(1);
    return std::string(label);
}

static std::string_view firstSignature(std::string_view signature) {
    auto newline = signature.find('\n');
    if (newline != std::string_view::npos)
        signature = signature.substr(0, newline);
    return trim(signature);
}

static size_t findSystemSubroutineName(std::string_view signature, std::string_view label,
                                       std::string_view insertText) {
    for (auto name : {label, insertText}) {
        if (name.empty())
            continue;

        size_t pos = 0;
        while ((pos = signature.find(name, pos)) != std::string_view::npos) {
            auto end = pos + name.size();
            if (hasNameBoundaryBefore(signature, pos) && hasNameBoundaryAfter(signature, end))
                return end;
            pos = end;
        }
    }
    return std::string_view::npos;
}

static std::optional<std::string_view> findBalancedContents(std::string_view value, size_t openPos,
                                                            char openChar, char closeChar) {
    SLANG_ASSERT(openPos < value.size() && value[openPos] == openChar);

    int depth = 0;
    for (size_t i = openPos; i < value.size(); ++i) {
        if (value[i] == openChar) {
            depth++;
        }
        else if (value[i] == closeChar) {
            depth--;
            if (depth == 0)
                return value.substr(openPos + 1, i - openPos - 1);
        }
    }
    return std::nullopt;
}

static std::optional<size_t> findBalancedClose(std::string_view value, size_t openPos,
                                               char openChar, char closeChar) {
    SLANG_ASSERT(openPos < value.size() && value[openPos] == openChar);

    int depth = 0;
    for (size_t i = openPos; i < value.size(); ++i) {
        if (value[i] == openChar) {
            depth++;
        }
        else if (value[i] == closeChar) {
            depth--;
            if (depth == 0)
                return i;
        }
    }
    return std::nullopt;
}

static std::string stripOptionalSegments(std::string_view value) {
    std::string result;
    result.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '[') {
            result.push_back(value[i]);
            continue;
        }

        auto close = findBalancedClose(value, i, '[', ']');
        if (!close) {
            result.push_back(value[i]);
            continue;
        }

        i = *close;
    }

    return std::string(trim(result));
}

static std::optional<SignatureArgs> getSignatureArgs(std::string_view label,
                                                     const SystemTaskDoc& doc) {
    auto signature = firstSignature(doc.signature);
    auto insertText = getSystemSubroutineInsertText(label);
    auto nameEnd = findSystemSubroutineName(signature, label, insertText);
    if (nameEnd == std::string_view::npos)
        return std::nullopt;

    auto rest = trim(signature.substr(nameEnd));
    if (rest.starts_with("with "))
        return SignatureArgs{SignatureArgs::Kind::WithClause, rest};

    if (rest.empty() || (rest.front() != '(' && rest.front() != '['))
        return std::nullopt;

    if (rest.front() == '[')
        return SignatureArgs{SignatureArgs::Kind::CallArgs, ""};

    if (rest.front() == '(') {
        auto args = findBalancedContents(rest, 0, '(', ')');
        if (!args)
            return std::nullopt;
        return SignatureArgs{SignatureArgs::Kind::CallArgs, *args};
    }

    return std::nullopt;
}

static std::vector<std::string_view> splitSignatureArgs(std::string_view args) {
    std::vector<std::string_view> result;
    size_t start = 0;
    int parenDepth = 0;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == '(') {
            parenDepth++;
        }
        else if (args[i] == ')' && parenDepth > 0) {
            parenDepth--;
        }
        else if (args[i] == ',' && parenDepth == 0) {
            result.push_back(trim(args.substr(start, i - start)));
            start = i + 1;
        }
    }

    result.push_back(trim(args.substr(start)));
    return result;
}

static std::string_view stripOptionalArgMarkers(std::string_view arg) {
    arg = trim(arg);
    while (!arg.empty() && (arg.front() == '[' || arg.front() == ',')) {
        arg.remove_prefix(1);
        arg = trim(arg);
    }
    while (!arg.empty() && (arg.back() == '[' || arg.back() == ']')) {
        arg.remove_suffix(1);
        arg = trim(arg);
    }
    return arg;
}

static std::string_view stripDefaultValue(std::string_view arg) {
    int parenDepth = 0;
    int bracketDepth = 0;

    for (size_t i = 0; i < arg.size(); ++i) {
        switch (arg[i]) {
            case '(':
                parenDepth++;
                break;
            case ')':
                if (parenDepth > 0)
                    parenDepth--;
                break;
            case '[':
                bracketDepth++;
                break;
            case ']':
                if (bracketDepth > 0)
                    bracketDepth--;
                break;
            case '=':
                if (parenDepth == 0 && bracketDepth == 0)
                    return trim(arg.substr(0, i));
                break;
            default:
                break;
        }
    }

    return trim(arg);
}

static bool hasDefaultValue(std::string_view arg) {
    return stripDefaultValue(arg).size() != trim(arg).size();
}

static std::string getWithClausePlaceholder(std::string_view value) {
    auto open = value.find('(');
    if (open == std::string_view::npos)
        return "item.expr";

    auto contents = findBalancedContents(value, open, '(', ')');
    if (!contents)
        return "item.expr";

    auto placeholder = trim(*contents);
    if (placeholder.empty())
        return "item.expr";

    return std::string(placeholder);
}

static std::optional<std::string> getArgumentPlaceholder(std::string_view arg) {
    arg = stripDefaultValue(stripOptionalArgMarkers(arg));
    if (arg.empty())
        return std::nullopt;

    if (arg == "...")
        return std::string();

    size_t end = arg.size();
    while (end > 0 && !isNameChar(arg[end - 1]))
        end--;

    if (end == 0)
        return std::nullopt;

    size_t start = end;
    while (start > 0 && isNameChar(arg[start - 1]))
        start--;

    return std::string(arg.substr(start, end - start));
}

static bool shouldQuotePlaceholder(std::string_view placeholder) {
    return placeholder == "format";
}

static bool shouldIncludeDefaultPlaceholder(std::string_view placeholder,
                                            bool skippedDefaultPlaceholder) {
    return placeholder == "format" && !skippedDefaultPlaceholder;
}

static std::string buildSnippetFromArgs(std::string_view insertText, SignatureArgs signatureArgs) {
    SnippetString snippet;
    snippet.appendText(insertText);

    if (signatureArgs.kind == SignatureArgs::Kind::WithClause) {
        snippet.appendText(" with (");
        snippet.appendPlaceholder(getWithClausePlaceholder(signatureArgs.args));
        snippet.appendText(")");
        return std::string(snippet.getValue());
    }

    auto args = stripOptionalSegments(signatureArgs.args);

    snippet.appendText("(");
    bool needsComma = false;
    bool hasFinalTabstop = false;
    bool skippedDefaultPlaceholder = false;
    for (auto arg : splitSignatureArgs(args)) {
        auto placeholder = getArgumentPlaceholder(arg);
        if (!placeholder)
            continue;

        auto defaulted = hasDefaultValue(stripOptionalArgMarkers(arg));
        if (placeholder->empty() && skippedDefaultPlaceholder)
            continue;

        if (defaulted &&
            !shouldIncludeDefaultPlaceholder(*placeholder, skippedDefaultPlaceholder)) {
            skippedDefaultPlaceholder = true;
            continue;
        }

        if (needsComma)
            snippet.appendText(", ");

        if (placeholder->empty()) {
            snippet.appendTabstop(0);
            hasFinalTabstop = true;
        }
        else if (shouldQuotePlaceholder(*placeholder)) {
            snippet.appendText("\"");
            snippet.appendPlaceholder(*placeholder);
            snippet.appendText("\"");
        }
        else {
            snippet.appendPlaceholder(*placeholder);
        }

        needsComma = true;
    }

    if (!needsComma && args == "...")
        snippet.appendTabstop(0);
    else if (needsComma && args.find("...") != std::string_view::npos && !hasFinalTabstop) {
        snippet.appendText(", ");
        snippet.appendTabstop(0);
    }

    snippet.appendText(")");
    return std::string(snippet.getValue());
}

static std::string getSystemSubroutineSnippet(std::string_view label, const SystemTaskDoc* doc) {

    auto insertText = getSystemSubroutineInsertText(label);

    if (!doc)
        return insertText;

    if (auto args = getSignatureArgs(label, *doc))
        return buildSnippetFromArgs(insertText, *args);

    SnippetString snippet;
    snippet.appendText(insertText);
    return std::string(snippet.getValue());
}

} // namespace

lsp::CompletionItem getSystemSubroutineCompletion(parsing::KnownSystemName name,
                                                  const ast::SystemSubroutine& subroutine) {
    auto label = std::string(parsing::toString(name));

    std::optional<rfl::Variant<std::string, lsp::MarkupContent>> documentation;
    auto isTask = subroutine.kind == ast::SubroutineKind::Task;
    std::string detail = isTask ? " task" : " function";
    auto* doc = getSystemTaskDoc(name);
    if (doc) {
        detail = " " + std::string(doc->signature);
        markup::Document md;
        md.addParagraph().appendCodeBlock(doc->signature);
        if (!doc->description.empty())
            md.addParagraph().appendText(doc->description);
        documentation = md.build();
    }

    return lsp::CompletionItem{
        .label = label,
        .labelDetails =
            lsp::CompletionItemLabelDetails{
                .detail = detail,
            },
        .kind = lsp::CompletionItemKind::Function,
        .documentation = documentation,
        .filterText = getSystemSubroutineInsertText(label),
        .insertText = getSystemSubroutineSnippet(label, doc),
        .insertTextFormat = lsp::InsertTextFormat::Snippet,
    };
}

void addSystemSubroutineCompletions(std::vector<lsp::CompletionItem>& results,
                                    const ast::Compilation& compilation) {
    // The built-in system subroutines slang registers are identical across compilations, so
    // build the completion list once on the first call and reuse it.
    static const std::vector<lsp::CompletionItem> cached = [&] {
        std::vector<lsp::CompletionItem> items;
        for (auto name : parsing::KnownSystemName_traits::values) {
            if (name == parsing::KnownSystemName::Unknown)
                continue;

            auto label = parsing::toString(name);
            if (label.empty() || label[0] != '$')
                continue;

            auto* subroutine = compilation.getSystemSubroutine(name);
            if (!subroutine)
                continue;

            items.push_back(getSystemSubroutineCompletion(name, *subroutine));
        }
        return items;
    }();

    results.insert(results.end(), cached.begin(), cached.end());
}

bool inSystemTaskIdent(std::string_view prevText) {
    for (auto it = prevText.rbegin(); it != prevText.rend(); ++it) {
        char c = *it;
        if (c == '$')
            return true;
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
    }
    return false;
}

} // namespace server::completions
