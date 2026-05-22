#!/usr/bin/env python3

from __future__ import annotations

from argparse import ArgumentParser
from pathlib import Path
import re
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
CPP_SNIPPETS_NAME = "SV_MODULE_MEMBER_SNIPPETS"

TEMPLATE_RE = re.compile(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}")
WHOLE_TEMPLATE_RE = re.compile(r"^\$\{([A-Za-z_][A-Za-z0-9_]*)\}$")


def parse_args() -> tuple[Path, Path]:
    parser = ArgumentParser(description="Generate SystemVerilog snippet header")
    parser.add_argument(
        "input", nargs="?", type=Path, default=SCRIPT_DIR / "syntax.yaml"
    )
    parser.add_argument("--outDir", type=Path, default=SCRIPT_DIR)
    args = parser.parse_args()
    return args.input, args.outDir / "Snippets.hpp"


def load_yaml(path: Path) -> dict[str, Any]:
    try:
        import yaml
    except ImportError as exc:
        raise SystemExit(
            "PyYAML is required to read syntax.yaml.\n"
            "Install it with: python -m pip install pyyaml"
        ) from exc

    with path.open(encoding="utf-8") as f:
        data = yaml.safe_load(f)

    if not isinstance(data, dict):
        raise TypeError(f"{path} must contain a YAML mapping at the top level")
    return data


def substitute(value: Any, scope: dict[str, Any]) -> Any:
    if isinstance(value, list):
        out: list[Any] = []
        for item in map(lambda x: substitute(x, scope), value):
            out.extend(item if isinstance(item, list) else [item])
        return out

    if isinstance(value, dict):
        return {k: substitute(v, scope) for k, v in value.items()}

    if not isinstance(value, str):
        return value

    if whole := WHOLE_TEMPLATE_RE.fullmatch(value):
        return scope.get(whole.group(1), value)

    old = None
    while value != old:
        old = value
        value = TEMPLATE_RE.sub(lambda m: str(scope.get(m.group(1), m.group(0))), value)
    return value


def required(entry: dict[str, Any], names: tuple[str, ...], label: str) -> Any:
    for name in names:
        if name in entry:
            return entry[name]
    raise KeyError(f"snippet entry is missing {label}: {entry}")


def normalize_body(value: Any) -> str:
    if isinstance(value, str):
        return value.rstrip("\n")
    if isinstance(value, list) and all(isinstance(x, str) for x in value):
        return "\n".join(value)
    raise TypeError(
        f"snippet body must be a string or string list, got {type(value).__name__}"
    )


def normalize_contexts(value: Any) -> list[str]:
    if value is None:
        return []

    if isinstance(value, str):
        if WHOLE_TEMPLATE_RE.fullmatch(value):
            return []
        if TEMPLATE_RE.search(value):
            raise ValueError(f"unresolved template in context: {value!r}")
        return [value]

    if not isinstance(value, list):
        raise TypeError(f"context must be a string or list, got {type(value).__name__}")

    result = [ctx for item in value for ctx in normalize_contexts(item)]
    return list(dict.fromkeys(result))


def inline_code(text: str) -> str:
    fence = "`" * (max((len(m[0]) for m in re.finditer(r"`+", text)), default=0) + 1)
    return f"{fence}{text}{fence}"


def documentation(summary: str, body: str) -> str:
    summary = summary.strip() or (body.splitlines()[0] if body else "")
    return f"{inline_code(summary)}\n\n```systemverilog\n{body}\n```"


def apply_defaults(defaults: dict[str, Any], variant: dict[str, Any]) -> dict[str, Any]:
    scope = defaults | variant
    scope.setdefault("label", scope.get("tokens", scope.get("type")))

    if "label" in scope:
        scope.setdefault("filterText", scope["label"])

    once = {k: substitute(v, scope) for k, v in scope.items()}
    return {k: substitute(v, once) for k, v in once.items()}


