# Configuration Settings

- `slang.formatters`: array = []

  List of formatter configurations. Each entry specifies a command, directories to format, and language IDs. File input is sent to stdin, and formatted output is read from stdout.

- `slang.rewriterPath`: string = ""

  Rewriter command for macro expansion; e.g. `path/to/slang_rewriter --expand-macros`. This will shortly be part of the language server, and will not have to be set separately.

- `slang.path`: path

  Platform Defaults:

    linux:   `slang-server`

    mac:     `slang-server`

    windows: `slang-server.exe`

- `slang.args`: array = []

  Arguments to pass to the slang-server. These are different from slang flags; for those open `.slang/server.json`

- `slang.debugArgs`: array = []

  Arguments to pass to slang-server when debugging
