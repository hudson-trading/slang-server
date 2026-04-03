import * as vscode from 'vscode'
import * as vscodelc from 'vscode-languageclient/node'
import { LanguageClient } from 'vscode-languageclient/node'

export interface InactiveRegionsParams {
  textDocument: vscodelc.VersionedTextDocumentIdentifier
  regions: vscodelc.Range[]
}

const InactiveRegionsNotification = new vscodelc.NotificationType<InactiveRegionsParams>(
  'textDocument/inactiveRegions'
)

class InactiveRegionsFeature {
  private decorationType?: vscode.TextEditorDecorationType
  private files: Map<string, vscode.Range[]> = new Map()

  constructor(private client: LanguageClient) {}

  initialize(_capabilities: vscodelc.ServerCapabilities) {
    // const caps = serverCapabilities as vscodelc.ServerCapabilities & {
    //   inactiveRegionsProvider?: boolean
    // }

    // if (!caps.inactiveRegionsProvider) {
    //   return
    // }

    this.decorationType = vscode.window.createTextEditorDecorationType({
      isWholeLine: true,
      opacity: '0.5',
    })

    vscode.window.onDidChangeVisibleTextEditors((editors) => {
      editors.forEach((e) => this.applyHighlights(e.document.uri.toString()))
    })
  }

  handleNotification(params: InactiveRegionsParams) {
    const fileUri = params.textDocument.uri

    const ranges = params.regions.map((r) => this.client.protocol2CodeConverter.asRange(r))

    console.log(`Received ${ranges.length} inactive regions for ${fileUri}`)

    this.files.set(fileUri, ranges)
    this.applyHighlights(fileUri)
  }

  private applyHighlights(fileUri: string) {
    const ranges = this.files.get(fileUri)
    if (!ranges || !this.decorationType) return

    vscode.window.visibleTextEditors.forEach((editor) => {
      if (editor.document.uri.toString() === fileUri) {
        editor.setDecorations(this.decorationType!, ranges)
      }
    })
  }
}

type SlangExperimentalCapabilities = {
  inactiveRegions?: {
    inactiveRegions: boolean
  }
}

export class ExperimentalCapabilitiesFeature implements vscodelc.StaticFeature {
  private inactiveRegions: InactiveRegionsFeature

  constructor(private client: LanguageClient) {
    this.inactiveRegions = new InactiveRegionsFeature(client)
  }

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
    capabilities: vscodelc.ServerCapabilities,
    _documentSelector: vscodelc.DocumentSelector | undefined
  ) {
    this.inactiveRegions.initialize(capabilities)

    this.client.onNotification(
      InactiveRegionsNotification,
      this.inactiveRegions.handleNotification.bind(this.inactiveRegions)
    )
  }

  dispose() {}

  getState(): vscodelc.FeatureState {
    return { kind: 'static' }
  }

  clear() {}
}
