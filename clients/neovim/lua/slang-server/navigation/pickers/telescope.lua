local M = {}

function M.available()
   return pcall(require, "telescope.pickers")
end

---@param opts slang-server.navigation.SearchPickerOptions
function M.open(opts)
   local actions = require("telescope.actions")
   local action_state = require("telescope.actions.state")
   local entry_display = require("telescope.pickers.entry_display")
   local pickers = require("telescope.pickers")
   local sorters = require("telescope.sorters")

   local displayer = entry_display.create({ separator = " ", items = { {}, { remaining = true } } })
   local finder = {
      close = function() end,
      find = function(_, prompt, process_result, process_complete)
         opts.search(prompt, function(result)
            for index, item in ipairs(result.matches) do
               process_result({
                  value = item,
                  ordinal = item.path,
                  display = function()
                     return displayer({ item.path, item.description or "" })
                  end,
                  index = index,
               })
            end
            process_complete()
         end)
      end,
   }

   pickers.new({}, {
      prompt_title = "Search hierarchy",
      finder = finder,
      sorter = sorters.empty(),
      attach_mappings = function(prompt_bufnr)
         actions.select_default:replace(function()
            local selection = action_state.get_selected_entry()
            actions.close(prompt_bufnr)
            if selection then
               opts.select(selection.value)
            end
         end)
         return true
      end,
   }):find()
end

return M
