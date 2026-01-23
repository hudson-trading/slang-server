import tape from 'tape'
import * as path from 'path'
import * as fs from 'fs'
import * as tmp from 'tmp-promise'

import { createFakeAssets, withFakeGitHub } from './harness'
import { fakeGithubReleaseURL, fakePlatform, installLatestSlang } from '../../src/installer/install'

tape('install: linux', async (assert) => {
  await tmp.withDir(
    async (dir) => {
      const assetsDir = path.resolve(dir.path, 'assets')
      const installDir = path.resolve(dir.path, 'install')

      await fs.promises.mkdir(assetsDir, { recursive: true })
      await fs.promises.mkdir(installDir, { recursive: true })

      await createFakeAssets(assetsDir, 'linux')

      fakePlatform('linux')
      fakeGithubReleaseURL('http://127.0.0.1:9999/release.json')

      await withFakeGitHub(assetsDir, async () => {
        const binPath = await installLatestSlang(installDir)
        assert.true(fs.existsSync(binPath), 'binary exists')
        assert.ok(binPath.endsWith('slang-server'), 'correct binary name')
      })
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

      fakePlatform('windows')
      fakeGithubReleaseURL('http://127.0.0.1:9999/release.json')

      await withFakeGitHub(assetsDir, async () => {
        const binPath = await installLatestSlang(installDir)
        assert.true(fs.existsSync(binPath), 'binary exists')
        assert.ok(binPath.endsWith('slang-server.exe'), 'correct binary name')
      })
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

      fakePlatform('mac')
      fakeGithubReleaseURL('http://127.0.0.1:9999/release.json')

      await withFakeGitHub(assetsDir, async () => {
        const binPath = await installLatestSlang(installDir)
        assert.true(fs.existsSync(binPath), 'binary exists')
        assert.ok(binPath.endsWith('slang-server'), 'correct binary name')
      })
    },
    { unsafeCleanup: true }
  )

  assert.end()
})
