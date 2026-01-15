# Language Features

### Diagnostics

Diagnostics are provided by slang using [Shallow Compilations](design/shallow.md) when a design is not set.
When a [design is set](hdl/hdl.md), the shallow compilation provides diagnostics on each keystroke, and the design's compilation provides diagnostics on each save.

![goto-refs](/assets/images/lints.gif)


### Hovers and Go-to Definition

Hovers are provided on each symbol with the following info if applicable:

- Symbol kind, or Syntax kind in the case of macros.
- Lexical Scope, or file in the case of macros.
- Resolved type info
- Bitwidth, if a value or type
- Value, if a constant value
- The syntax that the symbol is derived from
- Macro usage, if symbol was defined from one.

![goto-refs](/assets/images/hovers.gif)

Planned features:

- Multiple definitions, for example with modports.
- Macro expansion on hover, with expand quick-action
- Hovers/Gotos for struct assignments
- Hovers for builtins


### Completions

Completions are currently provided for the following constructs:

- Generic expression completions: parameters, variables, types etc.
- Modules and interfaces
- Functions and macros
- Hierarchical references and struct members

![goto-refs](/assets/images/completions.gif)

Planned completions:

- Named assignments (structs, functions, ports, params)
- builtins (`$bits()`, etc.)

### Go-to References / Rename

Go-to references are provided for every nearly all symbols except for macros. Keep in mind this is an expensive operation, and may take some time for a large repo.

![goto-refs](/assets/images/gotorefs.gif)

### Inlay Hints

Inlay hints are text that show up inline in the code to provide useful info. They can be hovered for more info, and some can be double clicked to insert some text.

**Ports Types** - Show the type of ports in instances (off by default)

![goto-refs](/assets/images/port_inlays.png)

**Wildcard Ports** - Show which signals are passed through
![goto-refs](/assets/images/port_wildcard_inlays.png)

**Positional args in Macros, Functions, Paramter/Port lists** - Show the argument names
![goto-refs](/assets/images/macro_inlays.png)


Planned Inlays:

- **Constant Values** - Show parameter values and resolved types
- **Signals** - Show the value when a waveform is connected and an instance is selected.
- **Wildcard Imports** - Show which symbols are used from the import

### Planned LSP Methods:

**Semantic Token Highlighting**
This will provide additional coloring for variable names, distinguishing between wires, registers, and parameters.
For classes/functions it will distinguish betweeen instance variables, function args and locals.

**Formatting**
This will likely live in the slang repo and also be shipped as a standalone binary. In order to have fairly nice formatting in hovers and completions, basic formatting functions already exist in this repo. This includes things like squashing white spaces to condense hover and completion types, and left aligning blocks of text for hovers and completion docs.
