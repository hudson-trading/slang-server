# Configuration

The server uses a hierarchical configuration system, layering configs in this order:

1. `~/.slang/server.json`
2. `${workspaceFolder}/.slang/server.json` (should be in source control)
3. `${workspaceFolder}/.slang/local/server.json` (`.slang/local` should be ignored by source control)

Each option value overrides the value set in previous configs. The exception for this is lists, which are appended.

## Config Options

All configuration options are optional and have sensible defaults. In VSCode, there are completions and hovers for `server.json` files. For other editors, you may be able to associate the [config schema](https://github.com/hudson-trading/slang-server/blob/main/clients/vscode/resources/config.schema.json) with these config files to get these features.

---

### `index`

:   **Type:** `list[IndexConfig]`

    ```typescript
    interface IndexConfig {
      /** Directories to index */
      dirs?: string[]
      /** Directories to exclude; only supports single directory names and applies to all path levels */
      excludeDirs?: string[] | null
    }
    ```

    Which directories to index; By default it indexes the entire workspace. It's **highly** recommended to configure this for your repo, especially if there are generated build directories and non-hardware directories that can be skipped.

---

### `flags`

:   **Type:** `string`

    Flags to pass to slang.

    It's recommended to keep your slang flags in a flag file, that way it can be shared by both CI and the language server. Another nice setup is having `slang.f` contain your CI flags, then have `slang-server.f` include that file (via `-f path/to/slang.f`), along with more warnings in `slang-server.f`. That way more pedantic checks will show as yellow underlines in your editor. See the [Slang docs](https://sv-lang.com/user-manual.html#command-files) for information on the command files.

    **Example:** `"-f path/to/slang_flags.f"`

---

### `indexingThreads`

:   **Type:** `integer`

    **Default:** `0` (auto-detect)

    Thread count to use for indexing. When set to 0, automatically detects the optimal number of threads based on system capabilities.

---

### `parsingThreads`

:   **Type:** `integer`

    **Default:** `8`

    Thread count to use for parsing SystemVerilog files for compilations.

---

### `build`

:   **Type:** `string`

    Build file to automatically open on start.

    **Example:** `"./build/compile.f"`

---

### `buildPattern`

:   **Type:** `string` (glob pattern)

    Build file pattern used to find a `.f` file given the name of a waveform file. For example, `/tmp/{}.fst` with `builds/{}.f` looks for `build/foo.f` to load the compilation. This is also used to look for `.f` files in the VSCode client when selecting a `.f` file.

    **Example:** `"builds/{}.f"`

---

### `wcpCommand`

:   **Type:** `string`

    Waveform viewer command where `{}` will be replaced with the WCP port.

    **Example:** `"surfer --wcp-initiate {}"`

---

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
