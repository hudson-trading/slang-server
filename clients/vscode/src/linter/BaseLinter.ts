// SPDX-License-Identifier: MIT
import * as child_process from 'child_process'
import * as path from 'path'
import * as vscode from 'vscode'
import { ConfigObject, ExtensionComponent } from '../lib/libconfig'

export interface LintOutput {
  stdout: string
  stderr: string
}

export abstract class BaseLinter extends ExtensionComponent {
  toolName: string
  enabled: ConfigObject<boolean>
  path: ConfigObject<string>
  args: ConfigObject<string[]>

  diagnostics: vscode.DiagnosticCollection

  constructor(toolName: string, defaultEnabled: boolean = false) {
    super()
    this.toolName = toolName
    this.enabled = new ConfigObject({
      default: defaultEnabled,
      description: `Enable ${toolName} lint`,
    })
    this.path = new ConfigObject({
      default: toolName,
      description: `Path to the ${toolName} executable`,
    })
    this.args = new ConfigObject({
      default: [],
      description: `Additional arguments to pass to ${toolName}`,
    })
    this.diagnostics = vscode.languages.createDiagnosticCollection(toolName)
  }

  async activate(context: vscode.ExtensionContext): Promise<void> {
    context.subscriptions.push(this.diagnostics)
  }

  async lint(doc: vscode.TextDocument, workspaceFolder: string | undefined): Promise<void> {
    const enabled = this.enabled.getValue()
    if (!enabled) {
      return
    }

    const toolPath = this.path.getValue()
    if (!toolPath) {
      return
    }

    const args = [...this.toolArgs(doc)]
    args.push(...this.args.getValue())
    args.push(doc.uri.fsPath)

    const cwd = workspaceFolder ?? path.dirname(doc.uri.fsPath)

    this.logger.info(`${toolPath} ${args.join(' ')}`)

    try {
      const output = await this.runTool(toolPath, args, cwd)
      if (output.stderr) {
        this.logger.info(output.stderr)
      }
      const diags = this.parseDiagnostics(doc, output)
      this.diagnostics.set(doc.uri, diags)
    } catch (e: any) {
      this.logger.error(`${this.toolName} lint failed: ${e.message}`)
    }
  }

  clear(doc: vscode.TextDocument) {
    this.diagnostics.delete(doc.uri)
  }

  clearAll() {
    this.diagnostics.clear()
  }

  protected abstract toolArgs(doc: vscode.TextDocument): string[]
  protected abstract parseDiagnostics(doc: vscode.TextDocument, output: LintOutput): vscode.Diagnostic[]

  private runTool(
    command: string,
    args: string[],
    cwd: string
  ): Promise<LintOutput> {
    return new Promise((resolve, reject) => {
      child_process.execFile(
        command,
        args,
        { cwd, encoding: 'utf-8', timeout: 30000 },
        (error, stdout, stderr) => {
          if (error && error.killed) {
            reject(new Error('Process timed out'))
          } else {
            resolve({ stdout: stdout ?? '', stderr: stderr ?? '' })
          }
        }
      )
    })
  }
}
