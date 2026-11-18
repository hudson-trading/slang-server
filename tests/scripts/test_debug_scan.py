#!/usr/bin/env python3
# SPDX-FileCopyrightText: Hudson River Trading
# SPDX-License-Identifier: MIT
"""End-to-end scan runner tests; requires build/bin/slang-server with developer features."""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class DebugScanTests(unittest.TestCase):
    def run_scan(self, sources, *options, expected_status=0, native=False, target=""):
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
                    str(workspace / target),
                    "--workspace",
                    str(workspace),
                    "--output",
                    str(output),
                    *options,
                ],
                env={
                    **os.environ,
                    "SLANG_SERVER_TESTS": "YES",
                    "DEBUG_SCAN_TEST_WORKSPACE": str(workspace),
                },
                text=True,
                capture_output=True,
                timeout=30,
                check=False,
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

    def test_configured_directory_exclusions(self):
        sources = {
            ".slang/server.json": json.dumps(
                {
                    "env": {"SCAN_SKIP": "cache"},
                    "index": [
                        {
                            "dirs": ["."],
                            "excludeDirs": [
                                "generated",
                                "./rtl/private",
                                "$(SCAN_SKIP)",
                                "$DEBUG_SCAN_TEST_WORKSPACE/absolute-only",
                            ],
                        }
                    ],
                }
            ),
            ".slang/local/server.json": json.dumps(
                {
                    "env": {"LOCAL_SKIP": "local-only"},
                    "index": [{"dirs": [], "excludeDirs": ["${LOCAL_SKIP}"]}],
                }
            ),
            "top.sv": "module top; endmodule\n",
            "generated_extra/kept.SV": "module kept; endmodule\n",
            "other/private/valid.sv": "module valid; endmodule\n",
        }
        for directory in (
            "generated",
            "nested/generated",
            "rtl/private",
            "cache",
            "local-only",
            "absolute-only",
        ):
            sources[f"{directory}/broken.sv"] = (
                "module bad; initial missing(); endmodule\n"
            )
        for native in (False, True):
            with self.subTest(native=native):
                report = self.run_scan(sources, native=native)
                self.assertTrue(report["summary"]["complete"])
                self.assertEqual(report["summary"]["filesSelected"], 3)
                self.assertEqual(
                    [doc["path"] for doc in report["documents"]],
                    ["generated_extra/kept.SV", "other/private/valid.sv", "top.sv"],
                )
        for native in (False, True):
            for target in (
                "nested/generated",
                "rtl/private/broken.sv",
                "absolute-only/broken.sv",
            ):
                with self.subTest(native=native, target=target):
                    report = self.run_scan(
                        sources, native=native, target=target, expected_status=2
                    )
                    self.assertFalse(report["summary"]["complete"])
                    self.assertEqual(report["summary"]["filesSelected"], 0)

    def test_legacy_directory_exclusions(self):
        sources = {
            ".slang/server.json": '{"excludeDirs":["generated"]}',
            "top.sv": "module top; endmodule\n",
            "generated/broken.sv": "module bad; initial missing(); endmodule\n",
        }
        for native in (False, True):
            with self.subTest(native=native):
                report = self.run_scan(sources, native=native)
                self.assertEqual(
                    [doc["path"] for doc in report["documents"]], ["top.sv"]
                )

    def test_configured_file_size_limit(self):
        large = "// " + "é" * 64 + "\nmodule large; endmodule\n"
        size = len(large.encode())
        for native in (False, True):
            for limit in (size - 1, size, 0):
                with self.subTest(native=native, limit=limit):
                    report = self.run_scan(
                        {
                            ".slang/server.json": json.dumps({"maxFileSize": limit}),
                            "a.sv": "module a; endmodule\n",
                            "large.sv": large,
                            "z.sv": "module z; endmodule\n",
                        },
                        native=native,
                        expected_status=2 if limit == size - 1 else 0,
                    )
                    self.assertEqual(report["summary"]["filesSelected"], 3)
                    if limit == size - 1:
                        self.assertFalse(report["summary"]["complete"])
                        self.assertEqual(report["summary"]["filesScanned"], 2)
                        self.assertEqual(len(report["errors"]), 1)
                        self.assertIn("maxFileSize", report["errors"][0]["message"])
                        self.assertIn(
                            ".slang/server.json", report["errors"][0]["message"]
                        )
                        self.assertEqual(
                            [d["path"] for d in report["documents"]], ["a.sv", "z.sv"]
                        )
                    else:
                        self.assertTrue(report["summary"]["complete"])
                        self.assertEqual(report["summary"]["filesScanned"], 3)

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
