# Configuration

The server uses a hierarchical configuration system with three config files:

1. `${workspaceFolder}/.slang/server.json` — workspace config (should be in source control)
2. `~/.slang/server.json` — user config (personal defaults across all projects)
3. `${workspaceFolder}/.slang/local/server.json` — local config (`.slang/local` should be ignored by source control)

Later files override earlier ones for scalar values. Lists (like `index`) are appended across all files.

### Flags precedence

The `flags` field has special merging behavior. Workspace flags override user flags (only one is used as the base), and local flags are always appended on top. This means you can set shared flags in `.slang/server.json`, and add personal flags (like extra `-D` defines) in `.slang/local/server.json` without overriding the shared ones.

The server watches these config files, .f files that are passed in via flags and `.f` build files for changes, automatically reloading when they are saved.

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

    Flags to pass to slang. It uses the underlying driver to parse the flags, however some flags may not be used by the server.

    Use this to configure things like [include paths](https://sv-lang.com/command-line-ref.html#include-paths), [LRM relaxations](https://sv-lang.com/command-line-ref.html#compat-option), configure [warning severity](https://sv-lang.com/command-line-ref.html#clr-warnings) and [specific warnings](https://sv-lang.com/warning-ref.html).

    It's recommended to keep your slang flags in a [flag file](https://sv-lang.com/user-manual.html#command-files), that way it can be shared by both CI and the language server. Another nice setup is having `slang.f` contain your CI flags, then have `slang-server.f` include that file (via `-f path/to/slang.f`), along with more warnings so that more pedantic checks will show as yellow underlines in your editor.

    For preprocessor defines (`-D`), you can also use the **"Add define"** code action: place your cursor on an undefined macro name in an `` `ifdef `` and use the quick fix to automatically add `-D<name>` to `.slang/local/server.json`.

    **Example:** `"-f path/to/slang_flags.f"`

---

### `indexingThreads`

:   **Type:** `integer`

    **Default:** `0` (auto-detect)

    Thread count to use for indexing. When set to 0, automatically detects the optimal number of threads based on system capabilities.

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

### `inlayHints`

:   **Type:** `InlayHints`

    ```typescript
    interface InlayHints {
      /** Hints for port types */
      portTypes?: boolean           // default: false
      /** Hints for names of ordered ports and params */
      orderedInstanceNames?: boolean // default: true
      /** Hints for port names in wildcard (.*) ports */
      wildcardNames?: boolean       // default: true
      /** Function argument hints: 0=off, N=only calls with >=N args */
      funcArgNames?: integer        // default: 2
      /** Macro argument hints: 0=off, N=only calls with >=N args */
      macroArgNames?: integer       // default: 2
    }
    ```

    Controls inline hints displayed in the editor for things like ordered arguments, wildcard ports, and others.

    - **`portTypes`**: Show type hints on ports. Off by default.
    - **`orderedInstanceNames`**: Show parameter/port name hints on ordered (positional) instance connections.
    - **`wildcardNames`**: Show port name hints on wildcard (`.*`) connections.
    - **`funcArgNames`**: Show argument name hints on function calls. Set to `0` to disable, or `N` to only show hints for calls with N or more arguments.
    - **`macroArgNames`**: Show argument name hints on macro invocations. Set to `0` to disable, or `N` to only show hints for calls with N or more arguments.

---

## Example Configuration

### Workspace config (`.slang/server.json`)

Shared across the team, checked into source control:

```json
{
  "flags": "-f tools/slang/slang-server.f",
  "index": [
    {
      "dirs": ["fpga/src", "fpga/tb"],
      "excludeDirs": ["build", "synth"]
    }
  ],
  "buildPattern": "builds/**/*.f",
  "indexingThreads": 4
}
```

### User config (`~/.slang/server.json`)

Personal defaults that apply to all projects without a workspace config:

```json
{
  "flags": "-Wextra",
  "inlayHints": {
    "orderedInstanceNames": true,
    "funcArgNames": 3
  }
}
```

If the workspace config above has `flags`, it takes precedence over this one (they are not combined). If the workspace config has no `flags`, these user flags are used as the base.

### Local config (`.slang/local/server.json`)

Personal overrides for this workspace, not checked in (add `.slang/local` to `.gitignore`):

```json
{
  "flags": "-DSIM_MODE -DDEBUG_LEVEL=2",
  "build": "./builds/my_top.f"
}
```

These flags are **appended** to whichever base flags won (workspace or user), so the final flags in this example would be `-f tools/slang/slang-server.f -DSIM_MODE -DDEBUG_LEVEL=2`. The `build` field overrides any previous value since it's a scalar.
