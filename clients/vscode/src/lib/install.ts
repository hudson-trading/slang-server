import * as fs from 'fs/promises'
import * as path from 'path'
import decompress from 'decompress'
import * as semver from 'semver'
import * as stream from 'stream'
import { pipeline } from 'stream/promises'
import { getPlatform, Platform, PlatformMap } from './platform'
import type { Logger } from './logger'

type InstallLogger = Pick<Logger, 'info'>

export function isUpdateAvailable(latest: string, installed: string): boolean {
  console.log(`Comparing installed version '${installed}' to latest version '${latest}'`)
  if (!installed) {
    return true
  }
  if (!semver.valid(installed) || !semver.valid(latest)) {
    return true
  }
  return semver.lt(installed, latest)
}

export function chooseInstalledBinary(entries: string[], binaryName: string): string | undefined {
  const candidates = entries.filter((entry) => entry.split(/[\\/]/).at(-1) === binaryName)
  return candidates.sort((left, right) => {
    const leftVersion = semver.parse(left.split(/[\\/]/)[0])
    const rightVersion = semver.parse(right.split(/[\\/]/)[0])

    if (leftVersion && rightVersion) {
      return semver.rcompare(leftVersion, rightVersion)
    }
    if (leftVersion) {
      return -1
    }
    if (rightVersion) {
      return 1
    }
    return left.localeCompare(right)
  })[0]
}

type GithubAsset = {
  name: string
  browser_download_url: string
}

type GithubRelease = {
  name: string
  tag_name: string
  assets: GithubAsset[]
}

const assetPlatformNames: PlatformMap = {
  windows: 'windows',
  linux: 'linux',
  mac: 'macos',
}

export interface GithubInstallerConfig {
  githubRepo: string // e.g., 'hudson-trading/slang-server'
  assetNames: PlatformMap // asset name per platform
  platform?: Platform // optionally override platform detection
}

function getConfigPlatform(config: GithubInstallerConfig): Platform {
  return config.platform ?? getPlatform()
}

export async function latestRelease(
  config: GithubInstallerConfig,
  logger?: InstallLogger
): Promise<GithubRelease> {
  const url = `https://api.github.com/repos/${config.githubRepo}/releases/latest`
  logger?.info(`Fetching release metadata from ${url}`)

  const timeoutController = new AbortController()
  const timeout = setTimeout(() => {
    timeoutController.abort()
  }, 5000)
  try {
    const response = await fetch(url, {
      signal: timeoutController.signal,
    })
    logger?.info(`Release metadata response: HTTP ${response.status} ${response.statusText}`)
    if (!response.ok) {
      throw new Error(`Can't fetch release: HTTP ${response.status} ${response.statusText}`)
    }
    return (await response.json()) as GithubRelease
  } finally {
    clearTimeout(timeout)
  }
}

export function chooseReleaseAsset(
  release: GithubRelease,
  config: GithubInstallerConfig
): GithubAsset {
  const platform = getConfigPlatform(config)
  const assetName = config.assetNames[platform]

  if (!assetName) {
    throw new Error(`No asset configured for platform ${platform}`)
  }

  const asset = release.assets.find((a) => a.name === assetName)
  if (asset) {
    return asset
  }

  const architecture = assetName.match(/-(x64|arm64)(?:[.-])/)?.[1]
  const platformName = assetPlatformNames[platform]
  const fallback = release.assets
    .filter((candidate) => {
      const name = candidate.name.toLowerCase()
      return (
        (name.endsWith('.tar.gz') || name.endsWith('.zip')) &&
        name.includes(`-${platformName}`) &&
        (!architecture || name.includes(`-${architecture}`))
      )
    })
    .sort(
      (left, right) => left.name.length - right.name.length || left.name.localeCompare(right.name)
    )[0]

  if (fallback) {
    console.warn(`Release asset '${assetName}' not found; using '${fallback.name}' instead`)
    return fallback
  }

  throw new Error(`No compatible release asset '${assetName}' found for ${platform}`)
}

async function download(
  url: string,
  dest: string,
  logger?: InstallLogger,
  abort?: AbortController
) {
  logger?.info(`Downloading ${url} to ${dest}`)
  const res = await fetch(url, { signal: abort?.signal })
  logger?.info(`Download response: HTTP ${res.status} ${res.statusText}`)
  if (!res.ok || !res.body) {
    throw new Error(`Failed to download ${url}: HTTP ${res.status} ${res.statusText}`)
  }

  await fs.mkdir(path.dirname(dest), { recursive: true })

  const file = await fs.open(dest, 'w')
  try {
    await pipeline(res.body as unknown as stream.Readable, file.createWriteStream())
  } catch (e) {
    // Error? Clean up partial file
    await fs.rm(dest, { force: true })
    throw e
  } finally {
    await file.close()
  }
}

export async function installFromGithub(
  storagePath: string,
  config: GithubInstallerConfig,
  binaryNames: PlatformMap,
  logger?: InstallLogger
): Promise<string> {
  const platform = getConfigPlatform(config)
  logger?.info(`Installing ${config.githubRepo} for ${platform}/${process.arch} in ${storagePath}`)
  const release = await latestRelease(config, logger)
  logger?.info(
    `Latest release: ${release.tag_name}; requested asset: ${config.assetNames[platform]}`
  )
  const asset = chooseReleaseAsset(release, config)
  logger?.info(`Selected release asset: ${asset.name}`)

  const binaryName = binaryNames[platform]
  if (!binaryName) {
    throw new Error(`No binary name configured for platform ${platform}`)
  }

  const downloadDir = path.join(storagePath, 'download')
  await fs.mkdir(downloadDir, { recursive: true })
  const stagingDir = await fs.mkdtemp(path.join(downloadDir, 'install-'))
  try {
    const archivePath = path.join(stagingDir, asset.name)
    const extractDir = path.join(stagingDir, 'extracted')
    await download(asset.browser_download_url, archivePath, logger)
    logger?.info(`Extracting ${archivePath} to ${extractDir}`)
    await decompress(archivePath, extractDir)

    const stagedBinaryPath = path.join(extractDir, binaryName)
    logger?.info(`Checking extracted binary: ${stagedBinaryPath}`)
    if (!(await fs.stat(stagedBinaryPath)).isFile()) {
      throw new Error(`Release asset '${asset.name}' does not contain binary '${binaryName}'`)
    }
    if (platform !== 'windows') {
      await fs.chmod(stagedBinaryPath, 0o755)
    }

    const installDir = path.join(storagePath, 'install', release.tag_name)
    logger?.info(`Installing extracted files to ${installDir}`)
    await fs.mkdir(installDir, { recursive: true })
    for (const entry of await fs.readdir(extractDir)) {
      if (entry !== binaryName) {
        await fs.rename(path.join(extractDir, entry), path.join(installDir, entry))
      }
    }

    // Rename replaces the directory entry without writing to the running executable's inode.
    const binaryPath = path.join(installDir, binaryName)
    logger?.info(`Replacing ${binaryPath} with ${stagedBinaryPath}`)
    await fs.rename(stagedBinaryPath, binaryPath)
    return binaryPath
  } finally {
    logger?.info(`Removing temporary installation directory: ${stagingDir}`)
    await fs.rm(stagingDir, { recursive: true, force: true })
  }
}
