import type * as vscodelc from 'vscode-languageclient/node'
import type { ExperimentalCapabilities, SlangClientInfo } from '../SlangInterface'

export class SlangClientInfoFeature implements vscodelc.StaticFeature {
  constructor(private readonly clientInfo: SlangClientInfo) {}

  fillClientCapabilities(capabilities: vscodelc.ClientCapabilities): void {
    if (!capabilities.experimental) {
      capabilities.experimental = {}
    }

    const exp = capabilities.experimental as ExperimentalCapabilities
    exp.slangClient = this.clientInfo
  }

  initialize(
    _capabilities: vscodelc.ServerCapabilities,
    _documentSelector: vscodelc.DocumentSelector | undefined
  ) {}

  getState(): vscodelc.FeatureState {
    return { kind: 'static' }
  }

  clear() {}
}
