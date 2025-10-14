/**
 * Vaporview API TypeScript Functions
 *
 * Async wrapper functions for the Vaporview waveform viewer VSCode extension API
 * Based on: https://github.com/Lramseyer/vaporview/blob/main/API_DOCS.md
 */

import * as vscode from 'vscode'
import { setTimeout } from 'timers'
import { URLSearchParams } from 'url'

/**
 * Opens a waveform dump file with Vaporview
 */
export async function openFile(args: {
  uri: vscode.Uri
  loadAll?: boolean
  maxSignals?: number
}): Promise<void> {
  await vscode.commands.executeCommand('vaporview.openFile', args)
  // Subsequent commands may fail if executed too quickly :(
  await new Promise((r) => setTimeout(r, 200))
}

/**
 * Variable identifier - must specify at least one of:
 * 1. netlistId
 * 2. instancePath
 * 3. scopePath AND name
 */
export interface VariableIdentifier {
  uri?: vscode.Uri
  netlistId?: number
  instancePath?: string
  scopePath?: string
  name?: string
  msb?: number
  lsb?: number
}

/**
 * Add a variable to the waveform viewer
 */
export async function addVariable(args: VariableIdentifier): Promise<void> {
  await vscode.commands.executeCommand('waveformViewer.addVariable', args)
}

/**
 * Remove a variable from the waveform viewer
 */
export async function removeVariable(args: VariableIdentifier): Promise<void> {
  await vscode.commands.executeCommand('waveformViewer.removeVariable', args)
}

/**
 * Reveal a variable or scope in the netlist view
 */
export async function revealVariableInNetlistView(args: VariableIdentifier): Promise<void> {
  await vscode.commands.executeCommand('waveformViewer.revealVariableInNetlistView', args)
}

export type TimeUnit = 'fs' | 'ps' | 'ns' | 'us' | 'µs' | 'ms' | 's' | 'ks'
export type MarkerType = 0 | 1 // 0: Main Marker, 1: Alt Marker

/**
 * Set the marker or alt marker to a specific time in the viewer
 */
export async function setMarker(args: {
  uri?: vscode.Uri
  time: number
  units?: TimeUnit
  markerType?: MarkerType
}): Promise<void> {
  await vscode.commands.executeCommand('waveformViewer.setMarker', args)
}

/**
 * Get a list of open waveform viewer documents
 */
export async function getOpenDocuments(): Promise<{
  documents: vscode.Uri[]
  lastActiveDocument: vscode.Uri
}> {
  return await vscode.commands.executeCommand('waveformViewer.getOpenDocuments')
}

/**
 * Get the viewer state/settings in the same schema as the save file
 */
export async function getViewerState(uri?: vscode.Uri): Promise<ViewerSettings> {
  return await vscode.commands.executeCommand('waveformViewer.getViewerState', { uri })
}

/**
 * Get signal values at a specific time
 *
 * If time is not specified, uses the marker time for the document
 */
export async function getValuesAtTime(args: {
  uri?: vscode.Uri
  time?: number
  instancePaths: string[]
}): Promise<
  Array<{
    instancePath: string
    value: any[] // [current value] or [previous value, current value]
  }>
> {
  return await vscode.commands.executeCommand('waveformViewer.getValuesAtTime', args)
}

/**
 * Add a signal value link that emits a custom command when clicked
 */
export async function addSignalValueLink(
  args: VariableIdentifier & {
    command: string
  }
): Promise<void> {
  await vscode.commands.executeCommand('waveformViewer.addSignalValueLink', args)
}

let vaporviewExtension: vscode.Extension<VaporViewAPI> | null = null
let onLoad: ((api: VaporViewAPI) => Promise<void>) | null = null

