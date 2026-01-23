import * as child_process from 'child_process'
import * as semver from 'semver'

// Allow tests to replace spawn.
let spawnImpl: typeof child_process.spawn = child_process.spawn
export function fakeSpawn(fn: typeof child_process.spawn) {
  spawnImpl = fn
}
export function resetSpawn() {
  spawnImpl = child_process.spawn
}

async function run(cmd: string, args: string[]): Promise<string> {
  return new Promise((resolve, reject) => {
    const child = spawnImpl(cmd, args, {
      stdio: ['ignore', 'pipe', 'ignore'],
    })

    let out = ''
    child.stdout.on('data', (d) => (out += d.toString()))
    child.on('error', reject)
    child.on('close', () => resolve(out))
  })
}

export async function getInstalledVersion(binaryPath: string): Promise<string | null> {
  try {
    const output = await run(binaryPath, ['--version'])

    const prefix = 'slang-server version '
    const idx = output.indexOf(prefix)
    if (idx === -1) {
      return null
    }

    // Extract the version string
    const raw = output.slice(idx + prefix.length).trim()

    return semver.valid(raw) ?? semver.clean(raw)
  } catch {
    return null
  }
}

function isUpdateAvailable(installed: string | null, latest: string): boolean {
  console.log(`Comparing installed version '${installed}' to latest version '${latest}'`)
  if (!installed) {
    return true
  }
  if (!semver.valid(installed) || !semver.valid(latest)) {
    return true
  }
  return semver.lt(installed, latest)
}

export async function checkForSlangUpdate(
  latestReleaseTag: string,
  installedVersion: string | null
): Promise<boolean> {
  if (!installedVersion) {
    return true
  }

  if (!isUpdateAvailable(installedVersion, latestReleaseTag)) {
    return false
  }

  return true
}
