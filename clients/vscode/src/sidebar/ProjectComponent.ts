import * as child_process from 'child_process'
import { promises as fs } from 'fs'
import * as path from 'path'
import * as vscode from 'vscode'
import { TreeDataProvider, TreeItem } from 'vscode'
import { ext } from '../extension'
import {
  CommandNode,
  EditorButton,
  TreeItemButton,
  ViewButton,
  ViewComponent,
  WebviewButton,
} from '../lib/libconfig'
import * as slang from '../SlangInterface'
import { InteractionSource } from '../SlangInterface'
import * as vv from '../vaporview-api'
import { getBasename, getIcons, getWorkspaceFolder, isAnyVerilog } from '../utils'
import { Logger } from '../lib/logger'
import { glob } from 'glob'
import { parseArgsStringToArgv } from 'string-argv'
import {
  NetlistTreeItemData,
  NetlistVariableWebviewContext,
  ViewerState,
} from '../vaporview-api/types'
import {
  BuildCommandArgs,
  BuildFileParams,
  createBuildSelectionItems,
  formatString,
  getGeneratedBuildOutputPath,
  resolveCommandToken,
} from './BuildConfigUtils'
import {
  findInstancePaths,
  resolveHierarchyChild,
  splitHierarchyPath,
} from '../lib/InstancePathUtils'

const STRUCTURE_SYMS = [
  slang.SlangKind.Instance,
  slang.SlangKind.InstanceArray,
  slang.SlangKind.Scope,
  slang.SlangKind.ScopeArray,
]

const DATA_SYMS = [
  slang.SlangKind.Port,
  slang.SlangKind.Logic,
  slang.SlangKind.InterfacePort,
  slang.SlangKind.InterfacePortArray,
]

interface HasChildren {
  getChildren(): Promise<HierItem[]>
  getChild(name: string): Promise<HierItem | undefined>
  getPath(): string
}

interface HierarchyQuickPickItem extends vscode.QuickPickItem {
  path: string
}

interface RefreshOptions {
  revealSelection?: boolean
  preserveFocusedPath?: boolean
}

type CompilationSource =
  | { type: 'none' }
  | { type: 'filelist'; buildfile: string }
  | { type: 'commandBuild'; buildfile: string; args: BuildCommandArgs }
  | { type: 'topfile'; topFile: vscode.Uri }

export abstract class HierItem implements HasChildren {
  async showChildrenInWaveform(logger: Logger): Promise<void> {
    const signals = (await this.getChildren()).filter(
      (c) => c instanceof VarItem && c.inst.kind !== slang.SlangKind.Param
    )
    if (signals.length === 0) {
      vscode.window.showInformationMessage('No data signals to add in waveform')
      return
    }
    await Promise.all(
      signals.map((signal) =>
        vv.commands.addVariable({
          instancePath: signal.getPath(),
        })
      )
    )
    logger.info(`Added ${signals.length} signals to waveform`)
  }
  getPath(): string {
    if (this.path !== undefined) {
      return this.path
    }
    return this.parent!.getChildPath(this.inst.instName)
  }

  getChildPath(name: string): string {
    return this.getPath() + '.' + name
  }
  // the symbol to get children from
  path: string | undefined

  // Behind a macro
  fromExpansion: boolean
  inst: slang.Item

  parent: HierItem | undefined
  children: HierItem[] | undefined
  childrenByName: Map<string, HierItem> = new Map()

  constructor(parent: HierItem | undefined, item: slang.Item) {
    this.parent = parent
    this.inst = item
    this.fromExpansion = item.fromExpansion ?? false
  }
  async getChildren(): Promise<HierItem[]> {
    if (this.children === undefined) {
      this.children = await this._fetchChildren()
    }
    return this.children
  }

  async getChild(name: string): Promise<HierItem | undefined> {
    if (this.children === undefined) {
      this.children = await this._fetchChildren()
    }
    if (this.childrenByName.size === 0) {
      for (let child of this.children) {
        this.childrenByName.set(child.inst.instName, child)
      }
    }

    return this.childrenByName.get(name)
  }

  async _fetchChildren(): Promise<HierItem[]> {
    return []
  }

  setChildren(children: HierItem[]) {
    this.children = children
    this.childrenByName = new Map()
    for (const child of children) {
      this.childrenByName.set(child.inst.instName, child)
    }
  }

  async getTreeItem(): Promise<TreeItem> {
    let item = new TreeItem(this.inst.instName)
    item.iconPath = new vscode.ThemeIcon('chip')
    return item
  }

  async preOrderTraversal(fn: (item: HierItem) => void) {
    fn(this)
    for (let child of await this.getChildren()) {
      await child.preOrderTraversal(fn)
    }
  }

  // Return the module containing this item
  getModule(): InstanceItem | undefined {
    let current: HierItem | undefined = this
    while (current !== undefined) {
      if (current instanceof InstanceItem) {
        return current
      }
      current = current.parent
    }
    return undefined
  }

  async hasChildren(): Promise<boolean> {
    return false
  }
}

function mapChildren(parent: HierItem, items: slang.Item[]): HierItem[] {
  const res = []
  for (let item of items) {
    switch (item.kind) {
      case slang.SlangKind.Instance:
        res.push(new InstanceItem(parent, item as slang.Instance))
        break
      case slang.SlangKind.InstanceArray:
        res.push(new InstanceArrayItem(parent, item as slang.Instance))
        break
      case slang.SlangKind.Param:
      case slang.SlangKind.Port:
      case slang.SlangKind.Logic:
        res.push(new VarItem(parent, item as slang.Var))
        break
      case slang.SlangKind.ScopeArray:
      case slang.SlangKind.InterfacePortArray:
        res.push(new ScopeArrayItem(parent, item as slang.Scope))
        break
      case slang.SlangKind.Scope:
      case slang.SlangKind.InterfacePort:
        res.push(new ScopeItem(parent, item as slang.Scope))
        break
      default:
        vscode.window.showErrorMessage('Unknown item kind: ' + item.kind)
    }
  }
  return res
}

async function getSlangChildren(parent: HierItem, path: string): Promise<HierItem[]> {
  let items = await slang.getScope(path)
  return mapChildren(parent, items)
}

class ScopeItem extends HierItem {
  // scopes come with children populated
  inst: slang.Scope

  constructor(parent: HierItem | undefined, inst: slang.Scope) {
    super(parent, inst)
    this.inst = inst
  }

  async _fetchChildren(): Promise<HierItem[]> {
    return mapChildren(this, this.inst.children)
  }

  async hasChildren(): Promise<boolean> {
    return this.inst.children.length > 0
  }

  async getTreeItem(): Promise<TreeItem> {
    let item = new TreeItem(this.inst.instName)
    item.iconPath =
      this.inst.kind === slang.SlangKind.InterfacePort ||
      this.inst.kind === slang.SlangKind.InterfacePortArray
        ? new vscode.ThemeIcon('symbol-interface')
        : new vscode.ThemeIcon('symbol-namespace')
    item.description = this.inst.type ?? ''
    return item
  }
}

