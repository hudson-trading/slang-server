
# Limitations

**Potential Shallow Compilation Issues**

In shallow compilations some nodes may be invalid due to unset parameters, missing defines, etc. Therefore some expressions may not be visited by slang and cause some false positives, especially with unused checking. These can often be fixed, so please raise an issue if you see one. Before raising an issue, please check whether it shows up via a normal slang command to determine which repo to raise the issue in.

**Untaken Ifdef Branches**

Features may be limited in the context of untaken ifdef branches. To minimize this, it's recommended to encode these ifdefs in package parameters, then use generate blocks in the hdl code. Or if these are single branch ifdefs, include the most permissible defines in your server config.

**Deep Hierarchical References**

Shallow compilations only load the directly referenced syntax trees, and only load more transitive trees through packages. This may therefore cause issues with deep hierarchical references.

**Single Unit Compilations**

Designs that use slang's [`--single-unit`](https://sv-lang.com/command-line-ref.html) are not be supported. The main effect this tends to have is that macros are not inherited and so diagnostics will occur there. This flag means that all files are essentially concatenated before being sent to tools. Even if `slang-server` grabbed these dependencies, it would make file and compilation updates take much longer, so it's generally preferred to switch away from using this flag.

**Some UVM Code**

The common UVM practice where classes are `included in a package results in poor support for those classes.
