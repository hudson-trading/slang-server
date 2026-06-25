import * as path from 'path'
import * as vscode from 'vscode'

import { ViewComponent } from '../lib/libconfig'
import { ProjectComponent, RootItem } from './ProjectComponent'
import * as slang from '../SlangInterface'
import * as vv from '../vaporview-api'

const projectByView = new WeakMap<TopLevelModulesView, ProjectComponent>()

type TopLevelTreeItem = TopLevelModuleItem | TopLevelWaveformItem

type PersistedTopLevelWaveforms = {
  version: 1
  activeWaveform?: {
    topKey: string
    uri: string
  }
  waveformsByTop: Record<string, string[]>
}

class TopLevelWaveformItem {
  readonly contextValue = 'TopLevelWaveform'

  constructor(
    public readonly resourceUri: vscode.Uri,
    public readonly topKey: string
  ) {}

  getTreeItem(): vscode.TreeItem {
    const filePath = this.resourceUri.fsPath
    const item = new vscode.TreeItem(path.basename(filePath), vscode.TreeItemCollapsibleState.None)

    item.description = path.dirname(filePath)
    item.tooltip = filePath
    item.contextValue = this.contextValue
    item.resourceUri = this.resourceUri
    item.iconPath = new vscode.ThemeIcon('graph-line')
    item.command = {
      title: 'Open Waveform',
      command: 'slang.project.topLevels.openWaveform',
      arguments: [this],
    }

    return item
  }
}

class TopLevelModuleItem {
  readonly key = this.top.getPath()

  constructor(
    public readonly top: RootItem,

    /// Whether this module is the active top level module.
    private readonly selected: boolean,
    private readonly hasWaveforms: boolean
  ) {}

  getTreeItem(): vscode.TreeItem {
    const item = new vscode.TreeItem(
      this.top.inst.instName,
      this.hasWaveforms
        ? vscode.TreeItemCollapsibleState.Expanded
        : vscode.TreeItemCollapsibleState.None
    )

    item.description = this.top.inst.declName
    item.tooltip = this.key
    item.iconPath = new vscode.ThemeIcon('chip')
    item.contextValue = this.hasWaveforms ? 'TopLevelModuleWithWaveforms' : 'TopLevelModule'
    item.command = {
      title: 'Select Top Level',
      command: 'slang.project.topLevels.select',
      arguments: [this],
    }

    return item
  }
}

