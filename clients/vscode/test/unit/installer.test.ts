import tape from 'tape'
import * as sinon from 'sinon'
import * as path from 'path'
import * as fs from 'fs'
import * as tmp from 'tmp-promise'
import { Readable } from 'stream'

import { createFakeAssets } from './harness'
import { installFromGithub, GithubInstallerConfig } from '../../src/lib/install'
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
      linux: 'slang-server-linux-x64-gcc.tar.gz',
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
        const binPath = await installFromGithub(installDir, testConfig('linux'), binaryNames)
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
