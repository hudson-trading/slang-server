// Persists the active build/top-level selection across VS Code sessions.
// Stored per-workspace in .slang/local/, which is machine/session state and
// should not be committed - client-agnostic on purpose so a future non-vscode
// client could read the same file.
import { promises as fs } from 'fs'
import * as path from 'path'
import * as vscode from 'vscode'
import { BuildCommandArgs } from './BuildConfigUtils'
import { getWorkspaceFolder } from '../utils'

export type PersistedCompilationSource =
  | { type: 'filelist'; buildfile: string }
  | { type: 'commandBuild'; buildfile: string; args: BuildCommandArgs }
  | { type: 'topfile'; topFile: string }

function getMementoPath(): string | undefined {
  const ws = getWorkspaceFolder()
  return ws === undefined ? undefined : path.join(ws, '.slang', 'local', 'memento.json')
}

export async function loadCompilationSourceMemento(): Promise<
  PersistedCompilationSource | undefined
> {
  const mementoPath = getMementoPath()
  if (mementoPath === undefined) {
    return undefined
  }
  try {
    const contents = await fs.readFile(mementoPath, 'utf-8')
    return JSON.parse(contents) as PersistedCompilationSource
  } catch {
    // Missing or corrupt memento is fine - just means no prior selection.
    return undefined
  }
}

export async function saveCompilationSourceMemento(
  source: PersistedCompilationSource | undefined
): Promise<void> {
  const mementoPath = getMementoPath()
  if (mementoPath === undefined) {
    return
  }
  try {
    if (source === undefined) {
      await fs.rm(mementoPath, { force: true })
      return
    }
    await fs.mkdir(path.dirname(mementoPath), { recursive: true })
    await fs.writeFile(mementoPath, JSON.stringify(source, null, 2))
  } catch (e) {
    // Best-effort: losing the persisted selection isn't worth surfacing an error for.
    void e
  }
}

export function uriToPersistedTopFile(topFile: vscode.Uri): PersistedCompilationSource {
  return { type: 'topfile', topFile: topFile.fsPath }
}
