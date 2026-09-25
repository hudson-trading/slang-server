import tape from 'tape'
import * as sinon from 'sinon'
import * as path from 'path'
import * as fs from 'fs'
import * as tmp from 'tmp-promise'
import { Readable } from 'stream'
import { spawn } from 'child_process'
import { once } from 'events'

import { createFakeAssets } from './harness'
import { chooseReleaseAsset, installFromGithub, GithubInstallerConfig } from '../../src/lib/install'
import { Platform, PlatformMap } from '../../src/lib/platform'

const binaryNames: PlatformMap = {
  windows: 'slang-server.exe',
  linux: 'slang-server',
  mac: 'slang-server',
}

function testConfig(platform: Platform): GithubInstallerConfig {
  return {
    githubRepo: 'test/repo',
    assetNames: {
      windows: 'slang-server-windows-x64.zip',
      linux: 'slang-server-linux-x64.tar.gz',
      mac: 'slang-server-macos.tar.gz',
    },
    platform,
  }
}

function mockRelease(platform: Platform) {
  const assetName = testConfig(platform).assetNames[platform]
  return {
    name: 'v1.0.0',
    tag_name: 'v1.0.0',
    assets: [
      {
        name: assetName,
        browser_download_url: `http://fake/${assetName}`,
      },
    ],
  }
}

tape('install: exact release asset takes precedence', (assert) => {
  const config = testConfig('linux')
  const release = mockRelease('linux')
  release.assets.unshift({
    name: 'slang-server-old-linux-x64-gcc.tar.gz',
    browser_download_url: 'http://fake/fallback',
  })

  assert.equal(
    chooseReleaseAsset(release, config).name,
    'slang-server-linux-x64.tar.gz',
    'uses the configured asset'
  )
  assert.end()
})

tape('install: falls back to a release asset for the same platform and architecture', (assert) => {
  const release = mockRelease('linux')
  release.assets = [
    {
      name: 'slang-server-linux-arm64.tar.gz',
      browser_download_url: 'http://fake/arm64',
    },
    {
      name: 'slang-server-old-linux-x64-gcc.tar.gz',
      browser_download_url: 'http://fake/x64',
    },
  ]

  assert.equal(
    chooseReleaseAsset(release, testConfig('linux')).name,
    'slang-server-old-linux-x64-gcc.tar.gz',
    'does not select another architecture'
  )
  assert.end()
})

tape('install: recognizes platform names used by release assets', (assert) => {
  for (const [platform, fallbackName] of [
    ['windows', 'slang-server-old-windows-x64.zip'],
    ['mac', 'slang-server-old-macos.tar.gz'],
  ] as const) {
    const release = mockRelease(platform)
    release.assets = [{ name: fallbackName, browser_download_url: 'http://fake/fallback' }]
    assert.equal(chooseReleaseAsset(release, testConfig(platform)).name, fallbackName, platform)
  }
  assert.end()
})

function stubFetch(assetsDir: string, platform: Platform) {
  const config = testConfig(platform)
  const assetName = config.assetNames[platform]!
  const assetPath = path.join(assetsDir, assetName)

  return sinon.stub(global, 'fetch').callsFake(async (url: RequestInfo | URL) => {
    const urlStr = url.toString()

    // GitHub API call for release info
    if (urlStr.includes('api.github.com')) {
      return new Response(JSON.stringify(mockRelease(platform)), {
        status: 200,
        headers: { 'Content-Type': 'application/json' },
      })
    }

    // Asset download
    if (urlStr.includes(assetName)) {
      const fileBuffer = await fs.promises.readFile(assetPath)
      const body = Readable.from(fileBuffer) as unknown as ReadableStream
      return new Response(body, {
        status: 200,
        headers: { 'Content-Length': fileBuffer.length.toString() },
      })
    }

    return new Response(null, { status: 404 })
  })
}

tape('install: linux', async (assert) => {
  await tmp.withDir(
    async (dir) => {
      const assetsDir = path.resolve(dir.path, 'assets')
      const installDir = path.resolve(dir.path, 'install')

      await fs.promises.mkdir(assetsDir, { recursive: true })
      await fs.promises.mkdir(installDir, { recursive: true })

      await createFakeAssets(assetsDir, 'linux')

      const fetchStub = stubFetch(assetsDir, 'linux')
      try {
        const messages: string[] = []
        const binPath = await installFromGithub(installDir, testConfig('linux'), binaryNames, {
          info: (message) => messages.push(message),
        })
        assert.true(fs.existsSync(binPath), 'binary exists')
        assert.ok(binPath.endsWith('slang-server'), 'correct binary name')
        const log = messages.join('\n')
        assert.ok(log.includes('v1.0.0'), 'logs the selected release')
        assert.ok(log.includes(mockRelease('linux').assets[0].browser_download_url), 'logs the URL')
        assert.ok(log.includes(binPath), 'logs the installation path')
      } finally {
        fetchStub.restore()
      }
    },
    { unsafeCleanup: true }
  )

  assert.end()
})

