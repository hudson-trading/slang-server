# Shallow Compilation

In order to provide real time diagnostics and symbols, the server makes use of the concept of a shallow compilation, where directly referenced top level symbols in the current document are loaded for use in a slang compilation.

In software languages this is typically standard- you can compile just one cpp file for example, and forward declare other symbols that you use.
This isn't really a thing for HDLs, where a full design is almost always assumed. Parts of Slang can be tweaked to essentially get this functionality.

### Limitations

Hierarchical references can go down or up more than one layer, in which case some symbols may not load. It would be nice to continue adding the relevant syntax trees to get all symbols in the current document, rather than just loading directly referenced symbols. Upward references will always be a blind spot for the language server when a design isn't set, and are generally not considered a good practice.

## Untaken Generate Branches

Shallow compilations use slang's `CheckUninstantiated` flag to provide symbols and diagnostics in untaken generate branches. Many diagnostics are still useful in branches that are not elaborated for the current parameter values.

A related issue is the case of ifdef branches. In the future the server can attempt to parse out most untaken ifdef branches to at least provide goto/hover support, but likely never diagnostics.

## Interface Port Parameter Asserts

Without a full design, an interface port may have no connected instance to supply its parameters. Slang can infer value and type parameters for these ports from equality checks in `$static_assert` calls in the module body.

```systemverilog
package types_pkg;
    typedef logic [7:0] word_t;
endpackage

interface channel_if #(
    parameter int WIDTH = 1,
    parameter type data_t = logic
);
    logic [WIDTH-1:0] mask;
    data_t data;
endinterface

module consumer(channel_if channel, output types_pkg::word_t data);
    $static_assert(channel.WIDTH == 8);
    $static_assert(type(channel.data_t) == type(types_pkg::word_t));
    assign data = channel.data;
endmodule
```

During shallow analysis of `consumer`, these assertions set `channel.WIDTH` to `8` and `channel.data_t` to `types_pkg::word_t`, overriding the interface defaults. Hovers, completions, and diagnostics can then use the intended widths and member types. Without a matching assertion, the interface defaults apply.

This inference applies to interface ports without a connected instance. In a full design, the connected interface supplies the parameters and the assertions check them. `$static_assert` is a slang extension; ordinary runtime assertions do not provide these constraints.

## Single Unit

Slang's `--single-unit` parses the files in a build as one compilation unit, allowing later files to inherit preprocessor macros from earlier files.

slang-server warns and ignores this flag in both server configuration and build filelists. It parses each source file separately so that an edit can reparse just that file. Projects that share macros across source files can supply them through explicit includes or configured defines.
