# Installing

### Build from Source

```bash
# Clone the repository
git clone https://github.com/hudson-trading/slang-server.git
cd slang-server

# Pull dependencies
git submodule update --init --recursive

# Build with cmake using a C++20 compliant compiler
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Pre-built binaries

In the future we plan to have pre-built binaries released for common platforms. We also plan to have the editor clients auto-install these, similar to what clangd and others do.

### Neovim

Install the neovim client at [https://github.com/hudson-trading/slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim)

### Vscode

Extension will be released soon

<!-- Install the extension [here](https://marketplace.visualstudio.com/items?itemName=HudsonRiverTrading.slang-vscode), then set `slang.path` to your server binary -->


<!-- See [Vscode Options](../clients/vscode/CONFIG.md) -->
