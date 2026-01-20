# Workspace Indexing

slang-server's indexing system provides fast symbol lookup and navigation across large SystemVerilog codebases by building and maintaining an index of top level symbols.

The indexer uses multithreading to rapidly index your repo. Crawling a file system actually often takes longer than parsing in large unconfigured repos, so make sure your indexing config is as specific as possible.

In each syntax tree parsed, it indexes the top level symbols like moduldes, packages, etc, as well as references to other top level symbols. If no top level symbols were found, it'll instead index the macros defined in that file.
