import tape from 'tape'
import type * as vscodelc from 'vscode-languageclient/node'
import { SlangClientInfoFeature } from '../../src/lib/clientInfo'

tape('SlangClientInfoFeature advertises the extension identity', (assert) => {
  const capabilities: vscodelc.ClientCapabilities = {}
  const feature = new SlangClientInfoFeature({ name: 'vscode-slang', version: '1.2.3' })

  feature.fillClientCapabilities(capabilities)

  assert.deepEqual(capabilities.experimental, {
    slangClient: { name: 'vscode-slang', version: '1.2.3' },
  })
  assert.end()
})
