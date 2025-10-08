# External Tools

There have been requests to integrate external tools with the slang language server. While integrations have only been made with waveform viewers so far, these are guidlines for how one would go about adding more integrations.

Integrations are typically working with a slang compilation (elaborated design), since these are an hdl-specific construct and outside the scope of the LSP.

In general it's preferred for the `slang-server` to initiate the handshake, since a user will likely always start with their editor, and move to more advanced tools when necessary.


## Vscode-only Integrations

Vscode apis can easily be exposed via hidden commands that aren't shown to the user. See [`ProjectComponent.ts`](https://github.com/hudson-trading/slang-server/blob/main/clients/vscode/src/sidebar/ProjectComponent.ts) for examples of some commands. Some new routes may need to be added to the server-client interface as well in [`SlangInterface.ts`](https://github.com/hudson-trading/slang-server/blob/main/clients/vscode/src/SlangInterface.ts)

There should be an api exposed to subscribe to changes to which filelist is set, as well as when updated slang compilations are available.

## Slang Cpp Integrations

Similarly, it would be ideal to subscribe to a compilation and receive updates in cpp. However to get the full detail, the server should share a non-owning reference to the compilation over shared memory. This would need to be implemented.

## Other Integrations

One can look to the [Surfer wcp work](https://gitlab.com/waveform-control-protocol/wcp) for implementing an editor-agnostic and implementation-language-agnostic integration over JsonRPC.
