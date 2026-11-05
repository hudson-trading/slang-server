#!/usr/bin/env python3
# SPDX-FileCopyrightText: Hudson River Trading
# SPDX-License-Identifier: MIT
"""End-to-end scan runner tests; requires build/bin/slang-server with developer features."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class DebugScanTests(unittest.TestCase):
    def run_scan(self, sources, *options, expected_status=0, native=False):
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory)
            for name, text in sources.items():
                path = workspace / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(text.encode())
            output = workspace / "report.json"
            command = (
                [str(ROOT / "build/bin/slang-server"), "--debug-scan"]
                if native
                else [sys.executable, str(ROOT / "scripts/debug-scan.py")]
            )
            process = subprocess.run(
                [
                    *command,
                    str(workspace),
                    "--output",
                    str(output),
                    *options,
                ],
                env={**os.environ, "SLANG_SERVER_TESTS": "YES"},
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(process.returncode, expected_status, process.stderr)
            self.assertEqual(process.stdout, "")
            return json.loads(output.read_text())

    def test_clean_scan_and_selection(self):
        report = self.run_scan(
            {
                "top.sv": "module top; int value; initial value = 1; endmodule\r\n",
                "inc/types.svh": "typedef int value_t;\n",
                "ignored/broken.sv": "module bad; initial missing(); endmodule\n",
                "README.md": "not HDL\n",
            },
            "--exclude",
            "ignored",
        )
        self.assertEqual(
            report["summary"],
            {
                "filesSelected": 2,
                "filesScanned": 2,
                "issues": 0,
                "groups": 0,
                "complete": True,
            },
        )
        self.assertEqual(
            [doc["path"] for doc in report["documents"]], ["inc/types.svh", "top.sv"]
        )
        self.assertGreater(report["documents"][1]["completionChecks"], 0)

    def test_grouping_and_utf8_positions(self):
        source = "module top; initial begin /* é */ missing(); end endmodule\r\n"
        report = self.run_scan({"one.sv": source, "two.sv": source}, expected_status=1)
        self.assertEqual(report["summary"]["issues"], 4)
        self.assertEqual(report["summary"]["groups"], 2)
        self.assertEqual(
            {group["code"] for group in report["groups"]},
            {"missing-hover-goto", "missing-completion"},
        )
        self.assertTrue(report["summary"]["complete"])
        column = source.encode().index(b"missing")
        for group in report["groups"]:
            self.assertEqual(group["count"], 2)
            self.assertFalse(group["resolved"])
            self.assertEqual(group["role"], "reference")
            self.assertEqual(group["occurrences"][0]["byteColumn"], column + 1)
        for document in report["documents"]:
            self.assertTrue(document["compilerDiagnostics"])
            for diagnostic in document["diagnostics"]:
                self.assertEqual(
                    diagnostic["range"]["start"], {"line": 0, "character": column}
                )

    def test_native_and_lsp_scans_agree(self):
        sources = {
            "top.sv": "module top; int value; initial value = missing; endmodule\r\n",
            "nested/types.svh": "typedef int value_t;\n",
        }
        lsp = self.run_scan(sources, expected_status=1)
        native = self.run_scan(sources, expected_status=1, native=True)
        self.assertEqual(native["summary"], lsp["summary"])
        self.assertEqual(native["groups"], lsp["groups"])
        for native_doc, lsp_doc in zip(native["documents"], lsp["documents"]):
            self.assertEqual(
                native_doc["diagnostics"],
                [
                    {**diagnostic, "severity": {"Warning": 2}[diagnostic["severity"]]}
                    for diagnostic in lsp_doc["diagnostics"]
                ],
            )
            for field in (
                "path",
                "identifiers",
                "declarations",
                "references",
                "hoverChecks",
                "definitionChecks",
                "completionChecks",
            ):
                self.assertEqual(native_doc[field], lsp_doc[field], field)

    def test_timeout_reports_incomplete_scan(self):
        report = self.run_scan(
            {"top.sv": "module top; endmodule"},
            "--timeout",
            "0.000001",
            expected_status=2,
        )
        self.assertFalse(report["summary"]["complete"])
        self.assertTrue(report["errors"])

    def test_server_start_failure_is_machine_readable(self):
        report = self.run_scan(
            {"top.sv": "module top; endmodule"},
            "--server",
            "/nonexistent/slang-server",
            expected_status=2,
        )
        self.assertFalse(report["summary"]["complete"])
        self.assertEqual(report["summary"]["filesScanned"], 0)
        self.assertTrue(report["errors"])

    def test_no_selected_files_is_not_a_clean_scan(self):
        report = self.run_scan({"README.md": "text"}, expected_status=2)
        self.assertFalse(report["summary"]["complete"])


if __name__ == "__main__":
    unittest.main()
