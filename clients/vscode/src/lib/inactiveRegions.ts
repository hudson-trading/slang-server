import * as vscodelc from 'vscode-languageclient/node'

export interface InactiveRegionsParams {
  uri: string
  regions: vscodelc.Range[]
}

type SlangExperimentalCapabilities = {
  inactiveRegions?: {
    inactiveRegions: boolean
  }
}

export class ExperimentalCapabilitiesFeature implements vscodelc.StaticFeature {
  fillClientCapabilities(capabilities: vscodelc.ClientCapabilities): void {
    if (!capabilities.experimental) {
      capabilities.experimental = {}
    }

    const exp = capabilities.experimental as SlangExperimentalCapabilities

    exp.inactiveRegions = {
      inactiveRegions: true,
    }
  }

  initialize(
    _capabilities: vscodelc.ServerCapabilities,
    _documentSelector: vscodelc.DocumentSelector | undefined
  ) {}

  dispose() {}

  getState(): vscodelc.FeatureState {
    return { kind: 'static' }
  }

  clear() {}
}
