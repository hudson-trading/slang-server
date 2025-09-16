---
hide:
  # - toc
  - navigation
  - feedback
---

# Hardware Language Features

When a compilation is set, a proper compilation will be made on top of the shallow compilations per file. The shallow compilations are still used to you can get quick language features on all tokens. The compilation is refreshed on save.

## Setting a Compilation


### Setting a build file
A build pattern can be set to glob for .f files, e.g. `**/*.f`. Then you can run the "Select Build File" Command from the hierarchy view or as a general command. The general LSP command is `setBuildFile(path: str)`.


### Setting a top level

<div class="grid cards" markdown>


-   The 'chip' icon at the top right of a file will scan the current file for valid top levels, and find all necessary files for a compilation.

-   ![SetTopLevel](SetTopLevel.png)
</div>

## Hierarchy View

<div class="grid cards" markdown>


-   The Hierarchy View shows the elaborated tree, with declared types and resolved values to the right of the identifier.

    ### Hierarchy Buttons (left to right)
    - Clear Top Level
    - Set Build File
    - Toggle data- wires, registers, etc.
    - Toggle objects defined behind macro usages.
    - Toggle paramters- params, localparams, etc.
    - Collapse all

    Buttons exist on each object to copy its hierarchical path.
    An extra button exists on modules to go to the module definition, rather than the instantiation of that module.

    Clicking a symbol in the hierarchy view or modules view will open the instance in the file, as well as in the other view.

-   ![HierarchyView](HierarchyView.png)
</div>

## Modules View

<div class="grid cards" markdown>


-   The modules view shows the instances indexed by module, sorted by the number of instances with higher level objects closer to the top.

-   ![ModulesView](ModulesView.png)
</div>

## Setting a scope

### `Slang: Select Scope` Command

This command pulls up a fuzzy finder where you can enter the hierarchical path.

### Terminal Links

Hierarchical paths are automatically recognized for vscode-integrated terminals, and will set the compilation and the instance. Even if the exact instance can't be found, it will eagerly open the instance until the names are invalid.


## Cone Tracing (experimental)
This allows for drivers/loads tracing over the call hierarchy lsp route, since it's an analogous concept.


## Waveform Integration (experimental)

<div class="grid cards" markdown>


-   This is currently only setup between Neovim and Surfer, through the WCP (waveform control protocol), which runs on JSON-RPC over TCP.

    ### Editor Features
    - open waveform file
    - open signal in wave viewer
    - open module in wave viewer

    ### Wave Viewer Features
    - open signal in hierarchy view / editor
    - open driver/loads for signal

-   ![WCP](wcp.gif)
</div>