def normalize_snippet(entry: dict[str, Any]) -> dict[str, Any]:
    label = str(required(entry, ("label", "tokens", "type"), "label/tokens/type"))
    body = normalize_body(
        required(entry, ("body", "insert", "insertText", "declaration"), "body")
    )

    description = str(entry.get("description", label))
    return {
        "label": label,
        "detail": str(entry.get("detail", "")),
        "description": description,
        "filterText": str(entry.get("filterText", label)),
        "insertText": body,
        "documentation": documentation(
            str(entry.get("documentation", description)), body
        ),
        "context": normalize_contexts(entry.get("context")),
    }


def expand_group(name: str, group: Any) -> list[dict[str, Any]]:
    if isinstance(group, list):
        entries = group
    elif isinstance(group, dict) and "variants" in group:
        defaults = group.get("defaults", group.get("default", {}))
        variants = group["variants"]

        if not isinstance(defaults, dict):
            raise TypeError(f"repository group {name!r} defaults must be a mapping")
        if not isinstance(variants, list):
            raise TypeError(f"repository group {name!r} variants must be a list")

        entries = [apply_defaults(defaults, variant) for variant in variants]
    elif isinstance(group, dict):
        raise ValueError(
            f"repository group {name!r} must be a list or contain variants"
        )
    else:
        raise TypeError(f"repository group {name!r} must be a mapping or list")

    if not all(isinstance(entry, dict) for entry in entries):
        raise TypeError(f"repository group {name!r} contains a non-mapping entry")

    return [normalize_snippet(entry) for entry in entries]


def expand_repository(data: dict[str, Any]) -> list[dict[str, Any]]:
    repository = data.get("repository", {})
    if not isinstance(repository, dict):
        raise TypeError("'repository' must be a mapping")

    return [
        snippet
        for name, group in repository.items()
        for snippet in expand_group(str(name), group)
    ]


def cpp_escape(text: str) -> str:
    return (
        text.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\t", "\\t")
        .replace("\r", "")
    )


def cpp_string(text: str, *, multiline: bool = False, indent: str = "") -> str:
    if not multiline or "\n" not in text:
        return f'"{cpp_escape(text)}"'

    lines = text.split("\n")
    return "\n".join(
        f'{"" if i == 0 else indent}"{cpp_escape(line)}{"\\n" if i + 1 < len(lines) else ""}"'
        for i, line in enumerate(lines)
    )


def cpp_context(contexts: list[str]) -> str:
    if not contexts:
        return "server::toMask(server::CompletionContextKind::Unknown)"

    return " | ".join(
        f"server::toMask(server::CompletionContextKind::{ctx})" for ctx in contexts
    )


def emit_cpp(snippets: list[dict[str, Any]]) -> str:
    lines = [
        "//------------------------------------------------------------------------------",
        "// <auto-generated>",
        "//     Generated by gen_snippets.py from syntax.yaml.",
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
        "struct SVSnippet {",
        "    std::string_view label;",
        "    std::string_view detail;",
        "    std::string_view description;",
        "    std::string_view filterText;",
        "    std::string_view insertText;",
        "    std::string_view documentation;",
        "    server::CompletionContextMask context;",
        "};",
        "",
        f"static constexpr SVSnippet {CPP_SNIPPETS_NAME}[] = {{",
    ]

    for s in snippets:
        lines.extend(
            [
                "    {",
                f"        .label = {cpp_string(s['label'])},",
                f"        .detail = {cpp_string(s['detail'])},",
                f"        .description = {cpp_string(s['description'])},",
                f"        .filterText = {cpp_string(s['filterText'])},",
                f"        .insertText = {cpp_string(s['insertText'], multiline=True, indent='                      ')},",
                f"        .documentation = {cpp_string(s['documentation'], multiline=True, indent='                         ')},",
                f"        .context = {cpp_context(s['context'])},",
                "    },",
            ]
        )

    return "\n".join(lines + ["};", ""])


def main() -> None:
    input_file, output_file = parse_args()
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(
        emit_cpp(expand_repository(load_yaml(input_file))), encoding="utf-8"
    )
    print(f"Wrote {output_file}")


if __name__ == "__main__":
    main()
