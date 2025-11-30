import * as vscode from 'vscode'

import { Config } from './config.gen'

export type { Config }

export type ExperimentalCapabilities = {
  inactiveRegions?: {
    inactiveRegions: boolean
  }
}

export enum SlangKind {
  Instance = 'Instance',
  Scope = 'Scope',
  ScopeArray = 'ScopeArray',
  Param = 'Param',
  Logic = 'Logic',
  Port = 'Port',
  InstanceArray = 'InstanceArray',
  Package = 'Package',
}

/// Can't parse URIs
export interface Location {
  /**
   * The resource identifier of this location.
   */
  uri: string

  /**
   * The document range of this location.
   */
  range: vscode.Range
}

export interface Item {
  kind: SlangKind
  instName: string
  instLoc: Location
}

export interface Var extends Item {
  type: string
  value: string | undefined
}

// scopes are the only items that can have children
export interface Scope extends Item {
  children: Item[]
}

export interface Instance extends Item {
  declName: string
  declLoc: Location
  // May or may not be filled
  children: Item[]
}

////////////////////////////////////////////////////////////
// Instances View
////////////////////////////////////////////////////////////

// std::string declName;
// lsp::Location declLoc;
// size_t instanceCount;
// // Will be filled if there's only one
// std::optional<QualifiedInstance> instance;
export interface Module {
  declName: string
  declLoc: Location
  inst?: QualifiedInstance
  instCount: number
}

// When buttons are pressed on these, we call getScopes() to get relevant data
export interface QualifiedInstance {
  instPath: string
  instLoc: Location
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

/// Module -> scopes for instances view
export async function getScopesByModule(): Promise<Module[]> {
  const children: Module[] = await vscode.commands.executeCommand('slang.getScopesByModule')
  if (children === undefined) {
    vscode.window.showErrorMessage('Failed to get modules')
    return []
  }
  return children
}

/// Query a list of scopes going down to this instance. FilledInstance ... Item
export async function getScopes(hierPath: string): Promise<Instance[]> {
  return await vscode.commands.executeCommand('slang.getScopes', hierPath)
}

export async function getInstancesOfModule(declName: string): Promise<QualifiedInstance[]> {
  return await vscode.commands.executeCommand('slang.getInstancesOfModule', declName)
}

export async function getFilesContainingModule(moduleName: string): Promise<string[]> {
  return await vscode.commands.executeCommand('slang.getFilesContainingModule', moduleName)
}

export async function getModulesInFile(fsPath: string): Promise<string[]> {
  return await vscode.commands.executeCommand('slang.getModulesInFile', fsPath)
}

export async function getDrivers(hierPath: string): Promise<string[]> {
  return await vscode.commands.executeCommand('slang.getDrivers', hierPath)
}

export async function getLoads(hierPath: string): Promise<string[]> {
  return await vscode.commands.executeCommand('slang.getLoads', hierPath)
}

/// Cone tracing: Get drivers (incoming calls) of a given RTL path with full location info
/// Returns CallHierarchyIncomingCall array which includes URI, range, and hierPath for navigation
export async function getDriversWithLocation(
  hierPath: string
): Promise<vscode.CallHierarchyIncomingCall[] | undefined> {
  return await vscode.commands.executeCommand(
    'slang.getDriversWithLocation',
    hierPath
  )
}

/// Cone tracing: Get loads (outgoing calls) of a given RTL path with full location info
/// Returns CallHierarchyOutgoingCall array which includes URI, range, and hierPath for navigation
export async function getLoadsWithLocation(
  hierPath: string
): Promise<vscode.CallHierarchyOutgoingCall[] | undefined> {
  return await vscode.commands.executeCommand(
    'slang.getLoadsWithLocation',
    hierPath
  )
}

interface ExpandMacroArgs {
  dst: string
  src: string
}
export async function expandMacros(args: ExpandMacroArgs): Promise<boolean> {
  return await vscode.commands.executeCommand('slang.expandMacros', args)
}
////////////////////////////////////////////////////////////
/// server -> client is in commands in the project component
////////////////////////////////////////////////////////////
