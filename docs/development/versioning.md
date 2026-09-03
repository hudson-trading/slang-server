# Versioning and releases

The `slang-server` binary and editor clients share compatible major and minor
versions. VS Code has an independent patch version; Neovim revisions are the
commits in its mirror repository.

## Version sources

| Component | Version source | Release workflow |
| --- | --- | --- |
| Server | [`VERSION`](https://github.com/hudson-trading/slang-server/blob/main/VERSION) | `release.yml` |
| VS Code extension | [`clients/vscode/package.json`](https://github.com/hudson-trading/slang-server/blob/main/clients/vscode/package.json) | `vscode-publish.yml`, directly or from `release.yml` |
| Neovim plugin | [`CLIENT_VERSION`](https://github.com/hudson-trading/slang-server/blob/main/clients/neovim/lua/slang-server/_lsp/capabilities.lua) | `neovim-sync.yml`, directly or from `release.yml` |

The Neovim plugin is mirrored to
[slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim) rather
than published to a registry. Its rockspec is used for CI only and remains at
`scm-1`.

## Coordinated releases

A major or minor release updates `CLIENT_VERSION` from `VERSION`. After
uploading the server assets, it applies the same bump to VS Code and syncs
Neovim. Server patch releases do not publish either client. Re-running an
existing tag only rebuilds and replaces its server assets.

Dry-run mode calculates the next version, builds the branch selected in the
**Run workflow** dialog, and prepares archives without pushing or publishing
anything. Select a pull request branch to exercise its release build.

## Compatibility

Editor clients require a server with at least their major and minor version.
The server also warns when a client has an older major or minor version. Patch
differences do not generate compatibility warnings.

Each client advertises its name and version through the
`experimental.slangClient` initialization capability. The server uses this
information to detect an older client during initialization. Neovim 0.11 and
newer receive the capability through `vim.lsp.config`; configurations for older
Neovim versions can use `make_client_capabilities()` from
`slang-server._lsp.capabilities`.

## Server release assets

The server release workflow publishes one archive per supported platform. The
VS Code installer uses the canonical names, such as
`slang-server-linux-x64.tar.gz`, `slang-server-macos.tar.gz`, and
`slang-server-windows-x64.zip`.

Releases also include `slang-server-old-linux-x64-gcc.tar.gz` and
`slang-server-linux-arm64-clang.tar.gz` compatibility aliases for clients that
predate the canonical Linux names. Keep these aliases until those client
versions no longer need to install current server releases.
