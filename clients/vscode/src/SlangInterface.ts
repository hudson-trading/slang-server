import * as vscode from 'vscode'

import { Config } from './config.gen'

export type { Config }

export interface SlangClientInfo {
  name: string
  version: string
}

export type ExperimentalCapabilities = {
  inactiveRegions?: {
    inactiveRegions: boolean
  }
  slangClient?: SlangClientInfo
}

export enum SlangKind {
  Instance = 'Instance',
  Scope = 'Scope',
  ScopeArray = 'ScopeArray',
  InterfacePort = 'InterfacePort',
  InterfacePortArray = 'InterfacePortArray',
  Param = 'Param',
  Logic = 'Logic',
  Port = 'Port',
  InstanceArray = 'InstanceArray',
  Package = 'Package',
}

export enum SlangInstKind {
  Module = 'Module',
  Interface = 'Interface',
  Program = 'Program',
  Package = 'Package',
}

export interface Item {
  kind: SlangKind
  instName: string
  fromExpansion?: boolean
}

export interface Var extends Item {
  type: string
  value: string | undefined
}

// scopes are the only items that can have children
export interface Scope extends Item {
  // For interface ports
  type?: string
  children: Item[]
}

export interface Instance extends Item {
  declName: string
  declKind: SlangInstKind
  // True when the instance body has at least one hierarchy member.
  hasChildren: boolean
  // May or may not be filled
  children: Item[]
}

export interface QualifiedInstance {
  instPath: string
}

export interface Module {
  declName: string
  instCount: number
  inst?: QualifiedInstance
}

export enum InteractionSource {
  // Selection came from the editor or another command that should keep the editor in place.
  Editor = 'editor',
  // Selection came from a codelens action that should keep the editor in place.
  CodeLensSelect = 'codeLensSelect',
  // Selection came from a codelens action that should navigate to the instantiation.
  CodeLensGotoInstantiation = 'codeLensGotoInstantiation',
  // Selection came from the hierarchy tree.
  Hierarchy = 'hierarchy',
  // Selection came from the modules tree.
  Modules = 'modules',
  // Selection came from a terminal link.
  Terminal = 'terminal',
  // Selection came from waveform navigation.
  Waveform = 'waveform',
  // Selection came from the waveform netlist, which should not reveal the hierarchy view.
  WaveformNetlist = 'waveformNetlist',
}

export namespace InteractionSource {
  export function isFromEditor(source: InteractionSource | undefined): boolean {
    return (
      source === InteractionSource.Editor ||
      source === InteractionSource.CodeLensSelect ||
      source === InteractionSource.CodeLensGotoInstantiation
    )
  }

  export function isFromSidebar(source: InteractionSource | undefined): boolean {
    return source === InteractionSource.Hierarchy || source === InteractionSource.Modules
  }

  export function isFromWaveform(source: InteractionSource | undefined): boolean {
    return source === InteractionSource.Waveform || source === InteractionSource.WaveformNetlist
  }
}

export interface ActivateInstanceParams {
  hierPath: string
  interactionSource: InteractionSource
}

export interface ScopeStep {
  path: string
  children: Item[]
}

export interface HierarchySearchItem {
  name: string
  path: string
  kind: SlangKind
  description?: string
  containerName?: string
}

export interface HierarchySearchResult {
  totalResults: number
  matches: HierarchySearchItem[]
}

////////////////////////////////////////////////////////////
/// client -> server
////////////////////////////////////////////////////////////

/// Can be used by right clicking on a module or with button
/// Slang should automatically choose the module with no references
export async function setTopLevel(uri: string) {
  return await vscode.commands.executeCommand('slang.setTopLevel', uri)
}

/// May need to ask to select top level from the available modules
/// Or we can force the user to select the top level first, then specify the build file
export async function setBuildFile(uri: string) {
  return await vscode.commands.executeCommand('slang.setBuildFile', uri)
}

