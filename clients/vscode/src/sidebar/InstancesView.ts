// SPDX-License-Identifier: MIT

import * as vscode from 'vscode'

import { TreeItemButton, ViewComponent } from '../lib/libconfig'
import * as slang from '../SlangInterface'

export class InstanceViewItem {
  constructor(
    readonly parent: ModuleItem,
    readonly data: slang.QualifiedInstance
  ) {}

  getTreeItem(): vscode.TreeItem {
    const item = new vscode.TreeItem(this.data.instPath, vscode.TreeItemCollapsibleState.None)
    item.iconPath = new vscode.ThemeIcon('chip')
    item.contextValue = 'Instance'
    item.tooltip = this.data.instPath
    item.command = {
      title: 'Select Instance',
      command: 'slang.project.selectModuleInstance',
      arguments: [this.data.instPath, this.parent.data.declName],
    }
    return item
  }
}

class ModuleItem {
  private instances: Map<string, InstanceViewItem> | undefined

  constructor(readonly data: slang.Module) {}

  getTreeItem(expanded: boolean): vscode.TreeItem {
    const item = new vscode.TreeItem(
      `${this.data.declName} (${this.data.instCount})`,
      expanded
        ? vscode.TreeItemCollapsibleState.Expanded
        : vscode.TreeItemCollapsibleState.Collapsed
    )
    item.iconPath = new vscode.ThemeIcon('file')
    item.contextValue = 'Module'
    item.tooltip = this.data.declName
    return item
  }

  async getInstances(): Promise<Map<string, InstanceViewItem>> {
    if (this.instances === undefined) {
      this.instances = new Map()
      for (const instance of await slang.getInstancesOfModule(this.data.declName)) {
        this.instances.set(instance.instPath, new InstanceViewItem(this, instance))
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
  private readonly _onDidChangeTreeData = new vscode.EventEmitter<
    InstanceTreeItem | undefined | null
  >()
  readonly onDidChangeTreeData = this._onDidChangeTreeData.event

  private modules = new Map<string, ModuleItem>()
  private expandedModule: ModuleItem | undefined
  private treeView: vscode.TreeView<InstanceTreeItem> | undefined
  private changingExpansion = false
  gotoInstantiation: TreeItemButton

  constructor(onGotoInstantiation: (item: InstanceViewItem) => Promise<void>) {
    super({ name: 'Modules' })
    this.gotoInstantiation = new TreeItemButton(
      {
        title: 'Go to Instantiation',
        icon: '$(go-to-file)',
        viewItems: ['Instance'],
      },
      onGotoInstantiation
    )
  }

  async activate(context: vscode.ExtensionContext): Promise<void> {
    this.treeView = vscode.window.createTreeView(this.configPath!, {
      treeDataProvider: this,
      showCollapseAll: true,
      canSelectMany: false,
    })
    context.subscriptions.push(
      this.treeView,
      this.treeView.onDidExpandElement(({ element }) => {
        if (this.changingExpansion || !(element instanceof ModuleItem)) {
          return
        }
        void this.expandOnly(element, false)
      }),
      this.treeView.onDidCollapseElement(({ element }) => {
        if (!this.changingExpansion && element === this.expandedModule) {
          this.expandedModule = undefined
        }
      })
    )
  }

  private async expandOnly(module: ModuleItem, reveal: boolean): Promise<void> {
    const previous = this.expandedModule
    this.expandedModule = module
    if (previous && previous !== module) {
      this.changingExpansion = true
      try {
        await vscode.commands.executeCommand(
          `workbench.actions.treeView.${this.configPath}.collapseAll`
        )
        await this.treeView?.reveal(module, { expand: true })
      } finally {
        this.changingExpansion = false
      }
    } else if (reveal) {
      await this.treeView?.reveal(module, { expand: true })
    }
  }

  async updateModules(): Promise<void> {
    const expandedName = this.expandedModule?.data.declName
    this.modules = new Map(
      (await slang.getScopesByModule()).map((module) => [module.declName, new ModuleItem(module)])
    )
    this.expandedModule = expandedName ? this.modules.get(expandedName) : undefined
    this._onDidChangeTreeData.fire(undefined)
  }

  clearModules(): void {
    this.modules.clear()
    this.expandedModule = undefined
    this._onDidChangeTreeData.fire(undefined)
  }

  async revealPath(moduleName: string, instancePath: string): Promise<void> {
    if (!this.treeView?.visible) {
      return
    }

    const module = this.modules.get(moduleName)
    if (!module) {
      return
    }

    await this.expandOnly(module, true)
    const instance = (await module.getInstances()).get(instancePath)
    if (instance) {
      await this.treeView.reveal(instance, { select: true })
    }
  }

  getTreeItem(element: InstanceTreeItem): vscode.TreeItem {
    return element instanceof ModuleItem
      ? element.getTreeItem(element === this.expandedModule)
      : element.getTreeItem()
  }

  async getChildren(element?: InstanceTreeItem): Promise<InstanceTreeItem[]> {
    if (element instanceof ModuleItem) {
      return Array.from((await element.getInstances()).values())
    }
    if (element instanceof InstanceViewItem) {
      return []
    }
    return Array.from(this.modules.values()).sort((a, b) =>
      a.data.declName.localeCompare(b.data.declName)
    )
  }

  getParent(element: InstanceTreeItem): ModuleItem | undefined {
    return element instanceof InstanceViewItem ? element.parent : undefined
  }
}
