import tape from 'tape'
import { chooseInstalledBinary, isUpdateAvailable } from '../../src/lib/install'

tape('chooseInstalledBinary: selects the newest managed release', (assert) => {
  const binary = chooseInstalledBinary(
    ['v0.2.1/slang-server', 'v0.3.0/slang-server', 'notes.txt'],
    'slang-server'
  )
  assert.equal(binary, 'v0.3.0/slang-server')
  assert.end()
})

tape('isUpdateAvailable: returns true when update available', (assert) => {
  const needsUpdate = isUpdateAvailable('v0.3.0', '0.2.1')
  assert.equal(needsUpdate, true, 'update is available')
  assert.end()
})

tape('isUpdateAvailable: returns false when up to date', (assert) => {
  const needsUpdate = isUpdateAvailable('v0.2.1', '0.2.1')
  assert.equal(needsUpdate, false, 'no update needed')
  assert.end()
})

tape('isUpdateAvailable: returns true when installed version older', (assert) => {
  const needsUpdate = isUpdateAvailable('v1.0.0', '0.9.0')
  assert.equal(needsUpdate, true, 'update available for older version')
  assert.end()
})
