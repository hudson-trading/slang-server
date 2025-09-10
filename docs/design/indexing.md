# Workspace Indexing

slang-server's indexing system provides fast symbol lookup and navigation across large SystemVerilog codebases by building and maintaining an index of top level symbols.

## How it works

The Indexer indexes based on these config options on startup: 

  * `indexGlobs` (default: `"./.../*.sv*"`)
  * `excludeDirs` (default: `[]`)

It uses multithreading to rapidly index your repo. Crawling a file system actually often takes longer than parsing, so make sure these are as specific as possible.

In each syntax tree parsed, it indexes the top level symbols like moduldes, packages, etc. If no top level symbols were found, it'll instead index the macros defined in that file.

In the future, this will also index the referenced top level symbols to enable the go-to references feature.