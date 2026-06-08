import * as process from 'process'
import * as semver from 'semver'

export type Platform = 'windows' | 'linux' | 'mac'

export type PlatformMap = { [key in Platform]: string }

export function getPlatform(): Platform {
  switch (process.platform) {
    case 'win32':
      return 'windows'
    case 'darwin':
      return 'mac'
    default:
      // includes WSL
      return 'linux'
  }
}

function getGlibcVersionString(): string | null {
  if (process.platform !== 'linux') return null
  try {
    const report = process.report?.getReport()
    if (typeof report === 'object' && report !== null) {
      return (report as { header?: { glibcVersionRuntime?: string } })
        .header?.glibcVersionRuntime ?? null
    }
  } catch {
    // process.report not available
  }
  return null
}

export function isOldGlibc(): boolean {
  const versionStr = getGlibcVersionString()
  if (!versionStr) return false
  const version = semver.coerce(versionStr)
  return version !== null && semver.lt(version, '2.39.0')
}
