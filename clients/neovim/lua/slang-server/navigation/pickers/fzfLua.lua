local M = {}

function M.available()
   return pcall(require, "fzf-lua")
end

---@param opts slang-server.navigation.SearchPickerOptions
function M.open(opts)
   local fzf = require("fzf-lua")
   local items = {}

   fzf.fzf_live(function(args)
      local query = args[1] or ""
      return function(cb)
         opts.search(query, function(result)
            items = {}
            for index, item in ipairs(result.matches) do
               local line = item.description and string.format("%s — %s", item.path, item.description) or item.path
               items[line] = item
               cb(line)
            end
            cb(nil)
         end)
      end
   end, {
      prompt = "Hierarchy> ",
      exec_empty_query = true,
      fzf_opts = { ["--no-sort"] = true },
      actions = {
         ["default"] = function(selected)
            local item = selected and items[selected[1]]
            if item then
               opts.select(item)
            end
         end,
      },
   })
end

return M
