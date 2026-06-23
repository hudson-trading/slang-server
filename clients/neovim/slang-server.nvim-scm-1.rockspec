rockspec_format = "3.0"
package = "slang-server.nvim"
version = "scm-1"
source = {
   url = "git+https://github.com/hudson-trading/slang-server.nvim",
}
dependencies = {
   -- Add runtime dependencies here
   -- e.g. "plenary.nvim",
}
test_dependencies = {
   "lua >= 5.1",
   "nlua",
   -- this should be a regular (non-test) dependency but that is making lazyvim do things I don't understand
   "nui.nvim",
}
build = {
   type = "builtin",
   copy_directories = {
      "ftplugin",
      -- Add runtimepath directories, like
      -- 'plugin', 'ftplugin', 'doc'
      -- here. DO NOT add 'lua' or 'lib'.
   },
}
