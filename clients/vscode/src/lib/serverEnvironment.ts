// SPDX-License-Identifier: MIT
import * as fs from 'fs'
import * as path from 'path'
import { parse, ParseError, printParseErrorCode } from 'jsonc-parser'

/** Read literal startup overrides in the server's workspace, user, local order. */
export function loadServerEnvironment(
  workspace: string | undefined,
  home: string | undefined,
  reportError: (message: string) => void
): Record<string, string> {
  const files = [
    workspace && path.join(workspace, '.slang', 'server.json'),
    home && path.join(home, '.slang', 'server.json'),
    workspace && path.join(workspace, '.slang', 'local', 'server.json'),
  ]
  let env: Record<string, string> = {}
  for (const file of files) {
    if (!file) {
      continue
    }
    try {
      const errors: ParseError[] = []
      const config = parse(fs.readFileSync(file, 'utf8'), errors, { allowTrailingComma: true })
      if (errors.length) {
        throw new Error(`${printParseErrorCode(errors[0].error)} at offset ${errors[0].offset}`)
      }
      if (!config || typeof config !== 'object' || Array.isArray(config)) {
        throw new Error('expected a configuration object')
      }
      if (!Object.prototype.hasOwnProperty.call(config, 'env')) {
        continue
      }
      if (!config.env || typeof config.env !== 'object' || Array.isArray(config.env)) {
        throw new Error('env must be an object mapping variable names to strings')
      }
      for (const [name, value] of Object.entries(config.env)) {
        if (!name || /[=\0]/.test(name) || typeof value !== 'string' || value.includes('\0')) {
          throw new Error(
            `invalid env entry ${JSON.stringify(name)}: expected a variable name and string value without NUL bytes`
          )
        }
      }
      env = { ...env, ...config.env }
    } catch (error) {
      if (['ENOENT', 'ENOTDIR'].includes((error as { code?: string }).code ?? '')) {
        continue
      }
      reportError(`Failed to load slang-server environment from ${file}: ${String(error)}`)
    }
  }
  return env
}
