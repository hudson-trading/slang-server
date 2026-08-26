import * as fs from 'fs/promises'
import * as path from 'path'
import decompress from 'decompress'
import * as semver from 'semver'
import * as stream from 'stream'
import { pipeline } from 'stream/promises'
import { getPlatform, Platform, PlatformMap } from './platform'

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

export async function latestRelease(config: GithubInstallerConfig): Promise<GithubRelease> {
  const url = `https://api.github.com/repos/${config.githubRepo}/releases/latest`

  const timeoutController = new AbortController()
  const timeout = setTimeout(() => {
    timeoutController.abort()
  }, 5000)
  try {
    const response = await fetch(url, {
      signal: timeoutController.signal,
    })
    if (!response.ok) {
      console.error(response.url, response.status, response.statusText)
      throw new Error(`Can't fetch release: ${response.statusText}`)
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

async function download(url: string, dest: string, abort?: AbortController) {
  const res = await fetch(url, { signal: abort?.signal })
  if (!res.ok || !res.body) {
    throw new Error(`Failed to download ${url}`)
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

async function extractArchive(archive: string, dest: string) {
  await fs.mkdir(dest, { recursive: true })

  await decompress(archive, dest, {
    strip: 0, // keep directory structure
  })
}

async function ensureExecutable(binPath: string, platform: Platform) {
  if (platform !== 'windows') {
    await fs.chmod(binPath, 0o755)
  }
}

export async function installFromGithub(
  storagePath: string,
  config: GithubInstallerConfig,
  binaryNames: PlatformMap
): Promise<string> {
  const platform = getConfigPlatform(config)
  const release = await latestRelease(config)
  const asset = chooseReleaseAsset(release, config)

  const downloadDir = path.join(storagePath, 'download')
  const installDir = path.join(storagePath, 'install', release.tag_name)
  const archivePath = path.join(downloadDir, asset.name)

  await download(asset.browser_download_url, archivePath)

  await extractArchive(archivePath, installDir)

  const binaryName = binaryNames[platform]
  if (!binaryName) {
    throw new Error(`No binary name configured for platform ${platform}`)
  }
  const binaryPath = path.join(installDir, binaryName)

  await ensureExecutable(binaryPath, platform)

  await fs.rm(archivePath, { force: true })

  return binaryPath
}
