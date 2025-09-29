# Extension Development

Install nvm (Node Version Manager), or install Node 20

```
git clone git@github.com:hudson-trading/slang-server.git
code .
cd slang-server/clients/vscode
nvm use 20
npm install -g pnpm
pnpm install
```

### Debugging

- The server logs to the `slang-server` output channel. These are triggered with the `INFO`, `WARN` and `ERROR` macros.
- The client logs to the `Slang` output channel using logger classes.
- `console.log()` logs to the debug console of the slang-server/ vscode window.
- `vscode.window.showInformationMessage()` is useful for showing popups for debugging. On the server side these can also be triggered with `LspClient::showInfo()`.

### Updating package.json (config paths, commands, etc.)

Traditional vscode extension development requires many strings to match up between package.json and the code. I made a small library within this repo to generate package.json from the code where possible, so there's a single source of truth for these strings, and it's easy to add commands, buttons, and congurations.

To update the package.json after changing one of these components, Run "Extdev: update config (package.json and CONFIG.md)" in the vscode window you're debugging.
