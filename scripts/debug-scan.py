#!/usr/bin/env python3
# SPDX-FileCopyrightText: Hudson River Trading
# SPDX-License-Identifier: MIT
"""Scan HDL files through slang-server and group coverage findings for agent triage."""

import argparse
import asyncio
import fnmatch
import json
import os
from pathlib import Path
import sys
import tempfile


class ScanClient:
    """Sequential JSON-RPC client that handles server requests and diagnostics while waiting."""

    def __init__(self, process, timeout):
        self.process = process
        self.timeout = timeout
        self.next_id = 0
        self.diagnostics = {}
        self.messages = []

    async def send(self, message):
        payload = json.dumps({"jsonrpc": "2.0", **message}).encode()
        self.process.stdin.write(
            f"Content-Length: {len(payload)}\r\n\r\n".encode() + payload
        )
        await self.process.stdin.drain()

    async def notify(self, method, params):
        await asyncio.wait_for(
            self.send({"method": method, "params": params}), self.timeout
        )

    async def request(self, method, params):
        self.next_id += 1
        request_id = self.next_id

        async def exchange():
            await self.send({"id": request_id, "method": method, "params": params})
            while True:
                headers = await self.process.stdout.readuntil(b"\r\n\r\n")
                length = next(
                    int(line.split(b":", 1)[1])
                    for line in headers.split(b"\r\n")
                    if line.lower().startswith(b"content-length:")
                )
                message = json.loads(await self.process.stdout.readexactly(length))
                if "method" in message:
                    if "id" in message:
                        if message["method"] == "client/registerCapability":
                            await self.send({"id": message["id"], "result": None})
                        else:
                            await self.send(
                                {
                                    "id": message["id"],
                                    "error": {
                                        "code": -32601,
                                        "message": "Unsupported request",
                                    },
                                }
                            )
                    elif message["method"] == "textDocument/publishDiagnostics":
                        data = message["params"]
                        self.diagnostics[data["uri"]] = data["diagnostics"]
                    elif message["method"] == "window/showMessage":
                        self.messages.append(message["params"])
                elif message.get("id") == request_id:
                    if "error" in message:
                        raise RuntimeError(f"{method}: {message['error']}")
                    return message.get("result")

        return await asyncio.wait_for(exchange(), self.timeout)


def discover(target, workspace, excludes):
    """Enumerate deterministically; do not follow symlinks into unrelated source trees."""

    def excluded(path):
        relative = path.relative_to(workspace).as_posix()
        return any(fnmatch.fnmatchcase(relative, pattern) for pattern in excludes)

    if target.is_file():
        return (
            []
            if excluded(target)
            or target.suffix.lower() not in {".v", ".vh", ".sv", ".svh"}
            else [target]
        )
    files = []

    def fail(error):
        raise error

    for root, dirs, names in os.walk(target, onerror=fail):
        directory = Path(root)
        dirs[:] = sorted(
            name
            for name in dirs
            if name != ".git"
            and not (directory / name).is_symlink()
            and not excluded(directory / name)
        )
        files.extend(
            directory / name
            for name in sorted(names)
            if Path(name).suffix.lower() in {".v", ".vh", ".sv", ".svh"}
            and not (directory / name).is_symlink()
            and not excluded(directory / name)
        )
    return sorted(files)


def group_issues(documents):
    """Group by feature and syntax, retaining every occurrence without copying source text."""
    groups = {}
    for document in documents:
        for index, diagnostic in enumerate(document["diagnostics"]):
            data = diagnostic["data"]
            key = (
                diagnostic["code"],
                data["syntaxKind"],
                data["completionKind"],
                data["trigger"],
                data["resolved"],
                data.get("role", ""),
            )
            group = groups.setdefault(
                key,
                {
                    "code": key[0],
                    "syntaxKind": key[1],
                    "completionKind": key[2],
                    "trigger": key[3],
                    "resolved": key[4],
                    "role": key[5],
                    "count": 0,
                    "occurrences": [],
                },
            )
            group["count"] += 1
            group["occurrences"].append(
                {
                    "path": document["path"],
                    "diagnostic": index,
                    "line": diagnostic["range"]["start"]["line"] + 1,
                    "byteColumn": diagnostic["range"]["start"]["character"] + 1,
                }
            )
    return sorted(
        groups.values(),
        key=lambda g: (
            -g["count"],
            g["code"],
            g["syntaxKind"],
            g["completionKind"],
            g["trigger"],
            g["resolved"],
        ),
    )


