import * as vscode from 'vscode'

export class InstallerUI {
  constructor(private readonly context: vscode.ExtensionContext) {}

  get storagePath(): string {
    return this.context.globalStorageUri.fsPath
  }

  async progress<T>(
    title: string,
    cancellable: boolean,
    body: (progress: (fraction: number) => void, abort: AbortController | null) => Promise<T>
  ): Promise<T> {
    const abort = cancellable ? new AbortController() : null

    return vscode.window.withProgress(
      {
        location: vscode.ProgressLocation.Notification,
        title,
        cancellable,
      },
      async (progress, token) => {
        if (abort) {
          token.onCancellationRequested(() => abort.abort())
        }

        let last = 0
        return body((fraction) => {
          const delta = fraction - last
          if (delta > 0) {
            progress.report({ increment: delta * 100 })
            last = fraction
          }
        }, abort)
      }
    )
  }

  async promptInstall(): Promise<boolean> {
    const msg =
      `slang-server is required but was not found.\n` +
      `Would you like to download and install it?`

    const install = 'Install slang-server'
    const resp = await vscode.window.showInformationMessage(msg, install)
    return resp === install
  }

  async promptReload() {
    const resp = await vscode.window.showInformationMessage(
      'slang-server installation complete. Restart language server?',
      'Restart'
    )
    if (resp === 'Restart') {
      vscode.commands.executeCommand('slang.restartLanguageServer')
    }
  }
}
