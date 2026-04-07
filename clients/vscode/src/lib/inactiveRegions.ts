import * as vscode from 'vscode'
import * as vscodelc from 'vscode-languageclient/node'

interface InactiveRegionsParams {
  uri: string
  regions: vscodelc.Range[]
}

type SlangExperimentalCapabilities = {
  inactiveRegions?: {
    inactiveRegions: boolean
  }
}

/// Taken from vscode-clangd with slight modifications see license at:
/// https://github.com/clangd/vscode-clangd/blob/master/LICENSE
export class InactiveRegionsFeature implements vscodelc.StaticFeature {
  private readonly decorationType = vscode.window.createTextEditorDecorationType({
    isWholeLine: false,
    opacity: '0.55',
  })
  private files: Map<string, vscode.Range[]> = new Map()

  fillClientCapabilities(capabilities: vscodelc.ClientCapabilities): void {
    if (!capabilities.experimental) {
      capabilities.experimental = {}
    }

    const exp = capabilities.experimental as SlangExperimentalCapabilities

    exp.inactiveRegions = {
      inactiveRegions: true,
    }
  }

  register(client: vscodelc.LanguageClient, context: vscode.ExtensionContext): void {
    client.onNotification('textDocument/inactiveRegions', (params: InactiveRegionsParams) => {
      const fileUri = client.protocol2CodeConverter.asUri(params.uri).toString()
      const ranges: vscode.Range[] = params.regions.map((r) =>
        client.protocol2CodeConverter.asRange(r)
      )
      this.files.set(fileUri, ranges)
      this.applyHighlights(fileUri)
    })

    context.subscriptions.push(
      vscode.window.onDidChangeVisibleTextEditors((editors) =>
        editors.forEach((e) => this.applyHighlights(e.document.uri.toString()))
      )
    )
  }

  private applyHighlights(fileUri: string) {
    const ranges = this.files.get(fileUri)
    if (!ranges) return
    vscode.window.visibleTextEditors.forEach((e) => {
      if (e.document.uri.toString() !== fileUri) return
      e.setDecorations(this.decorationType, ranges)
    })
  }

  initialize(
    _capabilities: vscodelc.ServerCapabilities,
    _documentSelector: vscodelc.DocumentSelector | undefined
  ) {}

  dispose() {
    this.decorationType.dispose()
    this.files.clear()
  }

  getState(): vscodelc.FeatureState {
    return { kind: 'static' }
  }

  clear() {}
}