async def scan(args, files, report):
    with tempfile.TemporaryFile() as log:
        process = await asyncio.create_subprocess_exec(
            str(args.server),
            cwd=args.workspace,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=log,
        )
        client = ScanClient(process, args.timeout)
        current = None
        try:
            initialized = await client.request(
                "initialize",
                {
                    "processId": os.getpid(),
                    "rootUri": args.workspace.as_uri(),
                    "capabilities": {"general": {"positionEncodings": ["utf-8"]}},
                },
            )
            commands = (
                initialized.get("capabilities", {})
                .get("executeCommandProvider", {})
                .get("commands", [])
            )
            if "slang.debug.scanDocument" not in commands:
                raise RuntimeError(
                    "Server requires a build with -DSLANG_SERVER_DEBUG=ON"
                )
            report["serverInfo"] = initialized.get("serverInfo")
            await client.notify("initialized", {})
            for current in files:
                uri = current.as_uri()
                # read_bytes preserves CRLF so reported byte positions match the source on disk.
                source = current.read_bytes().decode("utf-8")
                await client.notify(
                    "textDocument/didOpen",
                    {
                        "textDocument": {
                            "uri": uri,
                            "languageId": "systemverilog",
                            "version": 1,
                            "text": source,
                        }
                    },
                )
                result = await client.request(
                    "workspace/executeCommand",
                    {
                        "command": "slang.debug.scanDocument",
                        "arguments": [{"uri": uri}],
                    },
                )
                if not isinstance(result, dict) or "diagnostics" not in result:
                    raise RuntimeError("Server returned no scan report")
                result["path"] = current.relative_to(args.workspace).as_posix()
                result["compilerDiagnostics"] = [
                    diagnostic
                    for diagnostic in client.diagnostics.get(uri, [])
                    if diagnostic.get("source") != "slang-server-debug"
                ]
                report["documents"].append(result)
                await client.notify(
                    "textDocument/didClose", {"textDocument": {"uri": uri}}
                )
                client.diagnostics.pop(uri, None)
            current = None
            await client.request("shutdown", None)
            await client.notify("exit", None)
            await asyncio.wait_for(process.wait(), 5)
            if process.returncode:
                raise RuntimeError(f"Server exited with status {process.returncode}")
        except (
            OSError,
            ValueError,
            RuntimeError,
            asyncio.TimeoutError,
            asyncio.IncompleteReadError,
        ) as error:
            report["errors"].append(
                {
                    "path": str(current) if current else None,
                    "message": str(error) or type(error).__name__,
                }
            )
        finally:
            if process.returncode is None:
                process.kill()
                await process.wait()
            report["messages"] = client.messages
            if report["errors"]:
                log.seek(0, os.SEEK_END)
                log.seek(max(0, log.tell() - 8000))
                report["serverLogTail"] = log.read().decode("utf-8", errors="replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "target", type=Path, help="HDL file or directory to scan recursively"
    )
    parser.add_argument(
        "--workspace", type=Path, help="Repo root whose .slang configuration to load"
    )
    parser.add_argument(
        "--server",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "build/bin/slang-server",
    )
    parser.add_argument("--output", type=Path, help="Write JSON here instead of stdout")
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        help="Exclude a workspace-relative glob (repeatable; quote shell wildcards)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=120,
        help="Timeout in seconds per request (default: 120)",
    )
    args = parser.parse_args()
    args.target = args.target.resolve()
    args.server = args.server.resolve()
    args.workspace = (
        args.workspace or (args.target if args.target.is_dir() else args.target.parent)
    ).resolve()
    if not args.target.exists() or not args.workspace.is_dir():
        parser.error("Target and workspace must exist")
    if not args.target.is_relative_to(args.workspace):
        parser.error("Target must be inside the workspace")
    if args.timeout <= 0:
        parser.error("Timeout must be positive")

    report = {
        "schemaVersion": 1,
        "workspace": str(args.workspace),
        "target": str(args.target),
        "positionEncoding": "utf-8",
        "documents": [],
        "errors": [],
    }
    files = []
    try:
        files = discover(args.target, args.workspace, args.exclude)
        if not files:
            raise RuntimeError("No HDL files selected")
        asyncio.run(scan(args, files, report))
    except (OSError, RuntimeError) as error:
        report["errors"].append({"path": None, "message": str(error)})
    report["groups"] = group_issues(report["documents"])
    report["summary"] = {
        "filesSelected": len(files),
        "filesScanned": len(report["documents"]),
        "issues": sum(group["count"] for group in report["groups"]),
        "groups": len(report["groups"]),
        "complete": not report["errors"],
    }
    payload = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")
    print(json.dumps(report["summary"]), file=sys.stderr)
    for error in report["errors"]:
        print(f"{error['path'] or 'scan'}: {error['message']}", file=sys.stderr)
    for group in report["groups"]:
        print(
            f"{group['count']:5} {group['code']} {group['syntaxKind']} "
            f"{group['completionKind']} resolved={group['resolved']}",
            file=sys.stderr,
        )
    return 2 if report["errors"] else int(report["summary"]["issues"] > 0)


if __name__ == "__main__":
    sys.exit(main())
