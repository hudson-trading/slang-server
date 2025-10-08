---
hide:
  # - toc
  # - navigation
  - feedback
---

# Hardware Language Features - Neovim

## Cone Tracing (experimental)

This allows for drivers/loads tracing over the call hierarchy lsp route, since it's an analogous concept.

## Waveform Integration (experimental)

<div class="grid" markdown>
<div class="grid-item" markdown>

This is currently only setup between Neovim and Surfer, through the WCP (waveform control protocol), which runs on JSON-RPC over TCP.

### Editor Features
- open waveform file
- open signal in wave viewer
- open module in wave viewer

### Wave Viewer Features
- open signal in hierarchy view / editor
- open driver/loads for signal

</div>
<div class="grid-item" markdown>

![WCP](neovim/wcp.gif)

</div>
</div>
