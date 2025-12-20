import { fileExists } from '../lib/libconfig'
import { latestRelease } from './install'
import { InstallerUI } from './ui'

export async function prepareSlang(ui: InstallerUI): Promise<string | null> {
  let configuredPath = await ui.slangPathConfig.getValueAsync()

  if (configuredPath && (await fileExists(configuredPath))) {
    return configuredPath
  }

  try {
    const release = await latestRelease()
    await ui.promptInstall(release.name)
  } catch {
    ui.error('Slang-server is not installed and could not be downloaded automatically.')
  }

  return null
}
