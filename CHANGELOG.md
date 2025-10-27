# Change Log

All major and minor changes to this project will be documented in this file.

Clients require a server with the same or greater major and minor version in order to get the full feature set.
Patches published for either are independent.
For patch releases applied to the server or one of the various clients, please look at the git log, as they are only documented here as part of following minor release.

Vscode versions are specified in `clients/vscode/package.json`.

The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

<!-- new-release-here -->

## [0.1.0]

- Added Vaporview integration with vscode client
- Vscode Terminal Links
- Compleitons for imported symbols via package wildcards
- Add Lexical path info for completions and hovers outside of the current scope
- Hovers/gotos for lookup selectors (struct members and others)
- Load all transitive dependent packages in shallow compilation
- Hierarchical completions for structs and instances
- Include struct and enum names in hovers
- Provide support for .v files as well (just treat as sv)

## [0.0.1]
- Initial release with hovers, gotos and completions as specified in [the documentation](https://hudson-trading.github.io/slang-server/features/features/)
- Hierarchy and Modules view for both the vscode and neovim client.