class ScopeArrayItem extends ScopeItem {
  getChildPath(name: string): string {
    return this.getPath() + name
  }
}

export class InstanceItem extends HierItem {
  // Used by instances tree view as well
  inst: slang.Instance
  children: HierItem[] | undefined
  constructor(parent: HierItem | undefined, inst: slang.Instance) {
    super(parent, inst)
    this.inst = inst
  }

  async getTreeItem(): Promise<vscode.TreeItem> {
    let item = await super.getTreeItem()
    item.contextValue = 'Module'
    item.iconPath = new vscode.ThemeIcon(
      this.inst.declKind === slang.SlangInstKind.Interface ? 'symbol-class' : 'chip'
    )
    item.description = this.inst.declName
    return item
  }

  async _fetchChildren(): Promise<HierItem[]> {
    return await getSlangChildren(this, this.getPath())
  }

  async hasChildren(): Promise<boolean> {
    // Use the server-provided flag to avoid a getScope round-trip; only fetch if we
    // already happen to have the children cached.
    if (this.children !== undefined) {
      return this.children.length > 0
    }
    return this.inst.hasChildren
  }
}

class InstanceArrayItem extends InstanceItem {
  children: InstanceItem[] | undefined

  async _fetchChildren(): Promise<HierItem[]> {
    return mapChildren(this, this.inst.children)
  }

  async hasChildren(): Promise<boolean> {
    return true
  }

  getChildPath(name: string): string {
    return this.getPath() + name
  }
}

export class RootItem extends InstanceItem {
  getPath(): string {
    return this.inst.instName
  }
  preOrderTraversal = HierItem.prototype.preOrderTraversal
}

export class TopItem extends RootItem {
  constructor(instance: slang.Instance) {
    super(undefined, instance)
    this.children = mapChildren(this, instance.children)
  }
}

export class PkgItem extends RootItem {
  constructor(instance: slang.Instance) {
    super(undefined, instance)
  }

  getChildPath(name: string): string {
    return this.getPath() + '::' + name
  }

  async getTreeItem(): Promise<vscode.TreeItem> {
    let item = await super.getTreeItem()
    item.iconPath = new vscode.ThemeIcon('package')
    return item
  }
}
// Packages don't have children loaded

export class UnitItem implements HasChildren {
  children: RootItem[]
  childMap: Map<string, RootItem> = new Map()
  constructor(children: RootItem[]) {
    this.children = children
    for (let child of children) {
      this.childMap.set(child.inst.instName, child)
    }
  }

  async getChildren(): Promise<HierItem[]> {
    return this.children
  }

  async getChild(name: string): Promise<HierItem | undefined> {
    return this.childMap.get(name)
  }

  getPath(): string {
    return '$unit'
  }
}

class VarItem extends HierItem {
  inst: slang.Var
  static PARAM_TYPES: slang.SlangKind[] = [slang.SlangKind.Param]

  constructor(parent: HierItem | undefined, instance: slang.Var) {
    super(parent, instance)
    this.inst = instance
  }

  private isInterfacePortChild(): boolean {
    return (
      this.parent?.inst.kind === slang.SlangKind.InterfacePort ||
      this.parent?.inst.kind === slang.SlangKind.InterfacePortArray
    )
  }

  async getTreeItem(): Promise<TreeItem> {
    const item = new TreeItem(this.inst.instName)
    item.iconPath = new vscode.ThemeIcon('symbol-variable')
    item.description = ''
    switch (this.inst.kind) {
      case slang.SlangKind.Param:
        item.iconPath = new vscode.ThemeIcon('symbol-type-parameter')
        break
      case slang.SlangKind.Port:
        item.iconPath = new vscode.ThemeIcon('symbol-interface')
        break
      case slang.SlangKind.Logic:
        item.iconPath = new vscode.ThemeIcon(
          this.isInterfacePortChild() ? 'symbol-interface' : 'symbol-variable'
        )
        break
    }
    // if has value, show value
    if (this.inst.type) {
      item.description += this.inst.type + ' '
    }
    if (this.inst.value) {
      item.description += '= ' + this.inst.value
    }
    return item
  }
}

class InstanceLink extends vscode.TerminalLink {
  path: string
  // If the path's top already matches the top, files will be empty
  files: string[] = []
  constructor(path: string, files: string[], startIndex: number, length: number) {
    super(startIndex, length, 'Open in Hierarchy View')
    this.path = path
    this.files = files
  }
}

