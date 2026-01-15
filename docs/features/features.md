# Language Features

`slang-server` provides comprehensive Language Server Protocol (LSP) support for SystemVerilog, offering modern IDE features that significantly enhance development productivity.

### Diagnostics

Diagnostics are provided by slang using [Shallow Compilations](design/shallow.md) when a design is not set. Because of this some of the parameters may be invalid, and so some expressions may not be visited by slang and cause some false positives. We're trying to address these to get the best experience on the language server without needing to set a design.

If you think you found an issue with a diagnostics, please check whether it shows up via a normal slang command to determine which repo to raise the issue in.


### Hovers and Go-to Definition

Hovers are provided on each symbol with the following info:

- Symbol kind, or Syntax kind in the case of macros.
- Lexical Scope, or file in the case of macros.
- Resolved type info
- Bitwidth, if applicable
- Value, if a constant value
- The syntax that the symbol is derived from
- Macro usage, if defined from one.

Planned features:
- Multiple definitions, for example with modports.
- Macro expansion on hover, with expand quick-action
- Gotos for structural struct assignments
- Hover info for builtins

### Completions

Completions are currently provided for the following constructs:

#### Generic expression completions: parameters, variables, types, etc.

#### Modules and Interfaces

#### Functions

#### Hierarchical

#### Structs

Planned completions:

- Named assignments (structs, functions, ports, params)
- builtins (`$bits()`, etc.)

### Go-to References / Rename

Go-to references are provided for every nearly all symbols except for macros. Keep in mind this is an expensive operation, and may take some time for a large repo.

### Inlay Hints

Inlay hints are text that show up inline in the code to provide useful info. They can be hovered for more info, and some can be double clicked to insert some text.

**Ports** - Show the type of ports in instances (off by default)

**Params** - Show the resolved value of parameters

**Macros/Functions** - Show the positional argument names

**Wildcard Ports** - Show which signals are passed through

Planned Inlays:

- **Signals** - Show the resolved type of wires and registers, and show the value when a waveform is connected
- **Wildcard Imports** - Show which symbols are used from the import

### Planned features:
**Semantic Token Highlighting**
This will provide additional coloring for variable names, distinguishing between wires, registers, and parameters.
For classes/functions it will distinguish betweeen instance variables, function args and locals.


### Formatter

This one is pretty self explanatory, but it will live in the slang repo and also be shipped as a standalone binary. In order to make formatting nice for things like hovers and completions, basic formatting functions already exist in this repo, but not a full formatter. This includes things like squashing white spaces to condense hover and completion types, and left aligning blocks of text for hovers and completion docs.

### Limitations

- Features may be limited in the context of untaken ifdef branches. To minimize this, it's recommended to encode these ifdefs in package parameters, then use generate blocks in the hdl code.
- Shallow compilations only load the directly referenced syntax trees, and only load more transitive trees through packages. This may therefore cause issues with deep hierarchical references.
