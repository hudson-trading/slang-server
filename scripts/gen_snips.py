#!/usr/bin/env python3

from __future__ import annotations

from pathlib import Path
import re
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
INPUT_FILE = SCRIPT_DIR / "syntax.yaml"
OUTPUT_FILE = SCRIPT_DIR / "GeneratedSVSnippets.hpp"

CONTEXT_NAME = "module_member"
CPP_KEYWORDS_NAME = "SV_MODULE_MEMBER_KEYWORDS"
CPP_SNIPPETS_NAME = "SV_MODULE_MEMBER_SNIPPETS"


def load_yaml(path: Path) -> dict[str, Any]:
    try:
        import yaml
    except ImportError as exc:
        raise SystemExit(
            "PyYAML is required to read sv_snippets.yaml.\n"
            "Install it with: python -m pip install pyyaml"
        ) from exc

    with path.open("r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    if not isinstance(data, dict):
        raise TypeError(f"{path} must contain a YAML mapping at the top level")

    return data


# Matches ${foo}, but intentionally does not match snippet placeholders like
# ${1:name}. This lets YAML templates contain both generator variables and LSP
# snippet placeholders without conflict.
TEMPLATE_RE = re.compile(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}")


def substitute_template(value: Any, scope: dict[str, Any]) -> Any:
    """Substitute ${name}-style generator variables in strings."""
    if isinstance(value, list):
        return [substitute_template(item, scope) for item in value]

    if isinstance(value, dict):
        return {k: substitute_template(v, scope) for k, v in value.items()}

    if not isinstance(value, str):
        return value

    previous = None
    current = value

    # Allow nested templates:
    #   body: "${tokens} ${1:${name}};"
    #   name: "${kind}_name"
    while previous != current:
        previous = current

        def repl(match: re.Match[str]) -> str:
            key = match.group(1)
            if key not in scope:
                return match.group(0)
            return str(scope[key])

        current = TEMPLATE_RE.sub(repl, current)

    return current


def normalize_body(value: Any) -> str:
    """Accept either a YAML string or list-of-lines for snippet body."""
    if isinstance(value, str):
        return value.rstrip("\n")

    if isinstance(value, list):
        if not all(isinstance(item, str) for item in value):
            raise TypeError("snippet body list must contain only strings")
        return "\n".join(value)

    raise TypeError(
        f"snippet body must be a string or list, got {type(value).__name__}"
    )


def label_from_scope(scope: dict[str, Any]) -> str:
    if "label" in scope:
        return str(scope["label"])
    if "tokens" in scope:
        return str(scope["tokens"])
    if "type" in scope:
        return str(scope["type"])
    raise KeyError(f"snippet entry is missing label/tokens/type: {scope}")


def body_from_scope(scope: dict[str, Any]) -> str:
    # Prefer body, but accept insert / insertText if that is more convenient.
    if "body" in scope:
        return normalize_body(scope["body"])
    if "insert" in scope:
        return normalize_body(scope["insert"])
    if "insertText" in scope:
        return normalize_body(scope["insertText"])
    if "declaration" in scope:
        return normalize_body(scope["declaration"])

    raise KeyError(
        f"snippet entry is missing body/insert/insertText/declaration: {scope}"
    )


def normalize_snippet(entry: dict[str, Any]) -> dict[str, str]:
    """
    Convert a fully expanded YAML entry into the exact fields required by C++.

    Output shape:
        {
          "label": "...",
          "filterText": "...",
          "insertText": "...",
          "documentation": "..."
        }
    """
    label = label_from_scope(entry)
    filter_text = str(entry.get("filterText", label))
    insert_text = body_from_scope(entry)
    documentation = str(entry.get("documentation", ""))

    return {
        "label": label,
        "filterText": filter_text,
        "insertText": insert_text,
        "documentation": documentation,
    }


def apply_defaults(defaults: dict[str, Any], variant: dict[str, Any]) -> dict[str, Any]:
    """
    Merge defaults and a variant, then substitute generator variables.

    Variant keys override default keys.
    """
    scope: dict[str, Any] = {**defaults, **variant}

    # Derive label/filterText from tokens/type when omitted.
    if "label" not in scope:
        if "tokens" in scope:
            scope["label"] = scope["tokens"]
        elif "type" in scope:
            scope["label"] = scope["type"]

    if "filterText" not in scope and "label" in scope:
        scope["filterText"] = scope["label"]

    # Now that derived fields exist, substitute all string values.
    substituted = {
        key: substitute_template(value, scope) for key, value in scope.items()
    }

    # A substituted value can introduce another template, so run once more over
    # the substituted scope.
    return {
        key: substitute_template(value, substituted)
        for key, value in substituted.items()
    }


def expand_repository_entry(entry: dict[str, Any]) -> list[dict[str, str]]:
    """
    Expand one repository entry.

    Supported forms:

      snippets:
        - label: class
          body: ...

      defaults:
        name: struct_name
        body: ...
      variants:
        - tokens: struct
        - tokens: typedef struct packed
    """
    result: list[dict[str, str]] = []

    # Plain list of concrete snippets.
    if "snippets" in entry:
        snippets = entry["snippets"]
        if not isinstance(snippets, list):
            raise TypeError("'snippets' must be a list")

        for snippet_entry in snippets:
            if not isinstance(snippet_entry, dict):
                raise TypeError(f"snippet entry must be a mapping: {snippet_entry!r}")

            if "include" in snippet_entry:
                raise ValueError(
                    "Nested includes inside repository snippets are not supported here. "
                    "Put includes in contexts instead."
                )

            result.append(normalize_snippet(snippet_entry))

    # Defaults + variants expansion.
    if "variants" in entry:
        defaults = entry.get("defaults", {})
        variants = entry["variants"]

        if not isinstance(defaults, dict):
            raise TypeError("'defaults' must be a mapping")
        if not isinstance(variants, list):
            raise TypeError("'variants' must be a list")

        for variant in variants:
            if not isinstance(variant, dict):
                raise TypeError(f"variant must be a mapping: {variant!r}")

            expanded = apply_defaults(defaults, variant)
            result.append(normalize_snippet(expanded))

    return result


def resolve_include(data: dict[str, Any], include: str) -> dict[str, Any]:
    if not include.startswith("#"):
        raise ValueError(f"only repository includes are supported: {include!r}")

    name = include[1:]
    repository = data.get("repository", {})

    if not isinstance(repository, dict):
        raise TypeError("'repository' must be a mapping")

    if name not in repository:
        raise KeyError(f"unknown repository include: {include}")

    entry = repository[name]
    if not isinstance(entry, dict):
        raise TypeError(f"repository entry {include} must be a mapping")

    return entry


def expand_context(
    data: dict[str, Any], context_name: str
) -> tuple[list[str], list[dict[str, str]]]:
    contexts = data.get("contexts", {})
    if not isinstance(contexts, dict):
        raise TypeError("'contexts' must be a mapping")

    if context_name not in contexts:
        raise KeyError(f"missing context: {context_name}")

    context = contexts[context_name]
    if not isinstance(context, dict):
        raise TypeError(f"context {context_name!r} must be a mapping")

    keywords = context.get("keywords", [])
    if not isinstance(keywords, list):
        raise TypeError(f"context {context_name!r} keywords must be a list")

    snippets: list[dict[str, str]] = []

    entries = context.get("snippets", [])
    if not isinstance(entries, list):
        raise TypeError(f"context {context_name!r} snippets must be a list")

    for item in entries:
        if not isinstance(item, dict):
            raise TypeError(f"context snippet item must be a mapping: {item!r}")

        if "include" in item:
            repo_entry = resolve_include(data, str(item["include"]))
            snippets.extend(expand_repository_entry(repo_entry))
        else:
            snippets.append(normalize_snippet(item))

    return [str(keyword) for keyword in keywords], snippets


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
    rendered = []

    for i, line in enumerate(lines):
        prefix = "" if i == 0 else indent
        suffix = "\\n" if i != len(lines) - 1 else ""
        rendered.append(f'{prefix}"{cpp_escape_single_line(line)}{suffix}"')

    return "\n".join(rendered)


def emit_cpp(keywords: list[str], snippets: list[dict[str, str]]) -> str:
    lines: list[str] = [
        "//------------------------------------------------------------------------------",
        "// <auto-generated>",
        "//     Generated by gen_sv_snippets.py from sv_snippets.yaml.",
        "//     Do not edit this file directly.",
        "// </auto-generated>",
        "//------------------------------------------------------------------------------",
        "",
        "struct SVSnippet {",
        "    std::string_view label;",
        "    std::string_view filterText;",
        "    std::string_view insertText;",
        "    std::string_view documentation;",
        "};",
        "",
        "/// List of SystemVerilog keywords suitable for prepending to the LHS of an expression.",
        f"static constexpr std::string_view {CPP_KEYWORDS_NAME}[] = {{",
    ]

    for keyword in keywords:
        lines.append(f"    {cpp_string_literal(keyword)},")

    lines.extend(
        [
            "};",
            "",
            f"static constexpr SVSnippet {CPP_SNIPPETS_NAME}[] = {{",
        ]
    )

    for snippet in snippets:
        lines.extend(
            [
                "    {",
                f"        .label = {cpp_string_literal(snippet['label'])},",
                f"        .filterText = {cpp_string_literal(snippet['filterText'])},",
                f"        .insertText = {cpp_string_literal(snippet['insertText'], multiline=True, indent='                      ')},",
                f"        .documentation = {cpp_string_literal(snippet['documentation'])},",
                "    },",
            ]
        )

    lines.extend(
        [
            "};",
            "",
        ]
    )

    return "\n".join(lines)


def main() -> None:
    if not INPUT_FILE.exists():
        raise FileNotFoundError(INPUT_FILE)

    data = load_yaml(INPUT_FILE)
    keywords, snippets = expand_context(data, CONTEXT_NAME)

    OUTPUT_FILE.write_text(emit_cpp(keywords, snippets), encoding="utf-8")
    print(f"Wrote {OUTPUT_FILE}")


if __name__ == "__main__":
    main()
