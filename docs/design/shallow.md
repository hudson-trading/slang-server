
# Shallow Compilation

In order to provide real time diagnostics and symbols, the server makes use of the concept of a shallow compilation, where directly referenced top level symbols in the current document are loaded for use in a slang compilation.

In software languages this is typically standard- you can compile just one cpp file for example, and forward declare other symbols that you use.
This isn't really a thing for HDLs, where a full design is almost always assumed. Parts of Slang can be tweaked to essentially get this functionality.

### Limitations

Hierarchicaly references can go down or up more than one layer, in which case some symbols may not load. It would be nice to continue adding the relevant syntax trees to get all symbols in the current document, rather than just loading directly referenced symbols. Upward references will always be a blind spot for the language server, and are generall not considered a good practice.

## AllGenerateBranches

This compilation flag is used in shallow compilations to get symbols and diagnostics on all generate branches. This process can definitely be improved; for example certain diags are marked as invalid for shallow compilation, and that's tricky since some may have been missed, and ideally the system would never go down the path where it's publishing those diags.

A related issue is the case of ifdef branches. Making an AllIfdefBranches flag would certainly cause chaos, and perhaps the best way of dealing with this is to just do syntax lookups in the untaken branches to provide symbols.

## Interface Port Parameter Asserts

It's a common pattern to declare an input/output interface with a datatype, however SystemVerilog doesn't let you restrict the params of these interface ports, and people instead rely on asserts. Ideally slang could pick up on these asserts and fill in the correct dtype for these. Even more ideally, these constraints would be in the language itself. Either implementation would provide the user with more accurate info when doing hierarhical completions.

## Single Unit

The slang `--single-unit` causes all parsed files to essentially be squashed together, which has the effect of syntax trees inheriting preprocessor macros and defines.

This isn't great for a language server, and isn't supported at the moment, meaning repos that use this will see errors for undefined macros. 

Support for this could be added by feeding the indexed macros to the preprocessor if it can't find the macro in its current working set.