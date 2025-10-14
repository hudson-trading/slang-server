import * as vscode from 'vscode'

import { TreeItemButton, ViewComponent } from '../lib/libconfig'
import * as slang from '../SlangInterface'
import { ext } from '../extension'
import { HierItem } from './ProjectComponent'

export class InstanceViewItem {
  data: slang.QualifiedInstance
  parent: ModuleItem

  constructor(parent: ModuleItem, inst: slang.QualifiedInstance) {
    this.parent = parent
    this.data = inst
  }
  getParent(): ModuleItem {
    return this.parent
  }

  getTreeItem(): vscode.TreeItem {
    const item = new vscode.TreeItem(this.data.instPath, vscode.TreeItemCollapsibleState.None)
    item.iconPath = new vscode.ThemeIcon('chip')
    item.contextValue = 'Instance'
    return item
  }

  resolveTreeItem(item: vscode.TreeItem, _token: vscode.CancellationToken): vscode.TreeItem {
    item.tooltip = this.data.instPath
    item.command = {
      title: 'Open Instance',
      command: 'slang.project.setInstance',
      arguments: [
        this.data.instPath,
        { revealInstance: false, revealFile: true, revealHierarchy: true },
      ],
    }
    return item
  }

  getChildren(): [] {
    return []
  }
}

export class ModuleItem {
  data: slang.Module
  instances: Map<string, InstanceViewItem> = new Map()

  constructor(data: slang.Module) {
    this.data = data
  }

  getTreeItem(): vscode.TreeItem {
    const item = new vscode.TreeItem(
      this.data.declName + ` (${this.data.instCount})`,
      vscode.TreeItemCollapsibleState.Collapsed
    )
    item.iconPath = new vscode.ThemeIcon('file')
    if (this.data.inst) {
      item.collapsibleState = vscode.TreeItemCollapsibleState.Expanded
    }
    return item
  }

  resolveTreeItem(item: vscode.TreeItem, _token: vscode.CancellationToken): vscode.TreeItem {
    item.tooltip = this.data.declName
    item.command = {
      title: 'Open Module',
      command: 'vscode.open',
      arguments: [vscode.Uri.parse(this.data.declLoc.uri), { selection: this.data.declLoc.range }],
    }
    return item
  }

  async getChildren(): Promise<InstanceViewItem[]> {
    return Array.from((await this.getInstances()).values())
  }

  async getInstances(): Promise<Map<string, InstanceViewItem>> {
    if (this.instances.size === 0) {
      const insts = await slang.getInstancesOfModule(this.data.declName)
      this.instances = new Map()
      for (const inst of insts) {
        const item = new InstanceViewItem(this, inst)
        this.instances.set(inst.instPath, item)
      }
    }
    return this.instances
  }
}

type InstanceTreeItem = InstanceViewItem | ModuleItem
export class InstancesView
  extends ViewComponent
  implements vscode.TreeDataProvider<InstanceTreeItem>
{
  copyHierarchyPath: TreeItemButton = new TreeItemButton(
    {
      title: 'Copy Path',
      viewItems: ['Instance'],
      icon: '$(files)',
      isSubmenu: true,
      keybind: 'cmd+c',
    },
    async (item: InstanceViewItem | undefined) => {
      if (item === undefined) {
        // undefined if we're coming from keybind
        if (this.treeView?.selection.length === 0) {
          return
        }
        const genericItem = this.treeView?.selection[0]
        if (genericItem instanceof ModuleItem) {
          vscode.env.clipboard.writeText(genericItem.data.declName)
          return
        } else {
          item = genericItem as InstanceViewItem
        }
      }
      vscode.env.clipboard.writeText(item.data.instPath)
    }
  )

  showInWaveform: TreeItemButton = new TreeItemButton(
    {
      title: 'Show in Waveform',
      viewItems: ['Instance'],
      icon: '$(graph-line)',
      keybind: 'w',
    },
    async (instItem: InstanceViewItem | undefined) => {
      if (instItem === undefined) {
        // undefined if we're coming from keybind
        if (
          !this.treeView ||
          this.treeView.selection.length === 0 ||
          !(this.treeView.selection[0] instanceof InstanceViewItem)
        ) {
          this.logger.warn('No instance selected to show in waveform')
          return
        }
        instItem = this.treeView.selection[0]
      }
      const item: HierItem = await ext.project.setInstance.func(instItem.data.instPath, {
        revealInstance: false,
        revealFile: false,
        revealHierarchy: true,
      })
      const ok = await ext.project.maybeOpenWaveform()
      if (!ok) {
        return
      }
      await item.showChildrenInWaveform(this.logger)
    }
  )

  modules: Map<string, ModuleItem> = new Map()
  async updateModules() {
    const modules = await slang.getScopesByModule()
    this.modules = new Map()
    for (const mod of modules) {
      const item = new ModuleItem(mod)
      this.modules.set(mod.declName, item)
    }

    this._onDidChangeTreeData.fire()
  }

  async clearModules() {
    this.modules = new Map()
    this._onDidChangeTreeData.fire()
  }

  revealPath(module: string, path: string | undefined = undefined, focus: boolean = false) {
    const moduleItem = this.modules.get(module)
    if (moduleItem === undefined) {
      return
    }
    if (path === undefined) {
      this.treeView?.reveal(moduleItem, { select: true, focus: focus, expand: true })
      return
    }
    const inst = moduleItem.instances.get(path)
    if (inst) {
      this.treeView?.reveal(inst, { select: true, focus: focus, expand: true })
    }
  }
  private _onDidChangeTreeData: vscode.EventEmitter<void> = new vscode.EventEmitter<void>()
  readonly onDidChangeTreeData: vscode.Event<void> = this._onDidChangeTreeData.event
  treeView: vscode.TreeView<InstanceTreeItem> | undefined

  constructor() {
    super({
      name: 'Modules',
    })
  }

  async activate(_context: vscode.ExtensionContext) {
    this.treeView = vscode.window.createTreeView(this.configPath!, {
      treeDataProvider: this,
      showCollapseAll: true,
      canSelectMany: false,
      dragAndDropController: undefined,
      manageCheckboxStateManually: false,
    })
    // If you actually register it, you don't get the collapsible state button :/
    // context.subscriptions.push(vscode.window.registerTreeDataProvider(this.configPath!, this))
  }

  getTreeItem(element: InstanceTreeItem): vscode.TreeItem {
    // check if element has gettreeitem
    if (element instanceof ModuleItem) {
      return element.getTreeItem()
    }
    if (element instanceof InstanceViewItem) {
      return element.getTreeItem()
    }

    return new vscode.TreeItem('undefined')
  }

  async getChildren(element?: undefined | InstanceTreeItem): Promise<InstanceTreeItem[]> {
    if (element === undefined) {
      return Array.from(this.modules.values()).sort((a, b) => {
        if (a.data.instCount === b.data.instCount && a.data.inst && b.data.inst) {
          return a.data.inst.instPath < b.data.inst.instPath ? -1 : 1
        }
        return a.data.instCount > b.data.instCount ? 1 : -1
      })
    }
    return await element.getChildren()
  }

  getParent(element: InstanceTreeItem): InstanceTreeItem | undefined {
    if (element instanceof ModuleItem) {
      return undefined
    }
    return element.parent
  }

  async resolveTreeItem(
    item: vscode.TreeItem,
    element: InstanceTreeItem,
    _token: vscode.CancellationToken
  ): Promise<vscode.TreeItem> {
    return element.resolveTreeItem(item, _token)
  }
}
