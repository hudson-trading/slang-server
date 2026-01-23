import * as fs from 'fs/promises'
import * as path from 'path'
import { InstallerUI } from './ui'
import { installLatestSlang, latestRelease } from './install'
import * as vscode from 'vscode'
import { getPlatform } from '../lib/platform'
import { checkForSlangUpdate, getInstalledVersion } from './updater'

const BIN_NAME = getPlatform() === 'windows' ? 'slang-server.exe' : 'slang-server'

export async function prepareSlangServer(ui: InstallerUI): Promise<string> {
  const release = await latestRelease()
  const installRoot = path.join(ui.storagePath, 'install', release.tag_name)

  await fs.mkdir(installRoot, { recursive: true })

  // Check if already installed
  const bin = await findExistingBinary(installRoot)
  if (bin !== null) {
    const latest = await latestRelease()
    const installedVersion = await getInstalledVersion(bin)

    const needsUpdate = await checkForSlangUpdate(latest.tag_name, installedVersion)

    if (needsUpdate) {
      const installed = await getInstalledVersion(bin)

      const ok = await ui.promptUpdate(installed ?? 'unknown', latest.tag_name)
      if (ok) {
        const newBin = await ui.progress('Updating slang-server...', false, async () =>
          installLatestSlang(ui.storagePath)
        )
        await ui.promptReload()
        return newBin
      }
    }

    // installed; return path
    return bin
  }

  const ok = await ui.promptInstall()
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
