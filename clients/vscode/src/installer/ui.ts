import * as vscode from 'vscode'
import * as path from 'path'

export class InstallerUI {
  constructor(private readonly context: vscode.ExtensionContext) {}

  get storagePath(): string {
    return this.context.globalStorageUri!.fsPath
  }

  async choose(prompt: string, options: string[]) {
    return vscode.window.showInformationMessage(prompt, ...options)
  }

  async error(message: string) {
    vscode.window.showErrorMessage(message)
  }

  async info(message: string) {
    vscode.window.showInformationMessage(message)
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

  async promptInstall(version: string): Promise<boolean> {
    const msg =
      `slang-server (${version}) is required but was not found.\n` +
      `Would you like to download and install it automatically?`

    const install = 'Install slang-server'
    const resp = await vscode.window.showInformationMessage(msg, install)
    return resp === install
  }

  async promptReuse(version: string): Promise<boolean | undefined> {
    const msg = `slang-server ${version} is already installed.`
    const use = 'Use installed version'
    const reinstall = 'Reinstall'

    const resp = await vscode.window.showInformationMessage(msg, use, reinstall)
    if (resp === use) return true
    if (resp === reinstall) return false
    return undefined
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
