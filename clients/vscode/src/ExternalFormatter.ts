// SPDX-License-Identifier: MIT

import * as child_process from 'child_process'
import * as vscode from 'vscode'
import { ConfigObject, ExtensionComponent } from './lib/libconfig'
import { getWorkspaceFolder } from './utils'

export class ExternalFormatter
  extends ExtensionComponent
  implements vscode.DocumentFormattingEditProvider
{
  command: ConfigObject<string> = new ConfigObject({
    description:
      'Formatter Command. The file contents will be sent to stdin, and formatted code should be sent back on stdout. E.g. `path/to/verible-format --indentation_spaces=4 -',
    default: '',
    type: 'string',
  })

  provideDocumentFormattingEdits(
    document: vscode.TextDocument,
    _options: vscode.FormattingOptions,
    _token: vscode.CancellationToken
  ): vscode.ProviderResult<vscode.TextEdit[]> {
    this.logger.info(`formatting ${document.uri.fsPath}`)
    let command: string = this.command.getValue()
    if (command.length === 0) {
      return
    }
    const split = command.split(' ')
    const binPath = split[0]
    const args = split.slice(1)
    if (binPath === undefined) {
      this.logger.warn('No path specified for formatter')
      return []
    }

    this.logger.info('Executing command: ' + binPath + ' ' + args.join(' '))

    try {
      const result = child_process.spawnSync(binPath, args, {
        input: document.getText(),
        cwd: getWorkspaceFolder(),
        encoding: 'utf-8',
        timeout: 2000,
      })
      if (result.stdout.length === 0) {
        vscode.window.showErrorMessage('Verilog formatting failed: empty output')
        return []
      }
      if (result.status === null) {
        vscode.window.showErrorMessage('Verilog formatting failed: timed out')
        return []
      }
      return [
        vscode.TextEdit.replace(
          new vscode.Range(
            document.positionAt(0),
            document.lineAt(document.lineCount - 1).range.end
          ),
          result.stdout
        ),
      ]
    } catch (err) {
      this.logger.error('Formatting failed: ' + (err as Error).toString())
    }

    return []
  }

  provider: vscode.Disposable | undefined

  activateFormatter(formatDirs: string[], exts: string[], language: string): void {
    if (this.provider !== undefined) {
      this.provider.dispose()
    }

    let dirSel = undefined
    if (formatDirs.length > 0) {
      dirSel = formatDirs.length > 1 ? `{${formatDirs.join(',')}}` : formatDirs[0]
    }

    const sel: vscode.DocumentSelector = {
      scheme: 'file',
      language: language,
      pattern:
        formatDirs.length > 0
          ? `${getWorkspaceFolder()}/${dirSel}/**/*.{${exts.join(',')}}`
          : undefined,
    }

    this.provider = vscode.languages.registerDocumentFormattingEditProvider(sel, this)
  }
}