tape('install: failed downloads report HTTP status and release context', async (assert) => {
  await tmp.withDir(
    async (dir) => {
      const messages: string[] = []
      const fetchStub = sinon.stub(global, 'fetch')
      fetchStub.onFirstCall().resolves(new Response(JSON.stringify(mockRelease('linux'))))
      fetchStub
        .onSecondCall()
        .resolves(new Response(null, { status: 502, statusText: 'Bad Gateway' }))
      try {
        await installFromGithub(dir.path, testConfig('linux'), binaryNames, {
          info: (message) => messages.push(message),
        })
        assert.fail('must reject a failed download')
      } catch (error) {
        assert.ok(error instanceof Error)
        assert.match(String(error), /HTTP 502 Bad Gateway/, 'includes the HTTP failure')
        const log = messages.join('\n')
        assert.ok(log.includes('v1.0.0'), 'identifies the release being installed')
        assert.ok(
          log.includes(mockRelease('linux').assets[0].browser_download_url),
          'identifies the failed URL'
        )
        assert.ok(log.includes('HTTP 502 Bad Gateway'), 'logs the HTTP failure')
        assert.notOk(log.includes('Extracting'), 'shows that failure happened before extraction')
      } finally {
        fetchStub.restore()
      }
    },
    { unsafeCleanup: true }
  )

  assert.end()
})

tape('install: windows', async (assert) => {
  await tmp.withDir(
    async (dir) => {
      const assetsDir = path.resolve(dir.path, 'assets')
      const installDir = path.resolve(dir.path, 'install')

      await fs.promises.mkdir(assetsDir, { recursive: true })
      await fs.promises.mkdir(installDir, { recursive: true })

      await createFakeAssets(assetsDir, 'windows')

      const fetchStub = stubFetch(assetsDir, 'windows')
      try {
        const binPath = await installFromGithub(dir.path, testConfig('windows'), binaryNames)
        assert.true(fs.existsSync(binPath), 'binary exists')
        assert.ok(binPath.endsWith('slang-server.exe'), 'correct binary name')
      } finally {
        fetchStub.restore()
      }
    },
    { unsafeCleanup: true }
  )

  assert.end()
})

tape('install: mac', async (assert) => {
  await tmp.withDir(
    async (dir) => {
      const assetsDir = path.resolve(dir.path, 'assets')
      const installDir = path.resolve(dir.path, 'install')

      await fs.promises.mkdir(assetsDir, { recursive: true })
      await fs.promises.mkdir(installDir, { recursive: true })

      await createFakeAssets(assetsDir, 'mac')

      const fetchStub = stubFetch(assetsDir, 'mac')
      try {
        const binPath = await installFromGithub(installDir, testConfig('mac'), binaryNames)
        assert.true(fs.existsSync(binPath), 'binary exists')
        assert.ok(binPath.endsWith('slang-server'), 'correct binary name')
      } finally {
        fetchStub.restore()
      }
    },
    { unsafeCleanup: true }
  )

  assert.end()
})

tape('install: replaces a running Linux binary without overwriting it', async (assert) => {
  if (process.platform !== 'linux') {
    assert.skip('ETXTBSY is specific to Linux executable replacement')
    assert.end()
    return
  }

  await tmp.withDir(
    async (dir) => {
      const assetsDir = path.join(dir.path, 'assets')
      await createFakeAssets(assetsDir, 'linux')

      const binaryPath = path.join(dir.path, 'install', 'v1.0.0', 'slang-server')
      await fs.promises.mkdir(path.dirname(binaryPath), { recursive: true })
      // A native executable stays busy while running; a shell script does not.
      await fs.promises.copyFile(process.execPath, binaryPath)
      await fs.promises.chmod(binaryPath, 0o755)
      const original = await fs.promises.stat(binaryPath)
      const child = spawn(binaryPath, ['-e', 'setInterval(() => {}, 1000)'], { stdio: 'ignore' })
      const exited = once(child, 'exit')
      const fetchStub = stubFetch(assetsDir, 'linux')
      try {
        await once(child, 'spawn')
        const installed = await installFromGithub(dir.path, testConfig('linux'), binaryNames)
        assert.equal(installed, binaryPath, 'keeps the managed installation path')
        assert.notEqual((await fs.promises.stat(installed)).ino, original.ino, 'replaces the inode')
        assert.equal(await fs.promises.readFile(installed, 'utf8'), '#!/bin/sh\necho slang\n')
        assert.equal(child.exitCode, null, 'the original process is still running')
        assert.equal(child.signalCode, null, 'the original process was not stopped')
      } finally {
        fetchStub.restore()
        child.kill()
        await exited
      }
    },
    { unsafeCleanup: true }
  )

  assert.end()
})

tape(
  'install: a release without the binary preserves the existing installation',
  async (assert) => {
    await tmp.withDir(
      async (dir) => {
        const assetsDir = path.join(dir.path, 'assets')
        await createFakeAssets(assetsDir, 'linux')
        const fetchStub = stubFetch(assetsDir, 'linux')
        try {
          const binaryPath = await installFromGithub(dir.path, testConfig('linux'), binaryNames)
          const original = await fs.promises.readFile(binaryPath)

          // A valid archive that does not contain the configured binary.
          await createFakeAssets(assetsDir, 'windows')
          await fs.promises.copyFile(
            path.join(assetsDir, 'slang-server-windows-x64.zip'),
            path.join(assetsDir, testConfig('linux').assetNames.linux)
          )
          try {
            await installFromGithub(dir.path, testConfig('linux'), binaryNames)
            assert.fail('must reject an archive without the binary')
          } catch {
            assert.pass('rejects an archive without the binary')
          }
          assert.deepEqual(await fs.promises.readFile(binaryPath), original, 'keeps the old binary')
          assert.deepEqual(
            await fs.promises.readdir(path.join(dir.path, 'download')),
            [],
            'cleans up'
          )
        } finally {
          fetchStub.restore()
        }
      },
      { unsafeCleanup: true }
    )

    assert.end()
  }
)
