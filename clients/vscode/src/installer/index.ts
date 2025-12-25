import * as fs from 'fs/promises'
import * as path from 'path'
import { InstallerUI } from './ui'
import { installLatestSlang, latestRelease } from './install'

const BIN_NAME = process.platform === 'win32' ? 'slang-server.exe' : 'slang-server'

function expectedInstallPath(storagePath: string, tag: string) {
  return path.join(storagePath, 'install', tag)
}

export type InstallResult = { path: string; installed: boolean } | null
export async function prepareSlangServer(ui: InstallerUI): Promise<InstallResult> {
  const release = await latestRelease()
  const installRoot = expectedInstallPath(ui.storagePath, release.tag_name)

  // Check if already installed
  try {
    const bin = await findExistingBinary(installRoot)
    return { path: bin, installed: false }
  } catch {
    // Not installed, continue
  }

  const ok = await ui.promptInstall(release.tag_name)
  if (!ok) return null

  const binaryPath = await ui.progress('Installing slang-server…', false, async () => {
    return installLatestSlang(ui.storagePath)
  })

  await ui.info(`slang-server installed at ${binaryPath}`)
  return { path: binaryPath, installed: true }
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
