import * as vscode from 'vscode'
import * as vscodelc from 'vscode-languageclient/node'
import { ExperimentalCapabilities } from '../SlangInterface'
import { ConfigObject, ExtensionComponent } from './libconfig'

interface InactiveRegionsParams {
  uri: string
  regions: vscodelc.Range[]
}

/// Taken from vscode-clangd with slight modifications.
/// See license at:https://github.com/clangd/vscode-clangd/blob/master/LICENSE
export class InactiveRegionsFeature extends ExtensionComponent implements vscodelc.StaticFeature {
  style: ConfigObject<string> = new ConfigObject({
    default: 'opacity',
    description: 'How to visually indicate inactive preprocessor regions.',
    enum: ['opacity', 'background', 'none'],
  })

  opacity: ConfigObject<number> = new ConfigObject({
    default: 0.55,
    description: 'Opacity of inactive regions (used only when style is "opacity").',
  })

  backgroundColor: ConfigObject<string> = new ConfigObject({
    default: '#1212124C',
    description: 'Background color for inactive regions (used only when style is "background").',
  })

  private decorationType?: vscode.TextEditorDecorationType
  private files: Map<string, vscode.Range[]> = new Map()

  async activate(context: vscode.ExtensionContext): Promise<void> {
    this.updateDecorationType()

    context.subscriptions.push(
      this.onConfigUpdated(() => {
        this.updateDecorationType()
        this.reapplyAllHighlights()
      })
    )

    context.subscriptions.push(
      vscode.window.onDidChangeVisibleTextEditors((editors) =>
        editors.forEach((e) => this.applyHighlights(e.document.uri.toString()))
      )
    )
  }

  private updateDecorationType() {
    this.decorationType?.dispose()
    this.decorationType = undefined

    const style = this.style.getValue()
    if (style === 'background') {
      this.decorationType = vscode.window.createTextEditorDecorationType({
        isWholeLine: true,
        backgroundColor: this.backgroundColor.getValue(),
      })
    } else if (style === 'opacity') {
      this.decorationType = vscode.window.createTextEditorDecorationType({
        isWholeLine: true,
        opacity: this.opacity.getValue().toString(),
      })
    }
  }

  private reapplyAllHighlights() {
    vscode.window.visibleTextEditors.forEach((e) => {
      this.applyHighlights(e.document.uri.toString())
    })
  }

  fillClientCapabilities(capabilities: vscodelc.ClientCapabilities): void {
    if (!capabilities.experimental) {
      capabilities.experimental = {}
    }

    const exp = capabilities.experimental as ExperimentalCapabilities

    exp.inactiveRegions = {
      inactiveRegions: true,
    }
  }

  register(client: vscodelc.LanguageClient): void {
    this.files.clear()
    client.onNotification('textDocument/inactiveRegions', (params: InactiveRegionsParams) => {
      const fileUri = client.protocol2CodeConverter.asUri(params.uri).toString()
      const ranges: vscode.Range[] = params.regions.map((r) =>
        client.protocol2CodeConverter.asRange(r)
      )
      this.files.set(fileUri, ranges)
      this.applyHighlights(fileUri)
    })
  }

  private applyHighlights(fileUri: string) {
    const ranges = this.files.get(fileUri)
    if (!ranges) return
    vscode.window.visibleTextEditors.forEach((e) => {
      if (!this.decorationType) return
      if (e.document.uri.toString() !== fileUri) return
      e.setDecorations(this.decorationType, ranges)
    })
  }

  initialize(
    _capabilities: vscodelc.ServerCapabilities,
    _documentSelector: vscodelc.DocumentSelector | undefined
  ) {}

  dispose() {
    this.decorationType?.dispose()
    this.files.clear()
  }

  getState(): vscodelc.FeatureState {
    return { kind: 'static' }
  }

  clear() {
    this.files.clear()
  }
}
