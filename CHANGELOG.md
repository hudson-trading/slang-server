# Change Log

All major and minor changes to this project will be documented in this file.

Clients require a server with the same or greater major and minor version in order to get the full feature set.
Patches published for either are independent.
For patch releases applied to the server or one of the various clients, please look at the git log, as they are only documented here as part of following minor release.

Vscode versions are specified in `clients/vscode/package.json`.

The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

<!-- new-release-here -->
## v0.2.2 Major Changes
* Neovim Cells/Modules view when design is set
* Vscode client installs server from releases page (thanks @evanwporter!)
* Documents are synced on large workspace changes, like running an external formatter or rebasing.
* Compilation Diags when a design is set- updated on every save.
* Unused Diagnostics
* New hover format that provides more info like resolved types, constant values, bit widths, etc.
* Various completion fixes, notably struct members after array accesses.
* Various goto-def and goto-ref fixes

## What's Changed
* [vscode] use `rimraf` instead of `rm -rf` in `package.json/scripts` by @evanwporter in https://github.com/hudson-trading/slang-server/pull/175
* [goto refs] Address single file symbol coverage by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/174
* Partial elaboration tests by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/177
* Elaborate eagerly in shallow compilations, enabling better diags by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/178
* [VSCode] Remove unused code by @evanwporter in https://github.com/hudson-trading/slang-server/pull/179
* Replace AllGenerateBranches with UntakenGenerateChecks by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/180
* Enable analysis diags in shallow compilation (multidriven, unused, etc) by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/181
* Put note diags in the related information of the main diag. by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/182
* [Build] Set compile commands generation and vendored fmtlib as defaults by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/184
* Fix duplicates in slang's loadTrees function by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/185
* Use ranges instead of locations for unused diags by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/186
* Compilation diags when design is set by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/189
* Fix compile commands database by @evanwporter in https://github.com/hudson-trading/slang-server/pull/196
* Disable ctest from VSCode test explorer by @evanwporter in https://github.com/hudson-trading/slang-server/pull/194
* [Vscode] Change 'Slang' to 'slang' in package.json titles by @evanwporter in https://github.com/hudson-trading/slang-server/pull/188
* Hovers: Refactor into new file; add more info; format info like clangd by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/190
* Filter out unused def diags; simplify nodemeta accesses by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/191
* hdl test with more generate and inst arrays by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/192
* Fix hovers for instance and generate arrays by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/193
* Use fs::path in indexer instead of URI by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/199
* Slang: Parse more subexpressions when encountering an error type by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/201
* Fix capitalization of 'VS Code' in README by @evanwporter in https://github.com/hudson-trading/slang-server/pull/205
* Docs update: Add inlays and gotorefs, reorg features under one tab by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/202
* Add OpenVSX to vscode release process by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/208
* Fix tool used in openvsx release by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/211
* Test for struct completions after array accesses by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/209
* Fix struct completions after array accesses by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/210
* Parenthesized Macro arg regression by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/212
* Fix parenthesized macro arg by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/213
* less hacky luarocks testing by @toddstrader in https://github.com/hudson-trading/slang-server/pull/204
* Fix member selectors with further ElementSelectSyntax by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/215
* OpenVsx Release: Fix ovsx package manager arg; Add validation step by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/218
* Vscode: Allow omitting 'dirs' and 'languageIds' in formatter config. by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/219
* Clean up symbol indexer; limit instance depth; Allow recursive modules with setTopLevel by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/220
* Vscode: turn off color picker in verilog files by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/221
* Fix excessive threading after unused-diags addition; configure analysis options properly when design is set. by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/222
* [VSCode] Add installer to download latest release by @evanwporter in https://github.com/hudson-trading/slang-server/pull/176
* [VSCode] Modify `onStart` function to not reveal the slang sidebar on startup by @evanwporter in https://github.com/hudson-trading/slang-server/pull/227
* Don't return untaken generate branches from getScope() by @toddstrader in https://github.com/hudson-trading/slang-server/pull/230
* Fix gotos for end block names by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/228
* Fix infinite loop with some typedefs by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/233
* Module completion style fixes by @evanwporter in https://github.com/hudson-trading/slang-server/pull/226
* Fix reload on external edits by @spomata in https://github.com/hudson-trading/slang-server/pull/232
* Fix interface port completions by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/236
* Trigger textDocument/didSave only on open docs by @alex-torregrosa in https://github.com/hudson-trading/slang-server/pull/237
* Instance completions: Fix localparams being added by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/241
* Fix hovers for invalid utf chars in parameter values. by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/239
* Neovim cells interface by @toddstrader in https://github.com/hudson-trading/slang-server/pull/203
* Indexer: update on workspace/didChangeWatchedFiles notifs, typically activated on git pulls by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/242
* Slang: avoid reading header files in indexer by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/243
* Indexer: Avoid creating syntax tree and using sourceManager by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/244
* Indexer: more cleanup by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/245
* Completions: Fix interface port completions for unloaded interfaces by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/250
* update uri docs by @evanwporter in https://github.com/hudson-trading/slang-server/pull/247

