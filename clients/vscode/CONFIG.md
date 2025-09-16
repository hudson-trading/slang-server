# Configuration Settings

- `slang.formatDirs`: array = []

  Directories to format

- `slang.svFormat.command`: string = ""

  Formatter Command. The file contents will be sent to stdin, and formatted code should be sent back on stdout

- `slang.verilogFormat.command`: string = ""

  Formatter Command. The file contents will be sent to stdin, and formatted code should be sent back on stdout

- `slang.rewriterPath`: string = ""

  Path to the rewriter script (typically slang rewriter binary with --expand-macros flag)

- `slang.path`: path

  Platform Defaults:

  linux: `slang-server`

  mac: `slang-server`

  windows: `slang-server.exe`

- `slang.args`: array = []

  Arguments to pass to the slang-server

- `slang.debugArgs`: array = []

  Arguments to pass to slang-server when debugging
