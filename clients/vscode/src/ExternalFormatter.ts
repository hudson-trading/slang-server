// SPDX-License-Identifier: MIT

import * as path from 'path'
import * as vscode from 'vscode'
import { getWorkspaceFolder } from './utils'
import {
  DEFAULT_FORMATTER_TIMEOUT_MS,
  FormatProcessResult,
  runFormatterProcess,
} from './lib/formatProcess'
import { ConfigObject, ExtensionComponent } from './lib/libconfig'
import { Logger } from './lib/logger'

/// How long a formatter runs before a cancellable progress notification appears.
const PROGRESS_DELAY_MS = 1000

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
  timeout?: number
}

export interface ValidatedFormatterConfig {
  command: string
  dirs?: string[]
  languageIds: string[]
  timeout?: number
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

  async provideDocumentFormattingEdits(
    document: vscode.TextDocument,
    _options: vscode.FormattingOptions,
    token: vscode.CancellationToken
  ): Promise<vscode.TextEdit[]> {
    if (this.config.command.length === 0) {
      return []
    }
    const split = this.config.command.split(' ')
    const binPath = split[0]
    const args = split.slice(1).filter((arg) => arg !== '')
    if (binPath === undefined || binPath.length === 0) {
      return []
    }

    const timeoutMs = this.config.timeout ?? DEFAULT_FORMATTER_TIMEOUT_MS
    const version = document.version
    const abort = new AbortController()
    const tokenListener = token.onCancellationRequested(() => abort.abort())

    this.logger.info(`Running external formatter: ${binPath} ${args.join(' ')}`)
    const run = runFormatterProcess(binPath, args, document.getText(), {
      cwd: getWorkspaceFolder(),
      timeoutMs,
      signal: abort.signal,
    })

    // Offer a Cancel button if the formatter is still running after a short delay.
    const progressTimer = setTimeout(() => {
      void vscode.window.withProgress(
        {
          location: vscode.ProgressLocation.Notification,
          title: `Formatting ${path.basename(document.fileName)}`,
          cancellable: true,
        },
        (_progress, progressToken) => {
          progressToken.onCancellationRequested(() => abort.abort())
          return run
        }
      )
    }, PROGRESS_DELAY_MS)

    let result: FormatProcessResult
    try {
      result = await run
    } finally {
      clearTimeout(progressTimer)
      tokenListener.dispose()
    }

    switch (result.kind) {
      case 'cancelled':
        this.logger.info('External formatter cancelled.')
        return []
      case 'timeout': {
        const message = `Verilog formatting timed out after ${timeoutMs / 1000}s`
        this.logger.error(message)
        void vscode.window.showErrorMessage(message)
        return []
      }
      case 'failed': {
        const lines = result.message.split('\n').slice(0, 5).join('\n')
        this.logger.error(`External formatter failed:\n${lines}`)
        void vscode.window.showErrorMessage(`Verilog formatting failed:\n${lines}`)
        return []
      }
      case 'ok':
        break
    }

    if (token.isCancellationRequested || document.version !== version) {
      this.logger.info('Document changed while formatting; discarding formatter output.')
      return []
    }
    this.logger.info(`External formatter completed successfully.`)
    return [
      vscode.TextEdit.replace(
        new vscode.Range(document.positionAt(0), document.lineAt(document.lineCount - 1).range.end),
        result.stdout
      ),
    ]
  }
}
