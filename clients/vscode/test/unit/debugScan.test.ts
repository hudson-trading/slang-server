import tape from 'tape'
import { DebugScanFeature, debugScanCommand } from '../../src/lib/debugScan'

tape('Debug scan availability follows server capabilities across restarts', (assert) => {
  const changes: boolean[] = []
  const feature = new DebugScanFeature((available) => changes.push(available))
  assert.equal(feature.available, false)
  feature.initialize({ executeCommandProvider: { commands: [debugScanCommand] } })
  assert.equal(feature.available, true)
  feature.clear()
  assert.equal(feature.available, false)
  feature.initialize({ executeCommandProvider: { commands: ['slang.autoConfigure'] } })
  assert.equal(feature.available, false)
  feature.initialize({})
  assert.deepEqual(changes, [true, false, false, false])
  assert.end()
})
