import tape from 'tape'
import * as path from 'path'
import * as fs from 'fs'
import * as tmp from 'tmp-promise'

import { withFakeGitHub } from './harness'
import { fakeGithubReleaseURL, fakePlatform, installLatestSlang } from '../../src/installer/install'

const assetsRoot = path.resolve(__dirname, 'assets')

tape('install: linux gcc', async (assert) => {
  await tmp.withDir(async (dir) => {
    fakePlatform('linux')
    fakeGithubReleaseURL('http://127.0.0.1:9999/release.json')

    await withFakeGitHub(assetsRoot, async () => {
      const binPath = await installLatestSlang(dir.path)

      assert.true(fs.existsSync(binPath), 'binary exists')
      assert.ok(binPath.endsWith('slang-server'), 'correct binary name')
    })
  })

  assert.end()
})