export async function getVaporviewExtension(): Promise<vscode.Extension<VaporViewAPI> | undefined> {
  if (vaporviewExtension) {
    return vaporviewExtension
  }
  const vvExt = vscode.extensions.getExtension('lramseyer.vaporview')

  if (!vvExt) {
    const installCmd = vscode.Uri.parse('vscode:extension/Lramseyer.vaporview')
    vscode.commands.executeCommand('vscode.open', installCmd)
    vscode.window.showInformationMessage(
      'Vaporview extension not found, please install for waveform features.'
    )
    return undefined
  }

  if (!vvExt.isActive) {
    await vvExt.activate()
  }

  vaporviewExtension = vvExt
  if (onLoad !== null) {
    void onLoad(vvExt.exports)
  }
  return vvExt
}

// Use this to register vaporview subscriptions
export async function onExtensionActivated(
  func: (api: VaporViewAPI) => Promise<void>
): Promise<void> {
  onLoad = func
}

export interface ViewerSettings {
  extensionVersion: string | undefined
  fileName: string
  markerTime: number | null
  altMarkerTime: number | null
  selectedSignal: {
    name: string
    numberFormat: string | undefined
    msb: number | undefined
    lsb: number | undefined
  } | null
  zoomRatio: number
  scrollLeft: number
  displayedSignals: any[]
}

/**
 * The Vaporview Subscriptions API, from the repo
 */
export interface markerSetEvent {
  uri: string
  time: number
  units: string
}

export type EventSource = 'netlistView' | 'viewer'

export interface signalEvent {
  uri: string
  instancePath: string
  netlistId: number
  source: EventSource
}

export interface viewerDropEvent {
  uri: string
  resourceUriList: vscode.Uri[]
  groupPath: string[]
  index: number
}

export interface NetlistTreeItemData {
  collapsibleState: vscode.TreeItemCollapsibleState
  label: string
  _onDidChangeCheckboxState?: { A: number }
  numberFormat?: 'hexadecimal' | 'decimal' | 'binary' | 'octal'
  fsdbVarLoaded?: boolean
  resourceUri?: vscode.Uri
  type: 'module' | 'wire' | 'reg' | 'logic' | string
  encoding?: 'none' | string
  width: number
  signalId: number
  netlistId: number
  name: string
  scopePath: string // this plus name is the full path
  msb: number
  lsb: number
  scopeOffsetIdx: number
  children: NetlistTreeItemData[]
  tooltip?: string
  contextValue?: string
  iconPath?: vscode.ThemeIcon
}

export interface VaporViewAPI {
  onDidSetMarker: vscode.Event<markerSetEvent>
  onDidSelectSignal: vscode.Event<signalEvent>
  onDidAddVariable: vscode.Event<signalEvent>
  onDidRemoveVariable: vscode.Event<signalEvent>
  onDidDropInWaveformViewer: vscode.Event<viewerDropEvent>
}

/**
 * Message received from Vaporview webview events
 */
export interface SignalItemData {
  preventDefaultContextMenuItems: boolean
  webviewSelection: boolean
  uri: {
    $mid: number
    path: string
    scheme: string
  }
  webviewSection: string
  scopePath: string
  signalName: string
  type: string
  width: number
  commandValid: boolean
  netlistId: number
  isAnalog: boolean
  webview: string
}

export interface DecodedNetlistUri {
  fsPath: string
  path: string
  scopeId?: string
  id?: number
}

export function decodeNetlistUri(uri: vscode.Uri): DecodedNetlistUri {
  if (uri.scheme !== 'waveform') {
    throw new Error('Not a waveform URI')
  }
  const path = uri.path
  const query = uri.fragment
  const params = new URLSearchParams(query)
  const result: DecodedNetlistUri = { fsPath: path, path: '' }

  const scope = params.get('scope')
  const net = params.get('net')
  const id = params.get('id')

  if (scope) {
    result.scopeId = decodeURIComponent(scope)
  }
  if (net) {
    result.path = decodeURIComponent(net)
  }
  if (id) {
    result.id = parseInt(id)
  }
  return result
}