export class ProjectComponent
  extends ViewComponent
  implements TreeDataProvider<HierItem>, vscode.TerminalLinkProvider<InstanceLink>
{
  // Top $unit, has top level(s) + packages
  unit: UnitItem | undefined = undefined

  // Top level, if there's a single top. One of unit's children
  top: RootItem | undefined = undefined

  // Current build or top - mutually exclusive
  private compilationSource: CompilationSource = { type: 'none' }
  private activeBuildWatcher: vscode.FileSystemWatcher | undefined

  // Getters for backward compatibility
  get buildfile(): string | undefined {
    return this.compilationSource.type === 'filelist' ||
      this.compilationSource.type === 'commandBuild'
      ? this.compilationSource.buildfile
      : undefined
  }

  get topFile(): vscode.Uri | undefined {
    return this.compilationSource.type === 'topfile' ? this.compilationSource.topFile : undefined
  }

  get buildCommandArgs(): BuildCommandArgs | undefined {
    return this.compilationSource.type === 'commandBuild' ? this.compilationSource.args : undefined
  }

  // Setters to maintain mutual exclusivity
  set buildfile(value: string | undefined) {
    if (value === undefined) {
      this.compilationSource = { type: 'none' }
    } else {
      this.compilationSource = { type: 'filelist', buildfile: value }
    }
  }

  set topFile(value: vscode.Uri | undefined) {
    if (value === undefined) {
      this.compilationSource = { type: 'none' }
    } else {
      this.compilationSource = { type: 'topfile', topFile: value }
    }
  }

  // getModulesInFile is hit whenever a command needs the active editor's module; cache by
  // document version so unchanged files don't re-query the server.
  private modulesInFileCache: Map<string, { version: number; modules: string[] }> = new Map()

  // Hierarchy Tree
  private _onDidChangeTreeData: vscode.EventEmitter<void> = new vscode.EventEmitter<void>()
  readonly onDidChangeTreeData: vscode.Event<void> = this._onDidChangeTreeData.event
  treeView: vscode.TreeView<HierItem> | undefined
  focused: HierItem | undefined = undefined

  //////////////////////////////////////////////////////////////////
  // Editor Buttons
  //////////////////////////////////////////////////////////////////

  setTopLevel: EditorButton = new EditorButton(
    {
      title: 'Set Top Level',
      shortTitle: 'Set Top',
      languages: ['verilog', 'systemverilog'],
      icon: '$(chip)',
    },
    async (uri: vscode.Uri | undefined) => {
      if (uri === undefined) {
        vscode.window.showErrorMessage('Open a verilog document to select top')
        return
      }
      // should also be active text editor
      this.clearBuildCommandTracking()
      this.topFile = uri
      await slang.setTopLevel(uri.fsPath)
      await this.refreshSlangCompilation({ preserveFocusedPath: false })
    }
  )

  selectTopLevel: CommandNode = new CommandNode(
    {
      title: 'Select Top Level',
      shortTitle: 'Select Top',
    },
    async () => {
      // get all open sv and v files
      const files = vscode.workspace.textDocuments
        .filter((doc) => {
          return doc.languageId === 'verilog' || doc.languageId === 'systemverilog'
        })
        .map((doc) => doc.uri)
      if (files.length === 0) {
        vscode.window.showErrorMessage('No .v or .sv files open')
        return
      }
      const selection = await vscode.window.showQuickPick(
        files.map((f) => vscode.workspace.asRelativePath(f)),
        {
          placeHolder: 'Select a top level module',
        }
      )
      if (selection === undefined) {
        return
      }
      const file = vscode.Uri.joinPath(vscode.workspace.workspaceFolders![0].uri, selection)
      await this.setTopLevel.func(vscode.Uri.file(file.fsPath))
    }
  )
  private interactionSource: InteractionSource | undefined

  // If the saved interaction source is undefined, push/pop to this source
  private async withInteractionSource<T>(
    interactionSource: InteractionSource | undefined,
    action: () => Promise<T>
  ): Promise<T> {
    if (!interactionSource || this.interactionSource !== undefined) {
      return await action()
    }

    this.interactionSource = interactionSource
    try {
      return await action()
    } finally {
      this.interactionSource = undefined
    }
  }

  private getInteractionSource(): InteractionSource | undefined {
    return this.interactionSource
  }

  private shouldProcessEditorSync(): boolean {
    return this.getInteractionSource() === undefined
  }

  private isInterfaceInstance(item: HierItem): item is InstanceItem {
    return item instanceof InstanceItem && item.inst.declKind === slang.SlangInstKind.Interface
  }

  private isDataItem(item: HierItem): boolean {
    return this.isInterfaceInstance(item) || DATA_SYMS.includes(item.inst.kind)
  }

  private isCategoryVisible(item: HierItem): boolean {
    if (this.isInterfaceInstance(item)) {
      return this.includeData
    }
    return this.symFilter.has(item.inst.kind)
  }

  // The server emits LSP window/showDocument so the language client handles URI translation
  // (file:// vs vscode-remote://). viewColumn is dropped — showDocument has no equivalent.
  private async revealHierLocation(
    hierPath: string,
    { preserveFocus = false }: { preserveFocus?: boolean; viewColumn?: vscode.ViewColumn } = {}
  ): Promise<void> {
    await slang.showHierLocation(hierPath, !preserveFocus)
  }

  async onStart(): Promise<void> {
    await this.refreshSlangCompilation({ revealSelection: false })
  }

  //////////////////////////////////////////////////////////////////
  // Hierarchy View Buttons
  //////////////////////////////////////////////////////////////////

  clearTopLevel: ViewButton = new ViewButton(
    {
      title: 'Clear Top Level',
      icon: '$(panel-close)',
    },
    async () => {
      this.compilationSource = { type: 'none' }
      this.clearBuildCommandTracking()

      this.unit = undefined
      this.top = undefined
      this.focused = undefined

      this._onDidChangeTreeData.fire()
      await slang.setBuildFile('')
    }
  )

  async reveal(
    item: HierItem | undefined = undefined,
    focus: boolean = false,
    openViewWhenNotVisible: boolean = false
  ) {
    if (item === undefined) {
      if (this.focused === undefined) {
        this._onDidChangeTreeData.fire()
        return
      }
      item = this.focused
    }
    // We may have toggled params off for example, in which case our item is no longer visible
    if (!this.shouldBeVisible(item)) {
      item = item.parent
      this.focused = item
    }
    this._onDidChangeTreeData.fire()
    if (item !== undefined && this.treeView && (openViewWhenNotVisible || this.treeView.visible)) {
      this.logger.info('Revealing in hierarchy: ' + item.getPath())
      await this.treeView.reveal(item, { select: true, focus: focus, expand: true })
    }
  }

  private async showHierarchySearch(): Promise<void> {
    const quickPick = vscode.window.createQuickPick<HierarchyQuickPickItem>()
    quickPick.placeholder = 'Search hierarchy by path'
    quickPick.matchOnDescription = true
    quickPick.matchOnDetail = true

    let closed = false
    let searchInProgress = false
    let pendingQuery: string | undefined
    const applyFilter = async (query: string): Promise<void> => {
      pendingQuery = query
      if (searchInProgress) {
        return
      }

      searchInProgress = true
      quickPick.busy = true
      try {
        while (!closed && pendingQuery !== undefined) {
          const currentQuery = pendingQuery
          pendingQuery = undefined

          let result: slang.HierarchySearchResult | undefined
          try {
            result = await slang.searchHierarchy(currentQuery)
          } catch (error) {
            if (quickPick.value === currentQuery) {
              quickPick.items = []
              quickPick.title = `Search failed: ${String(error)}`
            }
            continue
          }
          if (closed || quickPick.value !== currentQuery) {
            continue
          }

          const matches = result?.matches ?? []
          const totalResults = result?.totalResults ?? 0
          quickPick.items = matches.map((match) => {
            const isSignal =
              match.kind === slang.SlangKind.Logic || match.kind === slang.SlangKind.Port
            return {
              label: match.name,
              description:
                isSignal && match.containerName ? match.containerName : match.description,
              detail:
                isSignal && match.description ? `${match.description} — ${match.path}` : match.path,
              path: match.path,
            }
          })
          if (totalResults === 0) {
            quickPick.title = 'No results found'
          } else if (matches.length !== totalResults) {
            quickPick.title = `Showing ${matches.length} of ${totalResults} results`
          } else {
            quickPick.title = `${totalResults} ${totalResults === 1 ? 'result' : 'results'}`
          }
        }
      } finally {
        searchInProgress = false
        if (!closed) {
          quickPick.busy = false
        }
      }
    }

    let selected: HierarchyQuickPickItem | undefined
    const disposables = [
      quickPick.onDidChangeValue((query) => void applyFilter(query)),
      quickPick.onDidAccept(() => {
        selected = quickPick.selectedItems[0]
        quickPick.hide()
      }),
    ]
    await new Promise<void>((resolve) => {
      disposables.push(
        quickPick.onDidHide(() => {
          closed = true
          resolve()
        })
      )
      quickPick.show()
      void applyFilter(quickPick.value)
    })
    disposables.forEach((disposable) => disposable.dispose())
    quickPick.dispose()

    if (selected) {
      await this.setInstance.func(selected.path)
    }
  }

  fuzzyFindInstance: ViewButton = new ViewButton(
    {
      title: 'Find in Hierarchy',
      icon: '$(search-view-icon)',
      keybind: 'cmd+f',
      keybindContainer: true,
    },
    async (_instance: HierItem | undefined) => {
      await this.showHierarchySearch()
    }
  )

  private async resolveHierarchyPath(path: string): Promise<{
    item: HierItem
    exact: boolean
    missingPart?: string
  } | null> {
    if (!this.unit) {
      return null
    }

    const parts = splitHierarchyPath(path)

    // Replace `current`'s children with server-fresh items, but reuse existing HierItem
    // objects (by name) so vscode's TreeView selection identity survives. Replacing
    // wholesale on every call orphans previously-selected items and the selection
    // visually gets stuck on the prior pick.
    const refreshChildren = (parent: HierItem, items: slang.Item[]) => {
      const existing = new Map<string, HierItem>()
      for (const child of parent.children ?? []) {
        existing.set(child.inst.instName, child)
      }
      const fresh = mapChildren(parent, items)
      for (let i = 0; i < fresh.length; i++) {
        const reuse = existing.get(fresh[i].inst.instName)
        if (reuse) {
          // Keep the same HierItem object; update its slang.Item payload so any new
          // fields (e.g. hasChildren) reflect the latest server snapshot.
          reuse.inst = fresh[i].inst
          reuse.fromExpansion = fresh[i].fromExpansion ?? false
          fresh[i] = reuse
        }
      }
      parent.setChildren(fresh)
    }

    const walkHierarchy = async (scopes?: slang.ScopeStep[]) => {
      let current: HasChildren = this.unit!
      let lastResolved: HierItem | undefined

      for (let i = 0; i < parts.length; i++) {
        if (i > 0 && current instanceof HierItem && scopes) {
          const step = scopes[i]
          if (step) {
            refreshChildren(current, step.children)
          }
        }

        // Normal instances lazily fetch their children from the server. Stop the cache-only
        // pass here; scope and array children are embedded in their parent payloads.
        if (
          !scopes &&
          current instanceof InstanceItem &&
          !(current instanceof InstanceArrayItem) &&
          current.children === undefined
        ) {
          return null
        }

        const childCandidates: Array<{ instName: string; path: string; item: HierItem }> = (
          await current.getChildren()
        ).map((item) => ({
          instName: item.inst.instName,
          path: item.getPath(),
          item,
        }))
        const resolvedChild = resolveHierarchyChild(
          childCandidates,
          parts,
          i,
          lastResolved?.getPath()
        )
        const child = resolvedChild.child?.item
        i = resolvedChild.nextIndex
        if (!child) {
          if (lastResolved) {
            return {
              item: lastResolved,
              exact: false,
              missingPart: parts[i],
            }
          }
          return null
        }

        lastResolved = child
        current = child
      }

      if (lastResolved) {
        const finalStep = scopes?.[parts.length]
        if (finalStep) {
          refreshChildren(lastResolved, finalStep.children)
        }
        return {
          item: lastResolved,
          exact: true,
        }
      }

      return null
    }

    const cached = await walkHierarchy()
    if (cached?.exact) {
      return cached
    }
    return await walkHierarchy(await slang.getScopes(path))
  }

  public async onActiveInstanceChanged(params: slang.ActivateInstanceParams): Promise<void> {
    await this.setInstance.func(params.hierPath, params.interactionSource, true)
  }

  // Set instance given one of:
  // - a path (from internal calls)
  // - a hierarchy item
  // - undefined (let user search the hierarchy)
  setInstance: CommandNode = new CommandNode(
    {
      title: 'Select Instance',
    },
    async (
      instance: HierItem | string | undefined,
      interactionSource?: InteractionSource,
      alreadyActivated = false
    ) =>
      this.withInteractionSource(interactionSource, async () => {
        if (instance === undefined) {
          if (this.unit === undefined) {
            // TODO: have one flow that this leads to- setting top, then specfiying build spec / params
            await vscode.window.showInformationMessage('Please set top level or build file first')
            return
          }

          await this.showHierarchySearch()
          return
        }

        const currentInteractionSource = this.getInteractionSource()
        const fromEditor = InteractionSource.isFromEditor(currentInteractionSource)
        const fromWaveform = InteractionSource.isFromWaveform(currentInteractionSource)
        const preserveEditorFocus =
          currentInteractionSource === undefined ||
          (fromEditor && currentInteractionSource !== InteractionSource.CodeLensGotoInstantiation)
        const viewColumn = fromWaveform ? vscode.ViewColumn.Beside : undefined
        const shouldOpenEditorLocation =
          !alreadyActivated &&
          (!fromEditor || currentInteractionSource === InteractionSource.CodeLensGotoInstantiation)
        let openedEditorPath: string | undefined

        // resolve instances if hierarchy path
        if (typeof instance === 'string') {
          if (this.unit === undefined) {
            // TODO: set the top level based on top name?
            await vscode.window.showErrorMessage(
              'Please set top level or build file first (no $unit)'
            )
            return
          }

          if (shouldOpenEditorLocation) {
            await this.revealHierLocation(instance, {
              preserveFocus: preserveEditorFocus,
              viewColumn,
            })
            openedEditorPath = instance
          }

          const resolved = await this.resolveHierarchyPath(instance)
          this._onDidChangeTreeData.fire()
          if (!resolved) {
            const error = `Could not find instance ${instance}`
            this.logger.warn(error)
            await vscode.window.showErrorMessage(error)
            return
          }
          if (!resolved.exact && resolved.missingPart) {
            const error = `Could not find instance ${resolved.missingPart} in ${resolved.item.getPath()}`
            this.logger.warn(error)
            await vscode.window.showErrorMessage(error)
          }
          instance = resolved.item
        }

        this.focused = instance
        // The tree's getChildren filter hides fromExpansion items, so reveal can't walk
        // the parent chain if any ancestor is macro-expanded. Toggle if any node on the
        // path needs the macro-defined visibility on.
        if (!this.includeMacroDefined) {
          for (let node: HierItem | undefined = instance; node; node = node.parent) {
            if (node.fromExpansion) {
              await this.toggleHiddenFunc()
              break
            }
          }
        }
        if (!this.isCategoryVisible(instance)) {
          if (instance.inst.kind === slang.SlangKind.Param) {
            await this.toggleParamsFunc()
          } else if (this.isDataItem(instance)) {
            await this.toggleDataFunc()
          }
        }

        // Reveal in Hierarchy
        if (
          currentInteractionSource !== InteractionSource.Hierarchy &&
          currentInteractionSource !== InteractionSource.WaveformNetlist
        ) {
          const openHierarchyWhenNotVisible =
            currentInteractionSource === undefined ||
            currentInteractionSource === InteractionSource.Terminal ||
            currentInteractionSource === InteractionSource.Waveform
          await this.reveal(instance, false, openHierarchyWhenNotVisible)
        } else {
          this._onDidChangeTreeData.fire()
        }

        // Set as active
        const selectedModule = instance.getModule()
        const selectedActiveModule =
          selectedModule && selectedModule.inst.declKind !== slang.SlangInstKind.Package
            ? selectedModule
            : undefined

        if (selectedActiveModule && !alreadyActivated) {
          await slang.setActiveInstance(instance.getPath())
        }

        if (shouldOpenEditorLocation && openedEditorPath !== instance.getPath()) {
          await this.revealHierLocation(instance.getPath(), {
            preserveFocus: preserveEditorFocus,
            viewColumn,
          })
        }

        return instance
      })
  )

  public async maybeOpenWaveform(): Promise<boolean> {
    const vvExt = await vv.getApi()

    if (vvExt === undefined) {
      return false
    }

    // check if a waveform is already open
    const docs = await vv.commands.getOpenDocuments()
    if (docs.length > 0) {
      return true
    }

    // try to guess the wave file name
    if (ext.slangConfig.wavesPattern) {
      let fillBlob: BuildFileParams = {}
      if (this.buildfile) {
        const basename = getBasename(this.buildfile)

        if (basename) {
          fillBlob.name = basename
        }
      }

      if (this.top) {
        fillBlob.top = this.top.inst.declName
      }

      const fileGlob = formatString(ext.slangConfig.wavesPattern, fillBlob)

      this.logger.info('Looking for waveform: ' + fileGlob)

      const files = await glob(fileGlob)

      if (files.length > 0) {
        let selected: string | undefined = files[0]
        if (files.length > 1) {
          selected = await vscode.window.showQuickPick(
            files.map((f) => vscode.workspace.asRelativePath(f)),
            { placeHolder: 'Select waveform file to open' }
          )
        }
        if (selected) {
          await vv.commands.openFile({ uri: vscode.Uri.file(selected) })
          return true
        }
      } else {
        this.logger.info('No wavesPattern pattern set in slang config, using glob')
        const files = await glob(
          formatString(ext.slangConfig.wavesPattern, { name: '*', top: '*' })
        )
        const selected = await vscode.window.showQuickPick(
          files.map((f) => vscode.workspace.asRelativePath(f)),
          { placeHolder: 'Select waveform file to open' }
        )
        if (selected) {
          await vv.commands.openFile({ uri: vscode.Uri.file(selected) })
          return true
        }
      }
      return false
    } else {
      return this.openWaveformGeneric()
    }
  }

  public async openWaveformGeneric(): Promise<boolean> {
    const options: vscode.OpenDialogOptions = {
      canSelectFiles: true,
      canSelectFolders: false,
      canSelectMany: false,
      filters: {
        'Wave files': ['vcd', 'fst', 'ghw', 'fsdb'],
      },
    }

    const uris = await vscode.window.showOpenDialog(options)
    if (!uris || uris.length === 0) {
      return false
    }
    const selectedFile = uris[0] // Get the first (and only) selected file
    try {
      await vv.commands.openFile({ uri: selectedFile })
    } catch (error) {
      vscode.window.showErrorMessage('Failed to open waveform: ' + error)
      return false
    }
    return true
  }

  showBuildFile: ViewButton = new ViewButton(
    {
      title: 'Open Build File',
      icon: '$(file)',
      shown: true,
    },
    async () => {
      // open the build file
      if (this.topFile) {
        vscode.window.showInformationMessage('Top level set from file, no build file to open')
        return
      }
      if (!this.buildfile) {
        vscode.window.showErrorMessage('No build file set')
        return
      }
      this.logger.info('Opening build file: ' + this.buildfile)
      const doc = await vscode.workspace.openTextDocument(this.buildfile)
      await vscode.window.showTextDocument(doc)
    }
  )

  selectBuildFile: ViewButton = new ViewButton(
    {
      title: 'Select build file',
      icon: '$(file-directory)',
      shown: true,
    },
    async () => {
      const { items, allGlobs, directBuildCount, commandBuildCount } =
        await createBuildSelectionItems(ext.slangConfig, (globs) => ext.findFiles(globs))
      this.logger.info('Looking for build files: ' + allGlobs.join(', '))
      this.logger.info(
        `Found ${directBuildCount} direct build files and ${commandBuildCount} command builds`
      )

      if (items.length === 0) {
        vscode.window.showErrorMessage(
          `No build files found. See [docs](https://hudson-trading.github.io/slang-server/start/config/#buildpattern) for more info.`
        )
        return
      }

      const selection = await vscode.window.showQuickPick(items, {
        placeHolder: 'Select a build file',
      })
      if (selection === undefined) {
        return
      }

      if (selection.type === 'command' && selection.buildCommand) {
        const ok = await this.setBuildCommandFile(selection.buildCommand)
        if (!ok) {
          return
        }
      } else if (selection.filePath) {
        await this.setDirectBuildFile(selection.filePath)
      }

      await this.refreshSlangCompilation({ preserveFocusedPath: false })
    }
  )

  //////////////////////////////////////////////////////////////////
  // Symbol Filtering
  //////////////////////////////////////////////////////////////////

  symFilter: Set<string> = new Set<string>(STRUCTURE_SYMS)
  // params / localparams (constants)
  includeParams: boolean = false
  // ports / nets / registers / interfaces / interface ports
  includeData: boolean = false

  // symbols hidden behind macros
  includeMacroDefined: boolean = false

  toggleParams: ViewButton = new ViewButton(
    {
      title: 'Toggle Params',
      icon: '$(symbol-type-parameter)',
    },
    async (_item: HierItem | undefined) => {
      await this.toggleParamsFunc()
      await this.reveal()
    }
  )

  async toggleParamsFunc() {
    this.includeParams = !this.includeParams
    if (this.includeParams) {
      for (let type of VarItem.PARAM_TYPES) {
        this.symFilter.add(type)
      }
    } else {
      for (let type of VarItem.PARAM_TYPES) {
        this.symFilter.delete(type)
      }
    }
  }

  toggleData: ViewButton = new ViewButton(
    {
      title: 'Toggle Data',
      icon: '$(symbol-variable)',
    },
    async (_item: HierItem | undefined) => {
      await this.toggleDataFunc()
      await this.reveal()
    }
  )
  async toggleDataFunc() {
    this.includeData = !this.includeData
    if (this.includeData) {
      for (let type of DATA_SYMS) {
        this.symFilter.add(type)
      }
    } else {
      for (let type of DATA_SYMS) {
        this.symFilter.delete(type)
      }
    }
  }

  toggleHidden: ViewButton = new ViewButton(
    {
      title: 'Toggle Macro Defined',
      icon: '$(eye)',
    },
    async (_item: HierItem | undefined) => {
      await this.toggleHiddenFunc()
      await this.reveal()
    }
  )

  async toggleHiddenFunc() {
    this.includeMacroDefined = !this.includeMacroDefined
  }

  refreshCompilation: ViewButton = new ViewButton(
    {
      title: 'Refresh Compilation',
      icon: '$(refresh)',
      shown: true,
    },
    async () => {
      const refreshed = await this.refreshActiveCompilationSource()
      if (!refreshed) {
        vscode.window.showInformationMessage('No build file or top level is currently set')
        return
      }
      await this.refreshSlangCompilation()
    }
  )

  //////////////////////////////////////////////////////////////////
  // Inline Item Buttons
  //////////////////////////////////////////////////////////////////

  showInWaveform: TreeItemButton = new TreeItemButton(
    {
      title: 'Show in Waveform',
      icon: '$(graph-line)',
      keybind: 'w',
    },
    async (item: HierItem | undefined) => {
      if (item === undefined) {
        item = this.focused
      }
      if (item === undefined) {
        vscode.window.showErrorMessage('No instance selected to show in waveform')
        return
      }

      const ok = await this.maybeOpenWaveform()
      if (!ok) {
        return
      }
      if (item instanceof VarItem) {
        this.logger.info('Showing variable in waveform: ' + item.getPath())
        await vv.commands.addVariable({
          instancePath: item.getPath(),
        })
      } else {
        this.logger.info('Showing scope in waveform: ' + item.getPath())
        // toggle if not visible
        let didToggle = false
        if (!this.includeData) {
          await this.toggleDataFunc()
          didToggle = true
        }
        if (!this.includeParams) {
          await this.toggleParamsFunc()
          didToggle = true
        }
        if (didToggle) {
          void this.reveal(item)
        }
        await item.showChildrenInWaveform(this.logger)
      }
    }
  )

  async openBuildFile(params: BuildFileParams = {}) {
    if (!ext.slangConfig.buildPattern) {
      return
    }
    const fmtParams = {
      name: '*',
      top: '*',
    }
    if (params.name) {
      fmtParams.name = params.name
    }
    if (params.top) {
      fmtParams.top = params.top
    }
    let buildPattern = formatString(ext.slangConfig.buildPattern, fmtParams)
    if (params.name) {
      buildPattern = buildPattern.replace('{}', params.name)
    }

    const files = await ext.findFiles([buildPattern])
    if (files.length > 0) {
      if (files.length > 1) {
        const selection = await vscode.window.showQuickPick(
          files.map((f) => vscode.workspace.asRelativePath(f)),
          {
            placeHolder: 'Select Build File',
          }
        )
        if (selection === undefined) {
          return
        }
        this.buildfile = vscode.Uri.joinPath(
          vscode.workspace.workspaceFolders![0].uri,
          selection
        ).fsPath
      } else {
        this.buildfile = files[0].fsPath
      }
      await this.setDirectBuildFile(this.buildfile)
      await this.refreshSlangCompilation({
        revealSelection: false,
        preserveFocusedPath: false,
      })
    } else {
      this.logger.warn('No build files found for pattern: ' + buildPattern)
    }
  }

  // For vaporview
  showInEditorFromNetlist: TreeItemButton = new TreeItemButton(
    {
      title: 'Show in Editor',
      icon: '$(file-code)',
      viewOverride: 'waveformViewerNetlistView',
      keybind: 'e',
    },
    async (item: NetlistTreeItemData | undefined) => {
      if (item === undefined) {
        // The keybind press comes with 'undefined', but
        // - the selected netlist signals are not in the extension state (should add this)
        // - we should be able to get the selected one from onDidSelectSignal subscription, but that doesn't seem to be working
        await vscode.window.showErrorMessage(
          "'e' keybind from netlist view not supported yet; please use the button."
        )
        return
      }
      let fullpath = item.name
      if (item.scopePath) {
        // In some types it's an array, others a string. Why?? who knows
        fullpath = item.scopePath.concat(fullpath).join('.')
      }
      if (this.unit === undefined && ext.slangConfig.buildPattern) {
        const decoded = vv.decodeNetlistUri(item.resourceUri!)
        const basename = getBasename(decoded.fsPath)!
        const top = decoded.scopeId?.split('.')[0] || ''
        await this.openBuildFile({ name: basename, top: top })
      }
      await this.setInstance.func(fullpath, InteractionSource.WaveformNetlist)
    }
  )

  showInEditorFromVaporview: WebviewButton = new WebviewButton(
    {
      title: 'Show in Editor',
      icon: '$(file-code)',
      group: '2_variables@2.1',
      editorId: 'vaporview.waveformViewer',
      webviewSection: 'signal',
      keybind: 'e',
    },
    async (item: NetlistVariableWebviewContext | undefined) => {
      // If we're coming from the keybind, we have to get the focused signal ourselves
      let signalPath = ''
      let vvState: ViewerState | undefined = undefined
      if (item === undefined) {
        vvState = await vv.commands.getViewerState()

        if (vvState === undefined) {
          vscode.window.showErrorMessage(
            'Failed to get Vaporview state; cannot open signal in editor.'
          )
          return
        }

        signalPath = vvState.selectedSignal?.name || ''
      } else {
        signalPath = item.signalName
        if (item.scopePath) {
          signalPath = item.scopePath + signalPath
        }
      }

      if (this.unit === undefined && ext.slangConfig.buildPattern) {
        let basename = ''
        let top = ''
        if (item === undefined) {
          top = vvState!.selectedSignal?.name.split('.')[0] || ''
          basename = getBasename(vvState!.fileName)!
        } else {
          top = item.scopePath.split('.')[0] || ''
          basename = getBasename(item.uri.path)!
        }

        await this.openBuildFile({ name: basename, top: top })
      }

      if (signalPath.length === 0) {
        await vscode.window.showErrorMessage(
          "'e' keybind from netlist view not yet supported; please add to waveform first or use button."
        )
      }
      await this.setInstance.func(signalPath, InteractionSource.Waveform)
    }
  )

  copyHierarchyPath: TreeItemButton = new TreeItemButton(
    {
      title: 'Copy Path',
      viewItems: [],
      isSubmenu: true,
      icon: getIcons('files'),
      keybind: 'cmd+c',
    },
    async (item: HierItem | undefined) => {
      if (item === undefined) {
        if (this.focused === undefined) {
          this.logger.warn('No instance focused to copy path from')
          return
        }
        item = this.focused
      }
      vscode.env.clipboard.writeText(item.getPath())
    }
  )

  constructor() {
    super({
      name: 'Hierarchy',
      welcome: {
        contents:
          '[Select Build File](command:slang.project.selectBuildFile)\n[Select Top Level](command:slang.project.selectTopLevel)',
      },
    })
  }

  private async getModulesInFileCached(document: vscode.TextDocument): Promise<string[]> {
    const key = document.uri.toString()
    const cached = this.modulesInFileCache.get(key)
    if (cached && cached.version === document.version) {
      return cached.modules
    }
    const modules = await slang.getModulesInFile(document.uri.fsPath)
    this.modulesInFileCache.set(key, { version: document.version, modules })
    return modules
  }

  private async getActiveEditorModuleName(
    editor: vscode.TextEditor | undefined = vscode.window.activeTextEditor
  ): Promise<string | undefined> {
    if (!editor || !this.unit || !isAnyVerilog(editor.document.languageId)) {
      return undefined
    }

    const modules = await this.getModulesInFileCached(editor.document)
    if (modules.length === 1) {
      return modules[0]
    }

    const focusedModule = this.focused?.getModule()?.inst.declName
    if (focusedModule && modules.includes(focusedModule)) {
      return focusedModule
    }

    return undefined
  }

  private async syncEditorToActiveInstance(
    editor: vscode.TextEditor | undefined = vscode.window.activeTextEditor
  ) {
    if (!editor || !this.unit || !isAnyVerilog(editor.document.languageId)) {
      return
    }

    const moduleName = await this.getActiveEditorModuleName(editor)
    if (!moduleName) {
      return
    }

    const selection = editor.selection.active
    const targetPath = await slang.getActiveInstanceAtPosition(
      moduleName,
      editor.document,
      selection
    )
    if (!targetPath) {
      return
    }

    await this.setInstance.func(targetPath, InteractionSource.Editor)
  }

  async provideTerminalLinks(
    context: vscode.TerminalLinkContext,
    _token: vscode.CancellationToken
  ): Promise<InstanceLink[]> {
    let links = []
    for (let match of findInstancePaths(context.line)) {
      this.logger.info('Potential instance path in terminal: ' + match.path)
      const startIndex = match.index
      const path = match.path
      const topModule = path.split('.')[0]

      if (this.unit?.childMap.has(topModule)) {
        links.push(new InstanceLink(path, [], startIndex, path.length))
      } else {
        const topFiles = await slang.getFilesContainingModule(topModule)
        if (topFiles.length > 0) {
          links.push(new InstanceLink(path, topFiles, startIndex, path.length))
        }
      }
    }
    return links
  }

  async handleTerminalLink(link: InstanceLink): Promise<void> {
    if (link.files.length > 0) {
      let file: string = link.files[0]
      if (link.files.length > 1) {
        // Get relative paths
        const selection = await vscode.window.showQuickPick(
          link.files.map((f) => vscode.workspace.asRelativePath(f)),
          {
            placeHolder: 'Select Top Level File',
          }
        )
        if (selection === undefined) {
          return
        }
        // Set top level from the file
        file = vscode.Uri.joinPath(vscode.workspace.workspaceFolders![0].uri, selection).fsPath
      }
      await this.setTopLevel.func(vscode.Uri.file(file))
    }
    await this.setInstance.func(link.path, InteractionSource.Terminal)
  }

  // Stop watching the currently active command-backed build source, if any.
  private clearBuildCommandTracking() {
    this.activeBuildWatcher?.dispose()
    this.activeBuildWatcher = undefined
  }

  // Reapply the active build/top selection, regenerating command-backed .f files when needed.
  private async refreshActiveCompilationSource(): Promise<boolean> {
    if (this.buildCommandArgs) {
      return await this.setBuildCommandFile(this.buildCommandArgs)
    }
    if (this.buildfile) {
      await slang.setBuildFile(this.buildfile)
      return true
    }
    if (this.topFile) {
      await slang.setTopLevel(this.topFile.fsPath)
      return true
    }
    return false
  }

  // Switch to a normal .f file and clear any command-backed build state.
  private async setDirectBuildFile(buildfile: string) {
    this.clearBuildCommandTracking()
    this.buildfile = buildfile
    await slang.setBuildFile(buildfile)
  }

  // Generate a build file from a configured command and make it the active compilation source.
  private async setBuildCommandFile(args: BuildCommandArgs): Promise<boolean> {
    const buildfile = await this.generateBuildCommandFile(args)
    if (!buildfile) {
      return false
    }

    this.compilationSource = { type: 'commandBuild', buildfile, args }
    await slang.setBuildFile(buildfile)
    this.trackBuildCommand(args)
    return true
  }

  // Watch the selected source file so command-generated build files stay in sync with edits.
  private trackBuildCommand(args: BuildCommandArgs) {
    this.clearBuildCommandTracking()

    const sourceDir = path.dirname(args.sourceFile)
    const sourceName = path.basename(args.sourceFile)
    const watcher = vscode.workspace.createFileSystemWatcher(
      new vscode.RelativePattern(vscode.Uri.file(sourceDir), sourceName)
    )

    const rerun = async (uri: vscode.Uri) => {
      const activeArgs = this.buildCommandArgs
      if (
        !activeArgs ||
        activeArgs.sourceFile !== args.sourceFile ||
        activeArgs.selectionIndex !== args.selectionIndex ||
        uri.fsPath !== args.sourceFile
      ) {
        return
      }

      this.logger.info(
        'Command build source updated, regenerating: ' + vscode.workspace.asRelativePath(uri)
      )
      const buildfile = await this.generateBuildCommandFile(args)
      if (!buildfile) {
        return
      }

      this.compilationSource = { type: 'commandBuild', buildfile, args }
      await slang.setBuildFile(buildfile)
      await this.refreshSlangCompilation({ revealSelection: false })
    }

    watcher.onDidChange((uri) => void rerun(uri))
    watcher.onDidCreate((uri) => void rerun(uri))
    watcher.onDidDelete((uri) => void rerun(uri))
    this.activeBuildWatcher = watcher
  }

  // Run the configured command, capture stdout, and materialize it as a local .f file.
  private async generateBuildCommandFile(args: BuildCommandArgs): Promise<string | undefined> {
    const workspaceFolder = getWorkspaceFolder()
    if (!workspaceFolder) {
      vscode.window.showErrorMessage('Cannot generate build: no workspace folder')
      return undefined
    }

    const build = ext.slangConfig.builds?.[args.selectionIndex]
    if (!build) {
      vscode.window.showErrorMessage('Invalid build index')
      return undefined
    }
    if (!build.command) {
      vscode.window.showErrorMessage('Build entry is missing command')
      return undefined
    }

    const command = parseArgsStringToArgv(build.command)
    if (command.length === 0) {
      vscode.window.showErrorMessage('Build entry is missing command')
      return undefined
    }

    const sourceFile = path.isAbsolute(args.sourceFile)
      ? path.normalize(args.sourceFile)
      : path.join(workspaceFolder, args.sourceFile)
    const resolvedCommand = command.map((token) => resolveCommandToken(token, workspaceFolder))
    const executable = resolvedCommand[0]!
    const procArgs = [...resolvedCommand.slice(1), sourceFile]

    const output = await this.runBuildCommand(executable, procArgs, workspaceFolder)
    if (output === undefined) {
      return undefined
    }

    const outPath = getGeneratedBuildOutputPath(
      workspaceFolder,
      sourceFile,
      args.selectionIndex,
      build.name ?? undefined
    )
    await fs.mkdir(path.dirname(outPath), { recursive: true })
    await fs.writeFile(outPath, output, 'utf8')
    this.logger.info('Wrote command-generated build output to ' + outPath)
    return outPath
  }

  // Execute the configured command without a shell so config-provided args stay literal.
  private async runBuildCommand(
    executable: string,
    args: string[],
    cwd: string
  ): Promise<string | undefined> {
    return await new Promise((resolve) => {
      const proc = child_process.spawn(executable, args, {
        cwd,
        shell: false,
        windowsHide: true,
        stdio: ['ignore', 'pipe', 'pipe'],
      })

      let stdout = ''
      let stderr = ''
      proc.stdout.on('data', (chunk: Buffer | string) => {
        stdout += chunk.toString()
      })
      proc.stderr.on('data', (chunk: Buffer | string) => {
        stderr += chunk.toString()
      })
      proc.on('error', (err) => {
        this.logger.error(`Build command failed: ${err.message}`)
        vscode.window.showErrorMessage(`Build command failed: ${err.message}`)
        resolve(undefined)
      })
      proc.on('close', (code) => {
        if (code !== 0) {
          const stderrText = stderr.trim()
          const suffix = stderrText.length > 0 ? `\n${stderrText}` : ''
          this.logger.error(`Build command exited with code ${code}${suffix}`)
          vscode.window.showErrorMessage(`Build command failed.${suffix}`)
          resolve(undefined)
          return
        }

        resolve(stdout)
      })
    })
  }

  async activate(context: vscode.ExtensionContext): Promise<void> {
    vscode.window.createTreeView
    this.treeView = vscode.window.createTreeView(this.configPath!, {
      treeDataProvider: this,
      showCollapseAll: true,
      canSelectMany: false,
      dragAndDropController: undefined,
      manageCheckboxStateManually: false,
    })
    // If you actually register it, you don't get the collapsible state button
    // context.subscriptions.push(vscode.window.registerTreeDataProvider(this.configPath!, this))

    context.subscriptions.push(vscode.window.registerTerminalLinkProvider(this))
    context.subscriptions.push({ dispose: () => this.clearBuildCommandTracking() })
    context.subscriptions.push(
      vscode.window.onDidChangeTextEditorSelection((event) => {
        if (event.kind !== vscode.TextEditorSelectionChangeKind.Command) {
          return
        }
        if (this.shouldProcessEditorSync()) {
          void this.syncEditorToActiveInstance(event.textEditor)
        }
      })
    )
    context.subscriptions.push(
      vscode.workspace.onDidCloseTextDocument((document) => {
        this.modulesInFileCache.delete(document.uri.toString())
      })
    )

    // user updates to buildfile
    context.subscriptions.push(
      vscode.workspace.onDidSaveTextDocument(async (document) => {
        if (document.uri.fsPath === this.buildfile) {
          this.logger.info(
            'Build file updated, reloading: ' + vscode.workspace.asRelativePath(this.buildfile)
          )
          vscode.commands.executeCommand('slang.setBuildFile', this.buildfile)
          await this.refreshSlangCompilation()
          return
        }

        if (this.unit && isAnyVerilog(document.languageId)) {
          this.logger.info(
            'HDL file updated, refreshing hierarchy: ' +
              vscode.workspace.asRelativePath(document.uri)
          )
          await this.refreshSlangCompilation({ revealSelection: false })
        }
      })
    )
  }

  async refreshSlangCompilation({
    revealSelection = true,
    preserveFocusedPath = true,
  }: RefreshOptions = {}) {
    const previousFocusedPath = preserveFocusedPath ? this.focused?.getPath() : undefined
    const unit = await slang.getUnit()
    if (unit.length === 0) {
      this.unit = undefined
      this.top = undefined
      this.focused = undefined
      this._onDidChangeTreeData.fire()
      return
    }
    this.unit = new UnitItem(
      unit.map((item) =>
        item.kind === slang.SlangKind.Instance ? new TopItem(item) : new PkgItem(item)
      )
    )

    const tops = this.unit.children.filter((item) => item.inst.kind === slang.SlangKind.Instance)
    this.top = tops.length === 1 ? tops[0] : undefined
    if (!preserveFocusedPath) {
      this.focused = undefined
    }
    this._onDidChangeTreeData.fire()
    if (previousFocusedPath) {
      const restored = await this.resolveHierarchyPath(previousFocusedPath)
      if (restored) {
        this.focused = restored.item
        if (revealSelection) {
          await this.setInstance.func(restored.item)
        } else {
          this._onDidChangeTreeData.fire()
        }
        return
      }
    }

    if (tops.length === 1 && this.treeView !== undefined && revealSelection) {
      await this.setInstance.func(tops[0])
    }
  }

  private decorateHierarchyTreeItem(item: TreeItem, element: HierItem): TreeItem {
    item.tooltip = element.getPath()
    item.command = {
      title: 'Go to definition',
      command: 'slang.project.setInstance',
      arguments: [element, InteractionSource.Hierarchy],
    }
    return item
  }

  async getTreeItem(element: HierItem): Promise<TreeItem> {
    if (element.inst.kind === slang.SlangKind.Package) {
      const treeItem = await element.getTreeItem()
      treeItem.collapsibleState = vscode.TreeItemCollapsibleState.Collapsed
      return this.decorateHierarchyTreeItem(treeItem, element)
    }

    const treeItem = await element.getTreeItem()
    const expandable = await element.hasChildren()
    if (!expandable) {
      treeItem.collapsibleState = vscode.TreeItemCollapsibleState.None
    } else if (element instanceof RootItem && element.inst.kind === slang.SlangKind.Instance) {
      treeItem.collapsibleState = vscode.TreeItemCollapsibleState.Expanded
    } else {
      treeItem.collapsibleState = vscode.TreeItemCollapsibleState.Collapsed
    }
    return this.decorateHierarchyTreeItem(treeItem, element)
  }

  async getChildren(element?: HierItem | undefined): Promise<HierItem[]> {
    if (element === undefined) {
      if (this.unit === undefined) {
        return []
      }
      return this.unit.getChildren()
    }
    const children = await element.getChildren()
    if (element.inst.kind === slang.SlangKind.Package) {
      // Packages don't have children loaded, but should always have something
      return children
    }
    return children.filter((child) => this.shouldBeVisible(child))
  }

  // doesn't include filtering for package children
  shouldBeVisible(element: HierItem): boolean {
    if (!this.isCategoryVisible(element)) {
      return false
    }
    if (!this.includeMacroDefined && element.fromExpansion) {
      return false
    }
    return true
  }

  getParent(element: HierItem): HierItem | undefined {
    return element.parent
  }

  async resolveTreeItem(
    item: TreeItem,
    element: HierItem,
    _token: vscode.CancellationToken
  ): Promise<TreeItem> {
    return this.decorateHierarchyTreeItem(item, element)
  }
}
