# Extension Development

Install nvm (Node Version Manager), or install Node 20

```
git clone git@github.com:AndrewNolte/vscode-system-verilog.git
cd vscode-system-verilog
nvm use 20
npm install
code .
```

Go to `Run and Debug` in vscode, and click `Launch Extension`. Click refresh whenver you want to update code and restart the extension. (There's no hot reloading for extensions)

### Debugging

- The logger logs to the `verilog` output channel on the new vscode window, and is also available in production
- `console.log()` logs to the debug console of the vscode-system-verilog/ vscode window.
- `vscode.window.showInformationMessage()` is useful for showing popups to debug

### Updating package.json (config paths, commands, etc.)

Traditional vscode extension development requires many strings to match up between package.json and the code. I made a small library within this repo to generate package.json from the code where possible, so there's a single source of truth for these strings, and it's easy to add commands, buttons, and conifgurations.

To update the package.json after changing one of these components, Run "Extdev: update config (package.json and CONFIG.md)" in the vscode window you're debugging.
