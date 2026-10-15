// SPDX-License-Identifier: MIT
import path from 'path'
import { glob } from 'glob'

export async function findWorkspaceFiles(workspace: string, patterns: string[]): Promise<string[]> {
  const results = await Promise.all(
    patterns.map((pattern) => {
      const normalized = pattern.replace(/\\/g, '/')
      const search = path.isAbsolute(normalized)
        ? normalized
        : path.join(workspace, pattern).replace(/\\/g, '/')
      return glob(search, { dot: true, ignore: '**/.git/**' })
    })
  )
  return results.flat()
}
