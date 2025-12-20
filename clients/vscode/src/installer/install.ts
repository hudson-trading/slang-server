import * as fs from 'fs/promises'
import * as path from 'path'
import decompress from 'decompress'
import * as stream from 'stream'
import { pipeline } from 'stream/promises'
import { getPlatform, Platform } from '../lib/platform'

type GithubAsset = {
  name: string
  browser_download_url: string
}

type GithubRelease = {
  name: string
  tag_name: string
  assets: GithubAsset[]
}

let githubLatestReleaseURL =
  'https://api.github.com/repos/hudson-trading/slang-server/releases/latest'

// set fake url for testing purposes
export function fakeGithubReleaseURL(url: string) {
  githubLatestReleaseURL = url
}

let platform = getPlatform()

// set fake platform for testing purposes
export function fakePlatform(p: Platform) {
  platform = p
}

export async function latestRelease(): Promise<GithubRelease> {
  const timeoutController = new AbortController()
  const timeout = setTimeout(() => {
    timeoutController.abort()
  }, 5000)
  try {
    const response = await fetch(githubLatestReleaseURL, {
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

function chooseReleaseAsset(release: GithubRelease): GithubAsset {
  const assets = release.assets

  if (platform === 'windows') {
    const asset = assets.find((a) => a.name === 'slang-server-windows-x64.zip')
    if (asset) {
      return asset
    }
  }

  if (platform === 'mac') {
    const asset = assets.find((a) => a.name === 'slang-server-macos.tar.gz')
    if (asset) {
      return asset
    }
  }

  if (platform === 'linux') {
    // Prefer gcc, fall back to clang
    const gcc = assets.find((a) => a.name === 'slang-server-linux-x64-gcc.tar.gz')
    if (gcc) {
      return gcc
    }

    const clang = assets.find((a) => a.name === 'slang-server-linux-x64-clang.tar.gz')
    if (clang) {
      return clang
    }
  }

  throw new Error(`No compatible slang-server release found for ${platform}`)
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

async function findBinary(root: string, name: string): Promise<string> {
  const entries = await fs.readdir(root, { withFileTypes: true })
  for (const e of entries) {
    const p = path.join(root, e.name)
    if (e.isFile() && e.name === name) {
      return p
    }
    if (e.isDirectory()) {
      try {
        return await findBinary(p, name)
      } catch {
        // ignore
      }
    }
  }
  throw new Error(`Failed to find ${name} in extracted archive`)
}

async function ensureExecutable(binPath: string) {
  if (platform !== 'windows') {
    await fs.chmod(binPath, 0o755)
  }
}

export async function installLatestSlang(storagePath: string) {
  const release = await latestRelease()
  const asset = chooseReleaseAsset(release)

  const downloadDir = path.join(storagePath, 'download')
  const installDir = path.join(storagePath, 'install', release.tag_name)
  const archivePath = path.join(downloadDir, asset.name)

  await download(asset.browser_download_url, archivePath)

  await extractArchive(archivePath, installDir)

  const binaryName = platform === 'windows' ? 'slang-server.exe' : 'slang-server'

  const binaryPath = await findBinary(installDir, binaryName)
  await ensureExecutable(binaryPath)

  await fs.rm(archivePath, { force: true })

  return binaryPath
}
