#!/usr/bin/env python3

from __future__ import annotations

from pathlib import Path
import re
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
INPUT_FILE = SCRIPT_DIR / "syntax.yaml"
OUTPUT_FILE = SCRIPT_DIR / "Snippets.hpp"

CPP_SNIPPETS_NAME = "SV_MODULE_MEMBER_SNIPPETS"


def load_yaml(path: Path) -> dict[str, Any]:
    try:
        import yaml
    except ImportError as exc:
        raise SystemExit(
            "PyYAML is required to read syntax.yaml.\n"
            "Install it with: python -m pip install pyyaml"
        ) from exc

    with path.open("r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    if not isinstance(data, dict):
        raise TypeError(f"{path} must contain a YAML mapping at the top level")

    return data


# Matches ${foo}, but intentionally does not match LSP placeholders like ${1:name}.
TEMPLATE_RE = re.compile(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}")

# Matches a string that is exactly ${foo}.
# This lets us preserve non-string values like lists:
#
#   context:
#     - ModuleMember
#     - ${context_variant}
#
# where context_variant is:
#
#   - CompilationUnit
WHOLE_TEMPLATE_RE = re.compile(r"^\$\{([A-Za-z_][A-Za-z0-9_]*)\}$")


def substitute_template(value: Any, scope: dict[str, Any]) -> Any:
    if isinstance(value, list):
        result: list[Any] = []

        for item in value:
            substituted = substitute_template(item, scope)

            # Splice list-valued substitutions into the parent list.
            # Example:
            #   ["ModuleMember", "${context_variant}"]
            # becomes:
            #   ["ModuleMember", "CompilationUnit"]
            if isinstance(substituted, list):
                result.extend(substituted)
            else:
                result.append(substituted)

        return result

    if isinstance(value, dict):
        return {k: substitute_template(v, scope) for k, v in value.items()}

    if not isinstance(value, str):
        return value

    # If the entire string is exactly ${foo}, preserve the original type.
    # This is the key behavior needed for list-valued context_variant.
    match = WHOLE_TEMPLATE_RE.match(value)
    if match:
        key = match.group(1)
        return scope[key] if key in scope else value

    previous = None
    current = value

    while previous != current:
        previous = current

        def repl(match: re.Match[str]) -> str:
            key = match.group(1)
            return str(scope[key]) if key in scope else match.group(0)

        current = TEMPLATE_RE.sub(repl, current)

    return current


def normalize_body(value: Any) -> str:
    if isinstance(value, str):
        return value.rstrip("\n")

    if isinstance(value, list):
        if not all(isinstance(item, str) for item in value):
            raise TypeError("snippet body list must contain only strings")
        return "\n".join(value)

    raise TypeError(
        f"snippet body must be a string or list, got {type(value).__name__}"
    )


def normalize_contexts(value: Any) -> list[str]:
    if value is None:
        return []

    if isinstance(value, str):
        # Drop unresolved placeholders like ${context_variant}.
        if WHOLE_TEMPLATE_RE.fullmatch(value):
            return []
        return [value]

    if isinstance(value, list):
        result: list[str] = []

        for item in value:
            result.extend(normalize_contexts(item))

        # Deduplicate while preserving order.
        seen: set[str] = set()
        deduped: list[str] = []

        for ctx in result:
            if ctx not in seen:
                seen.add(ctx)
                deduped.append(ctx)

        return deduped

    raise TypeError(f"context must be a string or list, got {type(value).__name__}")


def get_label(scope: dict[str, Any]) -> str:
    for key in ("label", "tokens", "type"):
        if key in scope:
            return str(scope[key])

    raise KeyError(f"snippet entry is missing label/tokens/type: {scope}")


def get_body(scope: dict[str, Any]) -> str:
    for key in ("body", "insert", "insertText", "declaration"):
        if key in scope:
            return normalize_body(scope[key])

    raise KeyError(
        f"snippet entry is missing body/insert/insertText/declaration: {scope}"
    )


def markdown_inline_code(text: str) -> str:
    """
    Wrap text in Markdown inline code.

    If the documentation itself contains backticks, use a longer backtick fence
    so the Markdown remains valid.
    """
    max_run = max(
        (len(match.group(0)) for match in re.finditer(r"`+", text)), default=0
    )
    fence = "`" * (max_run + 1)
    return f"{fence}{text}{fence}"


def format_documentation(summary: str, body: str) -> str:
    """
    Format snippet documentation as Markdown:

        `summary`

        ```systemverilog
        body
        ```
    """
    summary = summary.strip()

    if not summary:
        summary = body.splitlines()[0] if body else ""

    return f"{markdown_inline_code(summary)}\n\n```systemverilog\n{body}\n```"


def normalize_snippet(entry: dict[str, Any]) -> dict[str, Any]:
    label = get_label(entry)
    insert_text = get_body(entry)
    documentation = str(entry.get("documentation", label))

    return {
        "label": label,
        "filterText": str(entry.get("filterText", label)),
        "insertText": insert_text,
        "documentation": format_documentation(documentation, insert_text),
        "context": normalize_contexts(entry.get("context")),
    }


def apply_defaults(defaults: dict[str, Any], variant: dict[str, Any]) -> dict[str, Any]:
    scope: dict[str, Any] = {**defaults, **variant}

    if "label" not in scope:
        if "tokens" in scope:
            scope["label"] = scope["tokens"]
        elif "type" in scope:
            scope["label"] = scope["type"]

    if "filterText" not in scope and "label" in scope:
        scope["filterText"] = scope["label"]

    substituted = {
        key: substitute_template(value, scope) for key, value in scope.items()
    }

    return {
        key: substitute_template(value, substituted)
        for key, value in substituted.items()
    }


def expand_concrete_snippets(snippets: Any) -> list[dict[str, Any]]:
    if not isinstance(snippets, list):
        raise TypeError("'snippets' must be a list")

    result: list[dict[str, Any]] = []

    for snippet in snippets:
        if not isinstance(snippet, dict):
            raise TypeError(f"snippet entry must be a mapping: {snippet!r}")

        result.append(normalize_snippet(snippet))

    return result


def expand_variants(defaults: Any, variants: Any) -> list[dict[str, Any]]:
    if not isinstance(defaults, dict):
        raise TypeError("'defaults' must be a mapping")
    if not isinstance(variants, list):
        raise TypeError("'variants' must be a list")

    result: list[dict[str, Any]] = []

    for variant in variants:
        if not isinstance(variant, dict):
            raise TypeError(f"variant must be a mapping: {variant!r}")

        result.append(normalize_snippet(apply_defaults(defaults, variant)))

    return result


def expand_group(name: str, group: Any) -> list[dict[str, Any]]:
    if not isinstance(group, dict):
        raise TypeError(f"repository group {name!r} must be a mapping")

    has_snippets = "snippets" in group
    has_variants = "variants" in group

    if has_snippets and has_variants:
        raise ValueError(
            f"repository group {name!r} cannot contain both 'snippets' and 'variants'"
        )

    if has_snippets:
        return expand_concrete_snippets(group["snippets"])

    if has_variants:
        return expand_variants(group.get("defaults", {}), group["variants"])

    raise ValueError(
        f"repository group {name!r} must contain either 'snippets' or 'variants'"
    )


def expand_repository(data: dict[str, Any]) -> list[dict[str, Any]]:
    repository = data.get("repository", {})
    if not isinstance(repository, dict):
        raise TypeError("'repository' must be a mapping")

    snippets: list[dict[str, Any]] = []

    for name, group in repository.items():
        snippets.extend(expand_group(str(name), group))

    return snippets


# -----------------------------------------------------------------------------
# C++ emission
# -----------------------------------------------------------------------------


def cpp_escape_single_line(text: str) -> str:
    return (
        text.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\t", "\\t")
        .replace("\r", "")
    )


def cpp_string_literal(
    text: str,
    *,
    multiline: bool = False,
    indent: str = "",
) -> str:
    if not multiline or "\n" not in text:
        return f'"{cpp_escape_single_line(text)}"'

    lines = text.split("\n")
    rendered: list[str] = []

    for i, line in enumerate(lines):
        prefix = "" if i == 0 else indent
        suffix = "\\n" if i != len(lines) - 1 else ""
        rendered.append(f'{prefix}"{cpp_escape_single_line(line)}{suffix}"')

    return "\n".join(rendered)


def cpp_context_expr(contexts: list[str]) -> str:
    if not contexts:
        return "toMask(CompletionContextKind::Unknown)"

    return " | ".join(f"toMask(CompletionContextKind::{ctx})" for ctx in contexts)


def emit_cpp(snippets: list[dict[str, Any]]) -> str:
    lines: list[str] = [
        "//------------------------------------------------------------------------------",
        "// <auto-generated>",
        "//     Generated by gen_sv_snippets.py from syntax.yaml.",
        "//     Do not edit this file directly.",
        "// </auto-generated>",
        "//------------------------------------------------------------------------------",
        "",
        "#pragma once",
        "",
        '#include "completions/CompletionContext.h"',
        "",
        "#include <string_view>",
        "",
        "using namespace server;",
        "",
        "struct SVSnippet {",
        "    std::string_view label;",
        "    std::string_view filterText;",
        "    std::string_view insertText;",
        "    std::string_view documentation;",
        "    CompletionContextMask context;",
        "};",
        "",
        f"static constexpr SVSnippet {CPP_SNIPPETS_NAME}[] = {{",
    ]

    for snippet in snippets:
        lines.extend(
            [
                "    {",
                f"        .label = {cpp_string_literal(snippet['label'])},",
                f"        .filterText = {cpp_string_literal(snippet['filterText'])},",
                f"        .insertText = {cpp_string_literal(snippet['insertText'], multiline=True, indent='                      ')},",
                f"        .documentation = {cpp_string_literal(snippet['documentation'], multiline=True, indent='                         ')},",
                f"        .context = {cpp_context_expr(snippet['context'])},",
                "    },",
            ]
        )

    lines.extend(["};", ""])

    return "\n".join(lines)


def main() -> None:
    if not INPUT_FILE.exists():
        raise FileNotFoundError(INPUT_FILE)

    data = load_yaml(INPUT_FILE)
    snippets = expand_repository(data)

    OUTPUT_FILE.write_text(emit_cpp(snippets), encoding="utf-8")
    print(f"Wrote {OUTPUT_FILE}")


if __name__ == "__main__":
    main()
