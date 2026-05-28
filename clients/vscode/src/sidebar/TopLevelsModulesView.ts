import * as vscode from 'vscode'

import { ViewComponent } from '../lib/libconfig'
import { ProjectComponent, RootItem } from './ProjectComponent'
import * as slang from '../SlangInterface'

const projectByView = new WeakMap<TopLevelModulesView, ProjectComponent>()

class TopLevelModuleItem {
  constructor(
    public readonly top: RootItem,
    private readonly selected: boolean
  ) {}

  getTreeItem(): vscode.TreeItem {
    const item = new vscode.TreeItem(this.top.inst.instName)

    item.description = this.top.inst.declName
    item.tooltip = this.top.getPath()
    item.iconPath = new vscode.ThemeIcon(this.selected ? 'check' : 'chip')
    item.contextValue = 'TopLevelModule'

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
  implements vscode.TreeDataProvider<TopLevelModuleItem>
{
  private _onDidChangeTreeData = new vscode.EventEmitter<void>()
  readonly onDidChangeTreeData = this._onDidChangeTreeData.event

  treeView: vscode.TreeView<TopLevelModuleItem> | undefined

  constructor(project: ProjectComponent) {
    super({
      name: 'Top Level',
    })

    projectByView.set(this, project)
  }

  private get project(): ProjectComponent {
    const project = projectByView.get(this)
    if (project === undefined) {
      throw new Error('TopLevelModulesView project reference was not initialized')
    }
    return project
  }

  async activate(context: vscode.ExtensionContext): Promise<void> {
    this.treeView = vscode.window.createTreeView(this.configPath!, {
      treeDataProvider: this,
      showCollapseAll: false,
      canSelectMany: false,
      dragAndDropController: undefined,
      manageCheckboxStateManually: false,
    })

    context.subscriptions.push(this.treeView)

    context.subscriptions.push(
      vscode.commands.registerCommand(
        this.configPath! + '.select',
        async (item: TopLevelModuleItem) => {
          await this.project.selectRootTopLevel(item.top, {
            revealHierarchy: true,
            revealFile: false,
            revealInstance: true,
            focus: 'hierarchy',
          })
        }
      )
    )
  }

  refresh(): void {
    this._onDidChangeTreeData.fire()
  }

  getTreeItem(element: TopLevelModuleItem): vscode.TreeItem {
    return element.getTreeItem()
  }

  async getChildren(element?: TopLevelModuleItem): Promise<TopLevelModuleItem[]> {
    if (element !== undefined || this.project.unit === undefined) {
      return []
    }

    return this.project.unit.children
      .filter((item) => item.inst.kind === slang.SlangKind.Instance)
      .map((top) => new TopLevelModuleItem(top, top === this.project.top))
  }

  getParent(_element: TopLevelModuleItem): undefined {
    return undefined
  }
}
