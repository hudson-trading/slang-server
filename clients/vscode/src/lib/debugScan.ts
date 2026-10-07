import type * as vscodelc from 'vscode-languageclient/node'

export const debugScanCommand = 'slang.debug.scanDocument'

/** Tracks developer scan support across server starts, stops, and automatic restarts. */
export class DebugScanFeature implements vscodelc.StaticFeature {
  available = false

  constructor(private readonly onAvailabilityChanged: (available: boolean) => void) {}

  fillClientCapabilities(_capabilities: vscodelc.ClientCapabilities): void {}

  initialize(capabilities: vscodelc.ServerCapabilities): void {
    this.available =
      capabilities.executeCommandProvider?.commands.includes(debugScanCommand) ?? false
    this.onAvailabilityChanged(this.available)
  }

  getState(): vscodelc.FeatureState {
    return { kind: 'static' }
  }

  clear(): void {
    this.available = false
    this.onAvailabilityChanged(false)
  }
}
