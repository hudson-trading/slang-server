import path from 'path'
import * as vscode from 'vscode'
import { ConfigObject } from './lib/libconfig'
import { getWorkspaceFolder } from './utils'
import fs = require('fs')

async function fileExists(filePath: string): Promise<boolean> {
  try {
    await fs.promises.access(filePath, fs.constants.F_OK)
    return true
  } catch {
    return false
  }
}
export class IncludeConfig extends ConfigObject<string[]> {
  async resolve(
    doc: vscode.TextDocument,
    p: string,
    allowWorkspace = true,
    allowSibling = true
  ): Promise<string | undefined> {
    if (path.isAbsolute(p)) {
      if (await fileExists(p)) {
        return p
      }
    }

    let promises = [this.resolveIncludes(p)]
    if (allowWorkspace) {
      promises.push(this.resolveWorkspace(p))
    }
    if (allowSibling) {
      promises.push(this.resolveSibling(doc, p))
    }

    for (let p of promises) {
      const resolved = await p
      if (resolved !== undefined) {
        return resolved
      }
    }
    return undefined
  }

  private async resolveIncludes(p: string): Promise<string | undefined> {
    for (let incdir of this.getValue()) {
      let fullpath: string
      if (path.isAbsolute(incdir)) {
        fullpath = path.join(incdir, p)
      } else {
        const wsFolder = getWorkspaceFolder()
        if (wsFolder === undefined) {
          continue
        }
        fullpath = path.join(wsFolder, incdir, p)
        console.log('checking', fullpath)
      }
      if (await fileExists(fullpath)) {
        return fullpath
      }
    }
    return undefined
  }

  private async resolveSibling(doc: vscode.TextDocument, p: string): Promise<string | undefined> {
    const wsFolder = getWorkspaceFolder()
    if (wsFolder === undefined) {
      return undefined
    }
    const docPath = path.dirname(doc.fileName)
    const fullpath = path.join(docPath, p)
    if (await fileExists(fullpath)) {
      return fullpath
    }
    return undefined
  }

  private async resolveWorkspace(p: string): Promise<string | undefined> {
    const wsFolder = getWorkspaceFolder()
    if (wsFolder === undefined) {
      return undefined
    }
    const fullpath = path.join(wsFolder, p)
    if (await fileExists(fullpath)) {
      return fullpath
    }
    return undefined
  }
}
