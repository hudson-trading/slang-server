// SPDX-License-Identifier: MIT
import * as vscode from 'vscode'
import { ExtensionComponent } from '../lib/libconfig'
import { getWorkspaceFolder, isAnyVerilog } from '../utils'
import { BaseLinter } from './BaseLinter'
import { VerilatorLinter } from './VerilatorLinter'

export class LintManager extends ExtensionComponent {
  private linters: BaseLinter[] = []

  verilator: VerilatorLinter = new VerilatorLinter()

  async activate(context: vscode.ExtensionContext): Promise<void> {
    this.linters = [this.verilator]
    this.logger.info(`activating with ${this.linters.length} linter(s)`)

    for (const linter of this.linters) {
      this.logger.info(`activating linter: ${linter.toolName}`)
      await linter.activate(context)
    }

    context.subscriptions.push(
      vscode.window.onDidChangeActiveTextEditor((editor) => {
        if (editor) {
          this.lint(editor.document)
        }
      })
    )

    context.subscriptions.push(
      vscode.workspace.onDidSaveTextDocument((doc) => this.lint(doc))
    )

    context.subscriptions.push(
      vscode.workspace.onDidCloseTextDocument((doc) => this.removeDiagnostics(doc))
    )

    this.onConfigUpdated(() => {
      this.logger.info('config updated, clearing all linter diagnostics')
      for (const linter of this.linters) {
        linter.clearAll()
      }
    })
  }

  private async lint(doc: vscode.TextDocument): Promise<void> {
    if (!isAnyVerilog(doc.languageId)) {
      return
    }
    this.logger.info(`linting ${doc.uri.fsPath}`)
    const workspaceFolder = getWorkspaceFolder()
    await Promise.all(this.linters.map((l) => l.lint(doc, workspaceFolder)))
  }

  private removeDiagnostics(doc: vscode.TextDocument): void {
    for (const linter of this.linters) {
      linter.clear(doc)
    }
  }
}
