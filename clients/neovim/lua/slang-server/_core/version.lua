if vim.fn.has("nvim-0.10") == 0 then
   error("slang-server.nvim requires Neovim 0.10.0 or newer", 2)
end

return true
