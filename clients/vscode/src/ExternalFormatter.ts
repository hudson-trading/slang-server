// SPDX-License-Identifier: MIT

import * as child_process from 'child_process'
import * as vscode from 'vscode'
import { getWorkspaceFolder } from './utils'
import { ConfigObject, ExtensionComponent } from './lib/libconfig'
import { Logger } from './lib/logger'

export class DeprecatedExternalFormatter extends ExtensionComponent {
  /// Here temporarily for backward compatibility
  command: ConfigObject<string> = new ConfigObject({
    description:
      'Formatter Command. The file contents will be sent to stdin, and formatted code should be sent back on stdout. E.g. `path/to/verible-format --indentation_spaces=4 -',
    default: '',
    deprecationMessage: 'Use "verilog.formatters" instead.',
    type: 'string',
  })
}

export interface ExternalFormatterConfig {
  command?: string
  dirs?: string[]
  languageIds: string[]
}

export interface ValidatedFormatterConfig {
  command: string
  dirs?: string[]
  languageIds: string[]
}

export class ExternalFormatter implements vscode.DocumentFormattingEditProvider {
  private config: ValidatedFormatterConfig
  provider: vscode.Disposable
  logger: Logger

  constructor(config: ValidatedFormatterConfig, logger: Logger) {
    this.config = config
    this.logger = logger

    let selectors: vscode.DocumentFilter[]
    if (config.dirs && config.dirs.length > 0) {
      selectors = []
      for (const language of config.languageIds) {
        for (let dir of config.dirs) {
          if (dir.endsWith('/')) {
            dir = dir.slice(0, -1)
          }
          selectors.push({
            scheme: 'file',
            language: language,
            pattern: `${getWorkspaceFolder()}/${dir}/**/*`,
          })
        }
      }
    } else {
      selectors = config.languageIds.map((language) => ({
        scheme: 'file',
        language: language,
        pattern: `${getWorkspaceFolder()}/**/*`,
      }))
    }

    this.logger.info(`Registering external formatter with config: ${JSON.stringify(config)}`)

    this.provider = vscode.languages.registerDocumentFormattingEditProvider(selectors, this)
  }

  provideDocumentFormattingEdits(
    document: vscode.TextDocument,
    _options: vscode.FormattingOptions,
    _token: vscode.CancellationToken
  ): vscode.ProviderResult<vscode.TextEdit[]> {
    if (this.config.command.length === 0) {
      return
    }
    const split = this.config.command.split(' ')
    const binPath = split[0]
    let args = split.slice(1).filter((arg) => arg !== '')
    if (binPath === undefined) {
      return []
    }

    try {
      const options: child_process.SpawnSyncOptionsWithStringEncoding = {
        cwd: getWorkspaceFolder(),
        encoding: 'utf-8',
        timeout: 2000, // 2s
        input: document.getText(),
      }

      this.logger.info(`Running external formatter: ${binPath} ${args.join(' ')}`)

      const result = child_process.spawnSync(binPath, args, options)
      if (result.error) {
        this.logger.error(`External formatter execution failed: ${result.error.message}`)
        vscode.window.showErrorMessage(`Verilog formatting failed: ${result.error.message}`)
        return []
      }
      if (result.stdout.length === 0 || result.status !== 0 || result.stderr.length > 0) {
        const stderrLines = result.stderr.split('\n').slice(0, 5).join('\n')
        this.logger.error(`External formatter failed:\n${stderrLines}`)
        vscode.window.showErrorMessage(`Verilog formatting failed:\n${stderrLines}`)
        return []
      }
      this.logger.info(`External formatter completed successfully.`)
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
      vscode.window.showErrorMessage('Formatting failed: ' + (err as Error).toString())
    }

    return []
  }
}
