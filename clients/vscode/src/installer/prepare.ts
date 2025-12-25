import * as fs from 'fs/promises'
import * as path from 'path'
import { InstallerUI } from './ui'
import { installLatestSlang, latestRelease } from './install'
import * as vscode from 'vscode'

const BIN_NAME = process.platform === 'win32' ? 'slang-server.exe' : 'slang-server'

function expectedInstallPath(storagePath: string, tag: string) {
  return path.join(storagePath, 'install', tag)
}

export async function prepareSlangServer(ui: InstallerUI): Promise<string> {
  const release = await latestRelease()
  const installRoot = expectedInstallPath(ui.storagePath, release.tag_name)

  // Check if already installed
  try {
    const bin = await findExistingBinary(installRoot)
    return bin
  } catch {
    // Not installed, continue
  }

  const ok = await ui.promptInstall(release.tag_name)
  if (!ok) return ''

  const binaryPath = await ui.progress('Installing slang-server…', false, async () => {
    return installLatestSlang(ui.storagePath)
  })

  vscode.window.showInformationMessage(`slang-server installed at ${binaryPath}`)
  return binaryPath
}

async function findExistingBinary(root: string): Promise<string> {
  const entries = await fs.readdir(root, { recursive: true })
  for (const e of entries) {
    if (typeof e === 'string' && e.endsWith(BIN_NAME)) {
      return path.join(root, e)
    }
  }
  throw new Error('Not found')
}
