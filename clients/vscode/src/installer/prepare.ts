import * as fs from 'fs/promises'
import * as path from 'path'
import { InstallerUI } from './ui'
import { installLatestSlang, latestRelease } from './install'
import * as vscode from 'vscode'
import { getPlatform } from '../lib/platform'

const BIN_NAME = getPlatform() === 'windows' ? 'slang-server.exe' : 'slang-server'

export async function prepareSlangServer(ui: InstallerUI): Promise<string> {
  const release = await latestRelease()
  const installRoot = path.join(ui.storagePath, 'install', release.tag_name)

  // Check if already installed
  const bin = await findExistingBinary(installRoot)
  if (bin !== null) {
    // installed; return path
    return bin
  }

  const ok = await ui.promptInstall(release.tag_name)
  if (!ok) {return ''}

  const binaryPath = await ui.progress('Installing slang-server...', false, async () => {
    return installLatestSlang(ui.storagePath)
  })

  vscode.window.showInformationMessage(`slang-server installed at ${binaryPath}`)
  return binaryPath
}

async function findExistingBinary(root: string): Promise<string | null> {
  // perhaps we can just look at the expected path instead of doing a full search?
  const entries = await fs.readdir(root, { recursive: true })
  for (const e of entries) {
    if (e.endsWith(BIN_NAME)) {
      return path.join(root, e)
    }
  }
  return null
}
