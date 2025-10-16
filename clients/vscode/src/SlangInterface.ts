import * as vscode from 'vscode'

import { ConfigSchema } from './config.gen'
// enum class SlangKind {
//   InstanceKind,
//   ScopeKind,
//   ParamKind,
//   LogicKind,
//   WireKind,
// };

export { ConfigSchema as Config }

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
////////////////////////////////////////////////////////////
/// server -> client is in commands in the project component
////////////////////////////////////////////////////////////