**Full Changelog**: https://github.com/hudson-trading/slang-server/compare/v0.2.1...v0.2.2

## v0.2.1 Major Changes
- Goto references
- Inlay hints (positional arguments, wildcard ports, etc.)
- Windows Support (Thanks @evanwporter!)

## Detailed commit log
* Enable modern preprocessor mode for MSVC builds by @Sustrak in https://github.com/hudson-trading/slang-server/pull/112
* Static linking for release builds by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/118
* Build/Release CI: Fix mac and linux releases; adjust build CI by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/119
* Add tc for enums in macro args; Add exception support for regression tester by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/121
* Fix enum values in macro args by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/122
* Bump slang by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/129
* Ensure config is synced by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/134
* Implement Inlayhints by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/123
* Log terminal link attempts; remove files from attempts. These were being hit too often on paths. by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/124
* Stable completions in tests- this will help with an indexer rewrite if we change to an unordered_map. by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/125
* Buildfile config: Default to glob of all .f files; Add "buildRelativePaths" option in config by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/128
* Releases: use workflow for releases; Ensure tag and commit are synced by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/130
* Indexer: clean up and speed up by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/131
* Fix syntax highlighting for class typedefs by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/143
* Diags: suppress 'encountered' diags by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/144
* New indexing config for directory crawling speedup; grab all .sv, .svh, .v and vh files rather than having user specify by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/132
* Vscode Debugging: improve configs by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/138
* [Testing] Have sym goto references show the symbol kind rather than syntax kind by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/139
* Enum value arrays test case by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/140
* Index enum and struct values properly- to be used for refs by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/141
* Fix buffer invalidation issue by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/145
* Refactor shallow analysis and slang doc by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/146
* Fix typo in README.md by @evanwporter in https://github.com/hudson-trading/slang-server/pull/149
* Index module definition syms for decls; Fix inlay hints for inst arrays and class decls by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/147
* Have claude use the same build dir by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/148
* Document instructions to supply vendored copy of fmt by @mattyoung101 in https://github.com/hudson-trading/slang-server/pull/151
* Implement References and Rename by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/142
* fix `vscode` linter not recoginizing `slang.Config` by @evanwporter in https://github.com/hudson-trading/slang-server/pull/155
* Avoid indexer build issue on older gcc; update some docs; Fix inlay issue by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/153
* Add consistent formatting to console logs in `vscode` extension by @evanwporter in https://github.com/hudson-trading/slang-server/pull/156
* Search in PATH for waveform viewer executable by @max-kudinov in https://github.com/hudson-trading/slang-server/pull/162
* ub fixes by @evanwporter in https://github.com/hudson-trading/slang-server/pull/164
* Regression tests for hierarchy view routes by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/166
* [Neovim] Pass position encoding of the first client by @max-kudinov in https://github.com/hudson-trading/slang-server/pull/163
* Hierarchy view: don't index uninstantiated modules by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/167
* Configure yaml formatting (precommit, vscode, run formatting) by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/168
* Windows Compatibility by @evanwporter in https://github.com/hudson-trading/slang-server/pull/157
* [VSCode] modules should not expand when a module is clicked in the hierarchy by @evanwporter in https://github.com/hudson-trading/slang-server/pull/169
* Completion: Recommend keywords on the LHS of expressions by @mattyoung101 in https://github.com/hudson-trading/slang-server/pull/152
* [Source management] Fix `include file invalidation; switch to shared_ptrs for filedata by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/171
* Check workspace folders length in initialize by @AndrewNolte in https://github.com/hudson-trading/slang-server/pull/172

**Full Changelog**: https://github.com/hudson-trading/slang-server/compare/v0.2.0...v0.2.1

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
