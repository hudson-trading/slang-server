# Configuration Settings

- `slang.formatDirs`: array = []

  Directories to format

- `slang.svFormat.command`: string = ""

  Formatter Command. The file contents will be sent to stdin, and formatted code should be sent back on stdout. E.g. `path/to/verible-format --indentation_spaces=4 -

- `slang.verilogFormat.command`: string = ""

  Formatter Command. The file contents will be sent to stdin, and formatted code should be sent back on stdout. E.g. `path/to/verible-format --indentation_spaces=4 -

- `slang.path`: path

  Platform Defaults:

    linux:   `slang-server`

    mac:     `slang-server`

    windows: `slang-server.exe`

- `slang.args`: array = []

  Arguments to pass to the slang-server. These are different from slang flags; for those open `.slang/server.json`

- `slang.debugArgs`: array = []

  Arguments to pass to slang-server when debugging

