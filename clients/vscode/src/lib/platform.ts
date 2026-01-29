import * as process from 'process'

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
