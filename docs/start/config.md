# Configuration

The server is configurable through server.json files

## Configuration Files

The server uses a hierarchical configuration system, layering options in this order:

- `~/.slang/server.json`
- `${workspaceFolder}/.slang/server.json` (should be on source control)
- `${workspaceFolder}/.slang/local/server.json` (`.slang/local` should be ignored by source control)

## Config options

All configuration options are optional and have sensible defaults. In Vscode, the configuration files are self-documenting, meaning that there are completions and hovers for the server.json files.


### `indexGlobs`
Configuring this and excludeDirs are **highly** recommended, as narrowing these can dramatically reduce the indexing time. Crawling a directory can actually take more time than actually indexing the files, so avoiding build dirs and software dirs to reduce that time. If there's rewritten or generated verilog in build dirs that get indexed, it can slow down the language server and make features like gotos and completions inaccurate.

**Type:** `array of strings`

**Default:** `["./.../*.sv*"]`

**Description:** SV Globs of what to index. Supports recursive patterns with `...` \

**Example:**

```json
{
  "indexGlobs": ["./src/.../*.sv", "./tb/.../*.sv", "./include/.../*.svh"]
}
```

### `excludeDirs`

**Type:** `array of strings`

**Description:** Directories to exclude from indexing

**Example:**

```json
{
  "excludeDirs": ["build", "temp", "old_code"]
}
```

### `flags`
It's recommended to keep your slang flags in a flag file, that way it can be shared by both CI and the language server. Another nice setup is having slang.f contain your CI flags, then have slang-server.f include that file (via `-f path/to/slang.f`), then add more warnings in `slang-server.f`. This way more pedantic checks will show as yellow underlines in your editor.

**Type:** `string`

**Description:** Flags to pass to slang

**Example:** `"-f path/to/slang_flags.f`


### `indexingThreads`

**Type:** `integer`

**Default:** `0` (auto-detect)

**Description:** Thread count to use for indexing. When set to 0, automatically detects the optimal number of threads based on system capabilities.

### `parsingThreads`

**Type:** `integer`

**Default:** `8`

**Description:** Thread count to use for parsing SystemVerilog files for compilations.


### `build`

**Type:** `string` (optional)

**Description:** Build file to automatically open on start

**Example:** `"./build/compile.f"`


### `buildPattern`

**Type:** `string` (optional)

**Description:** Build file pattern used to find the a .f file given a the name of a waveform file. (e.g. /tmp/{}.fst with builds/{}.f looks for build/foo.f to load the compilation). This is also used to look for .f files in the vscode client when selecting a .f file.

**Example:** `"builds/{}.f"`

### `wcpCommand`

**Type:** `string`

**Description:** Waveform viewer command where `{}` will be replaced with the WCP port

**Example:** `"surfer --wcp-initiate {}"`

## Example Configuration

```json
{
  "flags": "-f tools/slang/slang-server.f",
  "indexGlobs": ["./src/**/*.sv", "./tb/**/*.sv"],
  "excludeDirs": ["build", "obj_dir"],
  "indexingThreads": 4,
  "parsingThreads": 8,
  "build": "./scripts/compile.f",
  "wcpCommand": "surfer --wcp-initiate {}"
}
```
