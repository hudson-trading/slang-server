// SPDX-License-Identifier: MIT
import * as vscode from 'vscode'
import { isSystemVerilog } from '../utils'
import { BaseLinter, LintOutput } from './BaseLinter'

export class VerilatorLinter extends BaseLinter {
  constructor() {
    super('verilator')
  }

  protected toolArgs(doc: vscode.TextDocument): string[] {
    const args = ['--lint-only']
    if (isSystemVerilog(doc.languageId)) {
      args.push('-sv')
    }
    return args
  }

  protected parseDiagnostics(_doc: vscode.TextDocument, output: LintOutput): vscode.Diagnostic[] {
    const diagnostics: vscode.Diagnostic[] = []
    const lines = output.stderr.split(/\r?\n/)

    for (let n = 0; n < lines.length; n++) {
      const line = lines[n]
      if (!line.startsWith('%')) {
        continue
      }

      // alternate:
      // "%Error(-[A-Z0-9]+)?: ((\\S+):(\\d+):((\\d+):)? )?(.*)$",
      const rex = line.match(/%(\w+)(-\w+)?: (\S+):(\d+):(\d+): (.+)/)
      if (!rex || rex[0].length === 0) {
        continue
      }

      const severity = rex[1]
      const warningType = rex[2] !== undefined ? rex[2].substring(1) : ''
      const lineNum = Number(rex[4]) - 1
      const colNum = Number(rex[5]) - 1
      const msg = rex[6]

      const pline = lines[n + 2]
      const pindex = pline.indexOf('^')
      const elen = pline.length - pindex
      n += 2

      if (!isNaN(lineNum)) {
        const diagnostic = new vscode.Diagnostic(
          new vscode.Range(lineNum, colNum, lineNum, colNum + elen),
          msg,
          this.convertSeverity(severity)
        )
        if (warningType) {
          diagnostic.code = warningType
        }
        diagnostic.source = 'verilator'
        diagnostics.push(diagnostic)
      }
    }
    if (diagnostics.length > 0) {
      const summary = diagnostics
        .map(
          (d) =>
            `${d.source} ${d.severity === 0 ? 'Error' : d.severity === 1 ? 'Warning' : 'Info'} [${d.range.start.line + 1}:${d.range.start.character}-${d.range.end.line + 1}:${d.range.end.character}] ${d.message}`
        )
        .join('\n')
      this.logger.info(`Parsed ${diagnostics.length} diagnostic(s):\n${summary}`)
    }
    return diagnostics
  }

  private convertSeverity(severity: string): vscode.DiagnosticSeverity {
    if (severity === 'Error') {
      return vscode.DiagnosticSeverity.Error
    } else if (severity === 'Warning') {
      return vscode.DiagnosticSeverity.Warning
    }
    return vscode.DiagnosticSeverity.Information
  }
}
