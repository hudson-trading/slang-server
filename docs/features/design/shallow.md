# Shallow Compilation

In order to provide real time diagnostics and symbols, the server makes use of the concept of a shallow compilation, where directly referenced top level symbols in the current document are loaded for use in a slang compilation.

In software languages this is typically standard- you can compile just one cpp file for example, and forward declare other symbols that you use.
This isn't really a thing for HDLs, where a full design is almost always assumed. Parts of Slang can be tweaked to essentially get this functionality.

### Limitations

Hierarchical references can go down or up more than one layer, in which case some symbols may not load. It would be nice to continue adding the relevant syntax trees to get all symbols in the current document, rather than just loading directly referenced symbols. Upward references will always be a blind spot for the language server when a design isn't set, and are generally not considered a good practice.

## UntakenGenerateChecks

This compilation flag is used in shallow compilations to get symbols and diagnostics untaken generate branches. Many diags are still valid in untaken generate branches, and are useful for applications like a language server.

A related issue is the case of ifdef branches. In the future the server can attempt to parse out most untaken ifdef branches to at least provide goto/hover support, but likely never diagnostics.

## Interface Port Parameter Asserts

It's a common pattern to declare an input/output interface with a datatype, however SystemVerilog doesn't let you restrict the params of these interface ports, and people instead rely on asserts. Ideally slang could pick up on these asserts and fill in the correct dtype for these. Even more ideally, these constraints would be in the language itself. Either implementation would provide the user with more accurate info when doing hierarhical completions.

## Single Unit

The slang `--single-unit` causes all parsed files to essentially be squashed together, which has the effect of syntax trees inheriting preprocessor macros and defines.

Actually parsing the files this way in the language server would slow down the server considerably. So repos that use this will see errors for symbols - typically macros - that are not explicitly included.

Support for this could be added in the future by feeding the indexed macros to the preprocessor if it can't find the macro in its current working set.
