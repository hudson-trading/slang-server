import * as vscode from 'vscode'
import * as path from 'path'
import { installLatestSlang } from './install'
import { PathConfigObject } from '../lib/libconfig'

export class InstallerUI {
  constructor(
    readonly context: vscode.ExtensionContext,
    readonly slangPathConfig: PathConfigObject
  ) {}

  get storagePath(): string {
    return this.context.globalStorageUri.fsPath
  }

  info(msg: string) {
    vscode.window.showInformationMessage(msg)
  }

  error(msg: string) {
    vscode.window.showErrorMessage(msg)
  }

  async promptInstall(version: string) {
    const result = await vscode.window.showInformationMessage(
      `Slang language server is not installed.\nInstall version ${version}?`,
      'Install'
    )

    if (result === 'Install') {
      await this.installLatest()
    }
  }

  async installLatest() {
    const abort = new AbortController()

    try {
      const binPath = await vscode.window.withProgress(
        {
          location: vscode.ProgressLocation.Notification,
          title: 'Installing slang-server…',
          cancellable: true,
        },
        (_, token) => {
          token.onCancellationRequested(() => abort.abort())
          return installLatestSlang(this.storagePath)
        }
      )

      await this.slangPathConfig.updateValue(binPath)

      vscode.window.showInformationMessage(
        'slang-server installed successfully. Restarting language server…'
      )

      await vscode.commands.executeCommand('slang.restartLanguageServer')
    } catch (e) {
      if (!abort.signal.aborted) {
        this.error(`Failed to install slang-server: ${String(e)}`)
      }
    }
  }
}
