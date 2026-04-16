import * as vscode from 'vscode'

export function getWorkspaceFolder(): string | undefined {
  return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath ?? undefined
}

export function getIcons(name: string) {
  return {
    light: `./resources/light/${name}.svg`,
    dark: `./resources/dark/${name}.svg`,
  }
}

export const HDLFiles = ['verilog', 'systemverilog']
export const AnyVerilogLanguages = [
  'verilog',
  'systemverilog',
  'systemverilogheader',
  'verilogheader',
]

export const anyVerilogSelector = [
  { scheme: 'file', language: 'verilog' },
  { scheme: 'file', language: 'systemverilog' },
  { scheme: 'file', language: 'systemverilogheader' },
  { scheme: 'file', language: 'verilogheader' },
]

export function isAnyVerilog(langid: string): boolean {
  return AnyVerilogLanguages.includes(langid)
}

/**
 * Get the basename of a path without extension
 * @param filePath - Path string to extract basename from
 * @returns The basename without extension, or undefined if not found
 */
export function getBasename(filePath: string): string | undefined {
  return filePath.split(/[\\/]/).pop()?.split('.')[0]
}

export function toPosix(p: string): string {
  return p.replace(/\\/g, '/')
}