/// Get children at this path. Will return filled Instances for the unit level
export async function getScope(hierPath: string): Promise<Item[]> {
  const children: Item[] = await vscode.commands.executeCommand('slang.getScope', hierPath)
  if (children === undefined) {
    vscode.window.showErrorMessage('Failed to get children for ' + hierPath)
    return []
  }
  return children
}

export async function getUnit(): Promise<Instance[]> {
  const children = (await getScope('')) as Instance[]
  return children
}

export async function getScopesByModule(): Promise<Module[]> {
  return (await vscode.commands.executeCommand('slang.getScopesByModule')) ?? []
}

export async function getInstancesOfModule(declName: string): Promise<QualifiedInstance[]> {
  return (await vscode.commands.executeCommand('slang.getInstancesOfModule', declName)) ?? []
}

/// Query root-to-focus scope steps, with child lists for each hierarchy segment.
export async function getScopes(hierPath: string): Promise<ScopeStep[]> {
  const scopes: ScopeStep[] = await vscode.commands.executeCommand('slang.getScopes', hierPath)
  if (scopes === undefined) {
    vscode.window.showErrorMessage('Failed to get scope chain for ' + hierPath)
    return []
  }
  return scopes
}

export async function searchHierarchy(query: string): Promise<HierarchySearchResult | undefined> {
  return await vscode.commands.executeCommand('slang.searchHierarchy', query)
}

// Get the instances for the module in this file (by editor position)
export async function getInstances(
  document: vscode.TextDocument,
  position: vscode.Position
): Promise<string[]> {
  const instances: string[] = await vscode.commands.executeCommand('slang.getInstances', {
    textDocument: { uri: document.uri.toString() },
    position,
  })
  return instances ?? []
}

// Server resolves the path (canonical or port-side alias) and walks up to set active
// instances and generate scopes for every module in the chain.
export async function setActiveInstance(hierPath: string): Promise<boolean> {
  return await vscode.commands.executeCommand('slang.setActiveInstance', hierPath)
}

export async function getActiveInstance(
  moduleName: string
): Promise<QualifiedInstance | undefined> {
  const instance: QualifiedInstance | undefined = await vscode.commands.executeCommand(
    'slang.getActiveInstance',
    moduleName
  )
  return instance
}

export async function getFilesContainingModule(moduleName: string): Promise<string[]> {
  return await vscode.commands.executeCommand('slang.getFilesContainingModule', moduleName)
}

export async function getModulesInFile(fsPath: string): Promise<string[]> {
  return await vscode.commands.executeCommand('slang.getModulesInFile', fsPath)
}

/// A single endpoint of a driver/load cone: the RTL path of the driver/load and
/// where it appears in source.
export interface ConeEntry {
  path: string
  location: Location
}

/// Cone tracing: Get the drivers of a given RTL path, each with its RTL path and source location.
export async function getDriversWithLocation(hierPath: string): Promise<ConeEntry[] | undefined> {
  return await vscode.commands.executeCommand('slang.getDriversWithLocation', hierPath)
}

/// Cone tracing: Get the loads of a given RTL path, each with its RTL path and source location.
export async function getLoadsWithLocation(hierPath: string): Promise<ConeEntry[] | undefined> {
  return await vscode.commands.executeCommand('slang.getLoadsWithLocation', hierPath)
}

interface ExpandMacroArgs {
  dst: string
  src: string
}
export async function expandMacros(args: ExpandMacroArgs): Promise<boolean> {
  return await vscode.commands.executeCommand('slang.expandMacros', args)
}

// Server-side show: the LSP language client handles file://-vs-vscode-remote:// URI
// translation, so remote workspaces work correctly without client-side conversion.
export async function showHierLocation(hierPath: string, takeFocus: boolean): Promise<void> {
  await vscode.commands.executeCommand('slang.showHierLocation', { hierPath, takeFocus })
}

export async function openModuleDefinition(moduleName: string): Promise<void> {
  await vscode.commands.executeCommand('slang.openModuleDefinition', moduleName)
}

////////////////////////////////////////////////////////////
/// server -> client is in commands in the project component
////////////////////////////////////////////////////////////
