local M = {}

function M.available()
   return true
end

---@param opts slang-server.navigation.SearchPickerOptions
function M.open(opts)
   vim.ui.input({ prompt = "Search hierarchy: " }, function(query)
      if query == nil then
         return
      end
      opts.search(query, function(result)
         if #result.matches == 0 then
            vim.notify("No hierarchy matches found", vim.log.levels.WARN)
            return
         end
         local prompt = "Select hierarchy object"
         if result.totalResults > #result.matches then
            prompt = string.format("Select hierarchy object (%d of %d)", #result.matches, result.totalResults)
         end
         vim.ui.select(result.matches, {
            prompt = prompt,
            format_item = function(item)
               return item.description and string.format("%s — %s", item.path, item.description) or item.path
            end,
         }, function(item)
            if item then
               opts.select(item)
            end
         end)
      end)
   end)
end

return M
