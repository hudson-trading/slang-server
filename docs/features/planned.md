# Planned Features

### Find references

This feature will likely be implemented in this order:

 - top level symbols like macros, modules, packages, and interfaces
 - scope members
 - struct members

### Semantic Token Highlighting

This will provide additional coloring for variable names, distinguisihing between wires, registers, and parameters.

For classes/functions it will distinguish betweeen instance variables, function args and locals.

### Inlay Hints

**Ports** - Show the type of ports in instances

**Params** - Show the resolved value of parameters

**Signals** - Show the resolved type of wires and registers, and show the value when a waveform is connected

**Macros/Functions** - Show the positional argument names

**Wildcard Ports** - Show which signals are passed through

**Wildcard Imports** - Show which symbols are used from the import

### Formatter

This one is pretty self explanatory, but it will live in the slang repo and also be shipped as a standalone binary. In order to make formatting nice for the language server, basic formatting functions already exist in this repo. This includes things like squashing white spaces to condense hovers/completion types, and left aligning blocks of text for hovers and completion docs.
