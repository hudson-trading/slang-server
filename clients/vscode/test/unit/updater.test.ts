import tape from 'tape'
import * as path from 'path'
import * as fs from 'fs'
import * as tmp from 'tmp-promise'

import { withFakeGitHub } from './harness'
import { fakeGithubReleaseURL, latestRelease } from '../../src/installer/install'
import { checkForSlangUpdate, getInstalledVersion } from '../../src/installer/updater'

tape('update available', async (assert) => {
  if (process.platform === 'win32') {
    return
  }

  const dir = await tmp.dir({ unsafeCleanup: false })

  try {
    const bin = path.resolve(process.cwd(), 'test/unit/assets/fake-linux/slang-server')
    fs.chmodSync(bin, 0o755)

    await fs.promises.copyFile(
      path.resolve(process.cwd(), 'test/unit/assets/release.json'),
      path.join(dir.path, 'release.json')
    )

    fakeGithubReleaseURL('http://127.0.0.1:9999/release.json')

    await withFakeGitHub(dir.path, async () => {
      const latest = await latestRelease()
      assert.equal(latest.tag_name, 'v0.2.1', 'parsed latest release tag')

      const installedVersion = await getInstalledVersion(bin)
      const needsUpdate = await checkForSlangUpdate(latest.tag_name, installedVersion)

      console.log({ installedVersion, needsUpdate })

      assert.equal(needsUpdate, true, 'update is available')
    })
  } finally {
    await new Promise((r) => setImmediate(r))
    await fs.promises.rm(dir.path, { recursive: true, force: true })
  }

  assert.end()
})
