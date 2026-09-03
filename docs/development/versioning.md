# Versioning and releases

The `slang-server` binary and editor extensions use independent semantic
versions. Patch versions can advance independently, but the major and minor
versions describe the shared feature set. Each side warns when the other has an
older major or minor version.

## Version sources

| Component | Version source | Release workflow |
| --- | --- | --- |
| Server | [`VERSION`](https://github.com/hudson-trading/slang-server/blob/main/VERSION) | `release.yml` |
| VS Code extension | [`clients/vscode/package.json`](https://github.com/hudson-trading/slang-server/blob/main/clients/vscode/package.json) | `vscode-publish.yml` |
| Neovim plugin | [`CLIENT_VERSION`](https://github.com/hudson-trading/slang-server/blob/main/clients/neovim/lua/slang-server/_lsp/capabilities.lua) in `_lsp/capabilities.lua` | `neovim-sync.yml` (mirror only) |

The Neovim plugin is mirrored to
[slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim) rather
than published to a registry, so its version is not derived from a package
manifest. The rockspec is used for CI only and stays at `scm-1`; bump
`CLIENT_VERSION` by hand when the plugin gains or requires new server features.

## Compatibility

Editor extensions require a server with at least their major and minor version.
The server also warns when an extension has an older major or minor version.
Patch differences do not generate compatibility warnings.

The extension advertises its name and version through the
`experimental.slangClient` initialization capability. The server uses this
information to detect an older extension during initialization.

## Server release assets

The server release workflow publishes one archive per supported platform. The
VS Code installer uses the canonical names, such as
`slang-server-linux-x64.tar.gz`, `slang-server-macos.tar.gz`, and
`slang-server-windows-x64.zip`.

Releases also include `slang-server-old-linux-x64-gcc.tar.gz` and
`slang-server-linux-arm64-clang.tar.gz` compatibility aliases for clients that
predate the canonical Linux names. Keep these aliases until those client
versions no longer need to install current server releases.