export class TopLevelModulesView
  extends ViewComponent
  implements vscode.TreeDataProvider<TopLevelTreeItem>
{
  private readonly _onDidChangeTreeData = new vscode.EventEmitter<void>()
  readonly onDidChangeTreeData = this._onDidChangeTreeData.event

  treeView: vscode.TreeView<TopLevelTreeItem> | undefined

  private readonly waveformsByTop = new Map<string, TopLevelWaveformItem[]>()
  private activeWaveform: TopLevelWaveformItem | undefined
  private extensionContext: vscode.ExtensionContext | undefined

  constructor(project: ProjectComponent) {
    super({ name: 'Top Level' })
    projectByView.set(this, project)
  }

  private get project(): ProjectComponent {
    const project = projectByView.get(this)

    if (!project) {
      throw new Error('TopLevelModulesView project reference was not initialized')
    }

    return project
  }

  private get storageKey(): string {
    return `${this.configPath ?? 'slang.project.topLevels'}.waveforms`
  }

  private get roots(): RootItem[] {
    return (
      this.project.unit?.children.filter(
        (item): item is RootItem => item.inst.kind === slang.SlangKind.Instance
      ) ?? []
    )
  }

  private getTopKey(top: RootItem): string {
    return top.getPath()
  }

  private getTopKeyForPath(path: string): string | undefined {
    return path.replace(/^TOP\./, '').split('.')[0]
  }

  private getTopForKey(topKey: string): RootItem | undefined {
    return this.roots.find((top) => this.getTopKey(top) === topKey)
  }

  private makeTopItem(top: RootItem): TopLevelModuleItem {
    const key = this.getTopKey(top)

    return new TopLevelModuleItem(
      top,
      top === this.project.top,
      (this.waveformsByTop.get(key)?.length ?? 0) > 0
    )
  }

  private async selectTop(top: RootItem): Promise<void> {
    await this.project.selectRootTopLevel(top, {
      revealHierarchy: true,
      revealFile: false,
      revealInstance: true,
      focus: 'hierarchy',
    })
  }

  private async openFile(uri: vscode.Uri): Promise<boolean> {
    try {
      await vv.commands.openFile({ uri })
      return true
    } catch (error) {
      vscode.window.showErrorMessage('Failed to open waveform: ' + error)
      return false
    }
  }

  private loadWaveforms(): void {
    const stored = this.extensionContext?.workspaceState.get<PersistedTopLevelWaveforms>(
      this.storageKey
    )

    if (!stored || stored.version !== 1) {
      return
    }

    this.waveformsByTop.clear()
    this.activeWaveform = undefined

    for (const [topKey, uriStrings] of Object.entries(stored.waveformsByTop)) {
      const waveforms = uriStrings.map(
        (uriString) => new TopLevelWaveformItem(vscode.Uri.parse(uriString), topKey)
      )

      if (waveforms.length > 0) {
        this.waveformsByTop.set(topKey, waveforms)
      }
    }

    if (stored.activeWaveform) {
      this.activeWaveform = this.waveformsByTop
        .get(stored.activeWaveform.topKey)
        ?.find((waveform) => waveform.resourceUri.toString() === stored.activeWaveform?.uri)
    }

    this.refresh()
  }

  private saveWaveforms(): void {
    const waveformsByTop: Record<string, string[]> = {}

    for (const [topKey, waveforms] of this.waveformsByTop.entries()) {
      waveformsByTop[topKey] = waveforms.map((waveform) => waveform.resourceUri.toString())
    }

    const stored: PersistedTopLevelWaveforms = {
      version: 1,
      activeWaveform: this.activeWaveform
        ? {
            topKey: this.activeWaveform.topKey,
            uri: this.activeWaveform.resourceUri.toString(),
          }
        : undefined,
      waveformsByTop,
    }

    void this.extensionContext?.workspaceState.update(this.storageKey, stored)
  }

  private addWaveform(top: RootItem, uri: vscode.Uri): TopLevelWaveformItem {
    const topKey = this.getTopKey(top)
    const waveforms = this.waveformsByTop.get(topKey) ?? []

    let waveform = waveforms.find((existing) => existing.resourceUri.fsPath === uri.fsPath)

    if (!waveform) {
      waveform = new TopLevelWaveformItem(uri, topKey)
      waveforms.push(waveform)
      this.waveformsByTop.set(topKey, waveforms)
    }

    this.activeWaveform ??= waveform
    this.saveWaveforms()
    this.refresh()

    return waveform
  }

  private async setActiveWaveform(waveform: TopLevelWaveformItem | undefined): Promise<void> {
    if (!waveform) {
      return
    }

    this.activeWaveform = waveform
    this.saveWaveforms()

    const top = this.getTopForKey(waveform.topKey)

    if (top) {
      await this.selectTop(top)
    }

    this.refresh()
  }

  public getActiveWaveformForTopKey(topKey: string): vscode.Uri | undefined {
    return this.activeWaveform?.topKey === topKey ? this.activeWaveform.resourceUri : undefined
  }

  public getActiveWaveformForPath(path: string): vscode.Uri | undefined {
    const topKey = this.getTopKeyForPath(path)
    return topKey ? this.getActiveWaveformForTopKey(topKey) : undefined
  }

  public async openActiveWaveformForPath(path: string): Promise<boolean> {
    const uri = this.getActiveWaveformForPath(path)
    return uri ? this.openFile(uri) : false
  }

  async activate(context: vscode.ExtensionContext): Promise<void> {
    this.extensionContext = context

    this.treeView = vscode.window.createTreeView(this.configPath!, {
      treeDataProvider: this,
      showCollapseAll: false,
      canSelectMany: false,
      dragAndDropController: undefined,
      manageCheckboxStateManually: false,
    })

    this.loadWaveforms()

    context.subscriptions.push(
      this.treeView,
      this.treeView.onDidChangeSelection(async ({ selection }) => {
        await this.handleSelection(selection[0])
      }),
      vscode.commands.registerCommand(this.configPath! + '.select', (item: TopLevelModuleItem) =>
        this.selectTopLevel(item)
      ),
      vscode.commands.registerCommand(this.configPath! + '.attachWaveform', (...args: unknown[]) =>
        this.attachWaveform(
          args.find((arg): arg is TopLevelModuleItem => arg instanceof TopLevelModuleItem),
          args.find((arg): arg is vscode.Uri => arg instanceof vscode.Uri)
        )
      ),
      vscode.commands.registerCommand(
        this.configPath! + '.openWaveform',
        (item: TopLevelWaveformItem) => this.openWaveform(item)
      ),
      vscode.commands.registerCommand(
        this.configPath! + '.openActiveWaveformForPath',
        async (path: string) => {
          const opened = await this.openActiveWaveformForPath(path)

          if (!opened) {
            vscode.window.showInformationMessage('No waveform has been selected for this top level')
          }
        }
      ),
      vscode.commands.registerCommand(
        this.configPath! + '.detachWaveform',
        (item: TopLevelWaveformItem) => this.detachWaveform(item)
      )
    )
  }

  private async handleSelection(selected: TopLevelTreeItem | undefined): Promise<void> {
    if (selected instanceof TopLevelWaveformItem) {
      await this.openWaveform(selected)
    } else if (selected instanceof TopLevelModuleItem) {
      await this.selectTopLevel(selected)
    }
  }

  private async selectTopLevel(item: TopLevelModuleItem): Promise<void> {
    await this.selectTop(item.top)
  }

  private async openWaveform(item: TopLevelWaveformItem): Promise<void> {
    if (await this.openFile(item.resourceUri)) {
      await this.setActiveWaveform(item)
    }
  }

  private async attachWaveform(
    topItem?: TopLevelModuleItem,
    uriFromExplorer?: vscode.Uri
  ): Promise<void> {
    const top = topItem?.top ?? this.project.top

    if (!top) {
      vscode.window.showErrorMessage('Select a top level before attaching a waveform')
      return
    }

    const uri = uriFromExplorer ?? (await this.pickWaveformFile())

    if (!uri || !(await this.openFile(uri))) {
      return
    }

    const waveform = this.addWaveform(top, uri)
    await this.setActiveWaveform(waveform)

    await this.treeView?.reveal(waveform, {
      select: true,
      focus: false,
      expand: false,
    })
  }

  private detachWaveform(item: TopLevelWaveformItem): void {
    const waveforms = this.waveformsByTop.get(item.topKey)

    if (!waveforms) {
      return
    }

    const remaining = waveforms.filter((waveform) => waveform !== item)

    if (remaining.length > 0) {
      this.waveformsByTop.set(item.topKey, remaining)
    } else {
      this.waveformsByTop.delete(item.topKey)
    }

    if (this.activeWaveform === item) {
      this.activeWaveform = remaining[0]
    }

    this.saveWaveforms()
    this.refresh()
  }

  private async pickWaveformFile(): Promise<vscode.Uri | undefined> {
    return (
      await vscode.window.showOpenDialog({
        canSelectFiles: true,
        canSelectFolders: false,
        canSelectMany: false,
        filters: {
          'Waveform files': ['vcd', 'fst', 'ghw', 'fsdb'],
        },
      })
    )?.[0]
  }

  refresh(): void {
    this._onDidChangeTreeData.fire()
  }

  getTreeItem(element: TopLevelTreeItem): vscode.TreeItem {
    return element.getTreeItem()
  }

  async getChildren(element?: TopLevelTreeItem): Promise<TopLevelTreeItem[]> {
    if (element instanceof TopLevelModuleItem) {
      return this.waveformsByTop.get(element.key) ?? []
    }

    if (element instanceof TopLevelWaveformItem) {
      return []
    }

    return this.roots.map((top) => this.makeTopItem(top))
  }

  getParent(element: TopLevelTreeItem): TopLevelTreeItem | undefined {
    if (!(element instanceof TopLevelWaveformItem)) {
      return undefined
    }

    const top = this.getTopForKey(element.topKey)
    return top ? this.makeTopItem(top) : undefined
  }
}
