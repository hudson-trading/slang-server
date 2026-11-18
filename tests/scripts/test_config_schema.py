#!/usr/bin/env python3
# SPDX-FileCopyrightText: Hudson River Trading
# SPDX-License-Identifier: MIT
"""Check the standalone schema generator and the schema shipped with the editor client."""

import json
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ConfigSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema = json.loads(
            subprocess.check_output(
                [str(ROOT / "build/bin/gen_config_schema")],
                cwd=ROOT,
                text=True,
                timeout=30,
            )
        )

    def test_generator_and_editor_schema_agree(self):
        editor_schema = json.loads(
            (ROOT / "clients/vscode/resources/config.schema.json").read_text()
        )
        self.assertEqual(self.schema, editor_schema)

    def test_scalar_and_empty_container_defaults(self):
        properties = self.schema["$defs"]["Config"]["properties"]
        self.assertEqual(properties["maxFileSize"]["default"], 1048576)
        self.assertEqual(properties["indexingThreads"]["default"], 0)
        self.assertIs(properties["buildRelativePaths"]["default"], False)
        self.assertEqual(properties["incdirs"]["default"], [])
        self.assertEqual(properties["env"]["default"], {})
        self.assertNotIn("default", properties["build"])

    def test_nested_defaults(self):
        definitions = self.schema["$defs"]
        properties = definitions["Config"]["properties"]
        hovers = definitions["Config__HoverConfig"]["properties"]
        self.assertEqual(hovers["docCommentFormat"]["default"], "markdown")
        self.assertEqual(
            properties["hovers"]["default"], {"docCommentFormat": "markdown"}
        )
        hints = definitions["Config__InlayHints"]["properties"]
        self.assertIs(hints["portTypes"]["default"], False)
        self.assertIs(hints["assignmentPatternTypes"]["default"], True)
        self.assertEqual(hints["funcArgNames"]["default"], 2)


if __name__ == "__main__":
    unittest.main()
