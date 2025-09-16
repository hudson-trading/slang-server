# Feature Support

slang-server provides comprehensive Language Server Protocol (LSP) support for SystemVerilog, offering modern IDE features that significantly enhance development productivity.

### Symbol-Specific Feature Support

| Symbol Type          | GoTo | Hover | Completion | Notes                                       |
|----------------------|------|-------|------------|---------------------------------------------|
| **Modules**          | ✅   | ✅    | ✅         |                                             |
| **Interfaces**       | ✅   | ✅    | ✅         |                                             |
| **Classes**          | 🟡   | 🟡    | 🟡         |                                             |
| **Functions/Tasks**  | ✅   | ✅    | 🟡         | Completions can be improved with signatures |
| **Scope Variables**  | ✅   | ✅    | ✅         |                                             |
| **Ports**            | ✅   | ✅    | ❌         | Completions planned for ports on instances  |
| **Parameters**       | ✅   | ✅    | ❌         | Completions planned for params on instances |
| **Typedefs**         | ✅   | ✅    | ⚪         | Custom type definitions                     |
| **Enums**            | ✅   | ✅    | ⚪         | Enumeration values                          |
| **Packages**         | ✅   | ✅    | ✅         | Completions for package members             |
| **Wildcard Imports** | ✅   | ✅    | ✅         | Completions for imported members            |
| **Macros**           | 🟡   | ✅    | ✅         | Indexed macro completions                   |
| **System Tasks**     | ⚪   | ❌    | ❌         | Planned                                     |
<!-- | **Constraints**  | 🟡      | ❌            | 🟡       | 🟡            | Basic support²                | -->
<!-- | **Covergroups**  | ✅      | ✅       | 🟡            | Coverage point navigation     |
| **Assertions**   | ✅      | ✅       | 🟡            | SVA property support          | -->
<!-- | **DPI Functions** | 🟡     | ❌            | ✅       | ✅            | ✅            | External function imports³    | -->
