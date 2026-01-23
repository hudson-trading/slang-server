import tape from 'tape'
import { isUpdateAvailable } from '../../src/lib/install'

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
