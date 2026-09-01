---
hide:
  # - toc
  # - navigation
  - feedback
---

# Hardware Language Features



<div class="grid" markdown>
<div class="grid-item" markdown>

When a design is set, a full hierarchy will be elaborated in conjunction with the shallow compilations per file, which will still be used to get quick language features on all tokens.

The compilation is refreshed on save, updating the diagnostics and related info.

## Active Instances

A module or interface definition can be elaborated many times with different parameters, types, generate branches, and interface connections. Slang Server keeps one **active instance** for each definition so source-level features have a concrete elaborated context.

The active instance controls:

- Resolved parameter and localparam values in hovers and inlay hints.
- Dependent signal types and widths.
- Interface parameter values, connections, and forwarded interface paths.
- The selected iteration of generate loops.

Selecting an instance also updates the active instances along its enclosing hierarchy. Go to Definition from an instantiation, port, or parameter reference selects the referenced instance before opening its declaration. Selections that still exist are preserved when the design is recompiled; if one disappears, the server chooses a valid default.

The editor integrations provide controls for inspecting and changing these selections. See the editor-specific pages below.

See the [Vscode Docs](./vscode.md)

See the [Neovim Docs](./neovim.md)

</div>
<div class="grid-item" markdown>

![HierarchyView](vscode/HierarchyView.png)

</div>
</div>
