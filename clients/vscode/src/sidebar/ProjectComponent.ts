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
import { InstancesView } from './InstancesView'
import * as vv from './VaporviewApi'
import { getBasename, getIcons, isAnyVerilog } from '../utils'
import { Logger } from '../lib/logger'
import { glob } from 'glob'

const STRUCTURE_SYMS = [
  slang.SlangKind.Instance,
  slang.SlangKind.InstanceArray,
  slang.SlangKind.Scope,
  slang.SlangKind.ScopeArray,
]

interface HasChildren {
  getChildren(): Promise<HierItem[]>
  getChild(name: string): Promise<HierItem | undefined>
  getPath(): string
}

interface RevealOptions {
  revealHierarchy?: boolean
  revealFile?: boolean
  revealInstance?: boolean
  focus?: 'editor' | 'hierarchy' | 'modules'
  showBeside?: boolean
}

interface BuildFileParams {
  name?: string
  top?: string
}

type CompilationSource =
  | { type: 'buildfile'; buildfile: string; topFile?: never }
  | { type: 'topfile'; topFile: vscode.Uri; buildfile?: never }
  | { type: 'none'; buildfile?: never; topFile?: never }

function formatString(template: string, vars: BuildFileParams): string {
  return template.replace(/{(\w+)}/g, (_, key: string) => vars[key as keyof BuildFileParams] || '*')
}
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
        vv.addVariable({
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
  isVirtualLoc: boolean
  inst: slang.Item

  parent: HierItem | undefined
  children: HierItem[] | undefined
  childrenByName: Map<string, HierItem> = new Map()

  constructor(parent: HierItem | undefined, item: slang.Item) {
    this.parent = parent
    this.inst = item
    // blank uri- file://
    this.isVirtualLoc = item.instLoc.uri.length === 7
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
        res.push(new ScopeArrayItem(parent, item as slang.Scope))
        break
      case slang.SlangKind.Scope:
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

  async getTreeItem(): Promise<TreeItem> {
    let item = new TreeItem(this.inst.instName)
    item.iconPath = new vscode.ThemeIcon('symbol-namespace')
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
    item.iconPath = new vscode.ThemeIcon('chip')
    item.description = this.inst.declName
    return item
  }

  async _fetchChildren(): Promise<HierItem[]> {
    return await getSlangChildren(this, this.getPath())
  }

  async hasChildren(): Promise<boolean> {
    return (await this.getChildren()).length > 0
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

// Split a scope path into parts, handling array indices.
// e.g. "top.u1[0].u2[0][1]" -> ["top", "u1", "[0]", "u2", "[0]", "[1]"]
function splitScope(path: string): string[] {
  const parts = path.split('.')
  const partsWithBrackets = []
  for (const part of parts) {
    if (part.includes('[')) {
      const bracketSplit = part.split('[')
      partsWithBrackets.push(bracketSplit[0])
      for (const index of bracketSplit.slice(1)) {
        partsWithBrackets.push('[' + index)
      }
    } else {
      partsWithBrackets.push(part)
    }
  }
  return partsWithBrackets
}

class VarItem extends HierItem {
  inst: slang.Var
  static PARAM_TYPES: slang.SlangKind[] = [slang.SlangKind.Param]
  static DATA_TYPES: slang.SlangKind[] = [slang.SlangKind.Logic, slang.SlangKind.Port]

  constructor(parent: HierItem | undefined, instance: slang.Var) {
    super(parent, instance)
    this.inst = instance
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
        item.iconPath = new vscode.ThemeIcon('symbol-variable')
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

  // Getters for backward compatibility
  get buildfile(): string | undefined {
    return this.compilationSource.type === 'buildfile'
      ? this.compilationSource.buildfile
      : undefined
  }

  get topFile(): vscode.Uri | undefined {
    return this.compilationSource.type === 'topfile' ? this.compilationSource.topFile : undefined
  }

  // Setters to maintain mutual exclusivity
  set buildfile(value: string | undefined) {
    if (value === undefined) {
      this.compilationSource = { type: 'none' }
    } else {
      this.compilationSource = { type: 'buildfile', buildfile: value }
    }
  }

  set topFile(value: vscode.Uri | undefined) {
    if (value === undefined) {
      this.compilationSource = { type: 'none' }
    } else {
      this.compilationSource = { type: 'topfile', topFile: value }
    }
  }

  // Show the selected instance for the open file
  focusedBar: vscode.StatusBarItem

  // Map from instance uri to instance for following along in the hierarchy view
  moduleToInstance: Map<string, InstanceItem> = new Map()

  // Hierarchy Tree
  private _onDidChangeTreeData: vscode.EventEmitter<void> = new vscode.EventEmitter<void>()
  readonly onDidChangeTreeData: vscode.Event<void> = this._onDidChangeTreeData.event
  treeView: vscode.TreeView<HierItem> | undefined
  focused: HierItem | undefined = undefined

  // Instances Index
  instancesView: InstancesView = new InstancesView()

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
      this.topFile = uri
      await slang.setTopLevel(uri.fsPath)
      await this.refreshSlangCompilation()
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
  isRevalingFile: boolean = false

  async onStart(): Promise<void> {
    await this.refreshSlangCompilation()
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

      this.unit = undefined
      this.top = undefined

      await this.instancesView.clearModules()
      this._onDidChangeTreeData.fire()
      await slang.setBuildFile('')
      this.focusedBar.hide()
    }
  )

  async reveal(item: HierItem | undefined = undefined, focus: boolean = false) {
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
    if (item !== undefined) {
      this.logger.info('Revealing in hierarchy: ' + item.getPath())
      await this.treeView?.reveal(item, { select: true, focus: focus, expand: true })
    }
  }

  fuzzyFindInstance: ViewButton = new ViewButton(
    {
      title: 'Fuzzy Find Instances',
      icon: '$(search-view-icon)',
      keybind: 'cmd+f',
      keybindContainer: true,
    },
    async (_instance: HierItem | undefined) => {
      // Calling with undefined will pull up the instance quick pick
      // We can't bind setInstance directly since it'll pass the focused tree item
      await this.setInstance.func(undefined)
    }
  )

  // Set instance given one of:
  // - a path (from internal calls)
  // - a hierarchy item (from hierarchy or modules view)
  // - undefined (let user select from compilation)
  setInstance: CommandNode = new CommandNode(
    {
      title: 'Select Instance',
    },
    async (
      instance: HierItem | string | undefined,
      { revealHierarchy, revealFile, revealInstance, focus, showBeside }: RevealOptions = {
        revealHierarchy: true,
        revealFile: true,
        revealInstance: true,
      }
    ) => {
      if (instance === undefined) {
        if (this.unit === undefined) {
          // TODO: have one flow that this leads to- setting top, then specfiying build spec / params
          await vscode.window.showInformationMessage('Please set top level or build file first')
        }

        let options: vscode.QuickPickItem[] = []
        await Promise.all(
          Array.from(this.instancesView.modules.values()).map(async (mod) => {
            const children = await mod.getChildren()
            for (const child of children) {
              options.push({
                label: child.data.instPath,
                description: mod.data.declName,
              })
            }
          })
        )
        const selectedInst = await vscode.window.showQuickPick(options, {
          placeHolder: 'Enter instance path',
        })

        if (selectedInst === undefined) {
          return
        }
        instance = selectedInst.label
        this.logger.info('Selected instance: ' + instance)
      }

      // resolve instances if hierarchy path
      if (typeof instance === 'string') {
        // const scopes = await slang.getScopes(instance)
        const scopes = splitScope(instance)
        // Go through hierarchy, revealing each level
        if (this.unit === undefined) {
          // TODO: set the top level based on top name?
          await vscode.window.showErrorMessage(
            'Please set top level or build file first (no $unit)'
          )
          return
        }
        let current: HasChildren = this.unit!
        for (let scope of scopes) {
          let child = await current.getChild(scope)

          if (child === undefined) {
            this.logger.warn(`Could not find instance ${scope} in ${current.getPath()}`)
            break
          }

          current = child
        }

        if (!(current instanceof HierItem)) {
          vscode.window.showErrorMessage(
            'Invalid instance type: ' + typeof current + ' = ' + current
          )
          return
        }
        instance = current
      }

      this.focused = instance
      if (revealHierarchy) {
        if (instance.isVirtualLoc && !this.includeMacroDefined) {
          await this.toggleHiddenFunc()
        }
        if (!this.symFilter.has(instance.inst.kind)) {
          switch (instance.inst.kind) {
            case slang.SlangKind.Param:
              await this.toggleParamsFunc()
              break
            case slang.SlangKind.Logic:
              await this.toggleDataFunc()
              break
          }
        }
        await this.reveal(instance, focus === 'hierarchy')
      }

      if (revealFile) {
        const uri = vscode.Uri.parse(instance.inst.instLoc.uri)
        // Check if URI has a valid path (not empty or just a directory)
        if (uri.path && uri.path !== '/' && uri.path.length > 0) {
          try {
            this.isRevalingFile = true
            await vscode.window.showTextDocument(uri, {
              selection: instance.inst.instLoc.range,
              preserveFocus: focus !== 'editor',
              viewColumn: showBeside ? vscode.ViewColumn.Beside : undefined,
            })
            this.isRevalingFile = false
          } catch (error) {
            this.logger.warn(`Failed to open file at ${uri.toString()}: ${error}`)
            this.isRevalingFile = false
          }
        } else {
          // This will be fixed in a future release by asking slang server to do the open
          vscode.window.showWarningMessage('Cannot open file, likely defined from a macro.')
        }
      }

      if (revealInstance) {
        // select the most recent module
        while (!(instance instanceof InstanceItem)) {
          instance = instance.parent
          if (instance === undefined) {
            return
          }
        }
        this.instancesView.revealPath(instance.inst.declName, instance.getPath())
      }

      const parentModule = instance.getModule()
      if (parentModule) {
        this.moduleToInstance.set(parentModule.inst.declLoc.uri, parentModule!)
        this.focusedBar.text = `$(chip) ${parentModule.getPath()}`
        this.focusedBar.show()
      }

      return instance
    }
  )

  public async maybeOpenWaveform(): Promise<boolean> {
    const vvExt = await vv.getVaporviewExtension()

    if (!vvExt) {
      return false
    }

    // check if a waveform is already open
    const docs = await vv.getOpenDocuments()
    if (docs.documents.length > 0) {
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
          await vv.openFile({ uri: vscode.Uri.file(selected) })
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
          await vv.openFile({ uri: vscode.Uri.file(selected) })
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
      await vv.openFile({ uri: selectedFile })
    } catch (error) {
      vscode.window.showErrorMessage('Failed to open waveform: ' + error)
      return false
    }
    return true
  }

  // This only needs to be used for generated .f files- otherwise people may overuse this
  // We should setup a proper file watcher on the build file instead
  // refreshCompilation: ViewButton = new ViewButton(
  //   {
  //     title: 'Refresh Compilation',
  //     icon: '$(refresh)',
  //   },
  //   async () => {
  //     // set either top or buildfile again
  //     if (this.buildfile) {
  //       // await slang.setBuildFile(this.buildfile)
  //       await this.selectBuildFile.func()
  //     } else if (this.topFile) {
  //       await this.setTopLevel.func(this.topFile)
  //     }
  //     await this.refreshSlangCompilation()
  //   }
  // )

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
      // quick pick from the glob
      const glob = ext.slangConfig.buildPattern
      if (!glob) {
        vscode.window.showErrorMessage('No buildPattern set in any .slang-server.json')
        return
      }
      // can't use workspace findfiles bc the build.f files are probably generated and not in repo
      this.logger.info('Looking for build files: ' + glob.replace('{}', '*'))
      const files = await ext.findFiles([glob.replace('{}', '*')], true)
      if (files.length === 0) {
        vscode.window.showErrorMessage('No build files found')
        return
      }

      // trim off common prefixes
      const globPre = glob.substring(0, glob.indexOf('{}'))
      const commonPrefixLen = files[0].fsPath.indexOf(globPre) + globPre.length
      const commonPrefix = files[0].fsPath.substring(0, commonPrefixLen)

      let items = files.map((f) => f.fsPath.replace(commonPrefix, ''))
      let selection = await vscode.window.showQuickPick(items, {
        placeHolder: 'Select a build file',
      })
      if (selection === undefined) {
        return
      }

      this.buildfile = commonPrefix + selection
      await slang.setBuildFile(this.buildfile)

      await this.refreshSlangCompilation()
    }
  )

  //////////////////////////////////////////////////////////////////
  // Symbol Filtering
  //////////////////////////////////////////////////////////////////

  symFilter: Set<string> = new Set<string>(STRUCTURE_SYMS)
  // params / localparams (constants)
  includeParams: boolean = false
  // ports / nets / registers (variables)
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
      for (let type of VarItem.DATA_TYPES) {
        this.symFilter.add(type)
      }
    } else {
      for (let type of VarItem.DATA_TYPES) {
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

  //////////////////////////////////////////////////////////////////
  // Inline Item Buttons
  //////////////////////////////////////////////////////////////////

  showSourceFile: TreeItemButton = new TreeItemButton(
    {
      title: 'Show Module',
      viewItems: ['Module'],
      icon: getIcons('go-to-file'),
    },
    async (item: HierItem) => {
      if (item instanceof InstanceItem && item) {
        this.isRevalingFile = true
        await vscode.window.showTextDocument(vscode.Uri.parse(item.inst.declLoc.uri), {
          selection: item.inst.declLoc.range,
        })
        this.isRevalingFile = false
      }
    }
  )

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
        await vv.addVariable({
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

    const files = await ext.findFiles([buildPattern], true)
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
      await slang.setBuildFile(this.buildfile)
      await this.refreshSlangCompilation({
        revealFile: false,
        revealHierarchy: false,
        revealInstance: false,
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
    async (item: vv.NetlistTreeItemData | undefined) => {
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
        fullpath = item.scopePath + '.' + fullpath
      }
      if (this.unit === undefined && ext.slangConfig.buildPattern) {
        const decoded = vv.decodeNetlistUri(item.resourceUri!)
        const basename = getBasename(decoded.fsPath)!
        const top = decoded.scopeId?.split('.')[0] || ''
        await this.openBuildFile({ name: basename, top: top })
      }
      await this.setInstance.func(fullpath, {
        revealHierarchy: true,
        revealFile: true,
        revealInstance: true,
        focus: 'editor',
        showBeside: true,
      })
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
    async (item: vv.SignalItemData | undefined) => {
      // If we're coming from the keybind, we have to get the focused signal ourselves
      let signalPath = ''
      let vvExt: vv.ViewerSettings | undefined = undefined
      if (item === undefined) {
        vvExt = await vv.getViewerState()
        signalPath = vvExt.selectedSignal?.name || ''
      } else {
        signalPath = item.signalName
        if (item.scopePath) {
          signalPath = item.scopePath + '.' + signalPath
        }
      }

      if (this.unit === undefined && ext.slangConfig.buildPattern) {
        let basename = ''
        let top = ''
        if (item === undefined) {
          top = vvExt!.selectedSignal?.name.split('.')[0] || ''
          basename = getBasename(vvExt!.fileName)!
        } else {
          top = item.scopePath.split('.')[0] || ''
          basename = getBasename(item.uri.path)!
        }

        await this.openBuildFile({ name: basename, top: top })
      }

      await this.setInstance.func(signalPath, {
        revealHierarchy: true,
        revealFile: true,
        revealInstance: true,
        focus: 'editor',
        showBeside: true,
      })
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
    this.focusedBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100)
    this.focusedBar.name = 'Slang Instance'
    vscode.window.onDidChangeActiveTextEditor(async (e: vscode.TextEditor | undefined) => {
      if (e === undefined) {
        return
      }
      if (this.unit === undefined) {
        return
      }
      if (!isAnyVerilog(e.document.languageId)) {
        return
      }
      if (this.isRevalingFile) {
        return
      }

      // Only show when slang view is visible.
      // Users may put the instances view in another tab, so check that too.
      if (!(this.treeView?.visible === true || this.instancesView.treeView?.visible === true)) {
        return
      }

      this.logger.info(
        'Active editor changed: ' + e.document.uri.toString(),
        'updating instances view'
      )

      // if we had selected this instance before, go back to it
      const instance = this.moduleToInstance.get(e.document.uri.toString())
      if (
        instance !== undefined &&
        !vscode.window.activeTextEditor!.document.uri.fsPath.endsWith(instance.inst.instLoc.uri)
      ) {
        await this.setInstance.func(instance, {
          revealHierarchy: true,
          revealFile: false,
          revealInstance: false,
          focus: 'hierarchy',
        })
      }

      // always open the modules view so we can select an instance

      // try this first to avoid querying slang-server
      const basename = getBasename(e.document.uri.fsPath)!
      if (this.instancesView.modules.has(basename)) {
        this.instancesView.revealPath(basename)
        return
      }

      const modules = await slang.getModulesInFile(e.document.uri.fsPath)
      if (modules.length === 0) {
        this.logger.info('No modules found in file')
        return
      }
      this.instancesView.revealPath(modules[0])
    })
  }

  static RE_INSTANCE_PATHS = /(?<!\/)[\w$]+(\[\d+\])?(\.[\w$]+(\[\d+\])?)+/g

  async provideTerminalLinks(
    context: vscode.TerminalLinkContext,
    _token: vscode.CancellationToken
  ): Promise<InstanceLink[]> {
    let links = []
    for (let match of context.line.matchAll(ProjectComponent.RE_INSTANCE_PATHS)) {
      const line = context.line
      const startIndex = line.indexOf(match[0])
      const path = match[0]
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
    await this.setInstance.func(link.path, {
      revealHierarchy: true,
      revealFile: true,
      revealInstance: true,
      focus: 'editor',
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

    // user updates to buildfile
    vscode.workspace.onDidSaveTextDocument(async (document) => {
      if (document.uri.fsPath === this.buildfile) {
        this.logger.info(
          'Build file updated, reloading: ' + vscode.workspace.asRelativePath(this.buildfile)
        )
        vscode.commands.executeCommand('slang.setBuildFile', this.buildfile)
        await this.refreshSlangCompilation()
      }
    })
  }

  async refreshSlangCompilation(
    revealOptions: RevealOptions = {
      revealHierarchy: true,
      revealFile: true,
      revealInstance: false,
    }
  ) {
    const unit = await slang.getUnit()
    if (unit.length === 0) {
      this.unit = undefined
      return
    }
    this.unit = new UnitItem(
      unit.map((item) =>
        item.kind === slang.SlangKind.Instance ? new TopItem(item) : new PkgItem(item)
      )
    )

    const tops = this.unit.children.filter((item) => item.inst.kind === slang.SlangKind.Instance)

    if (tops.length === 1 && this.treeView !== undefined) {
      this.top = tops[0]
      this.setInstance.func(tops[0], revealOptions)
    }
    this._onDidChangeTreeData.fire()
    // TODO: maybe setInstance() regardless of how many tops there are
    await this.instancesView.updateModules()
  }

  async getTreeItem(element: HierItem): Promise<TreeItem> {
    if (element.inst.kind === slang.SlangKind.Package) {
      const treeItem = await element.getTreeItem()
      treeItem.collapsibleState = vscode.TreeItemCollapsibleState.Collapsed
      return treeItem
    }

    const [treeItem, children] = await Promise.all([
      element.getTreeItem(),
      this.getChildren(element),
    ])
    if (children.length === 0) {
      treeItem.collapsibleState = vscode.TreeItemCollapsibleState.None
    } else if (element instanceof RootItem && element.inst.kind === slang.SlangKind.Instance) {
      treeItem.collapsibleState = vscode.TreeItemCollapsibleState.Expanded
    } else {
      treeItem.collapsibleState = vscode.TreeItemCollapsibleState.Collapsed
    }
    return treeItem
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
    if (!this.symFilter.has(element.inst.kind)) {
      return false
    }
    if (!this.includeMacroDefined && element.isVirtualLoc) {
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
    /// Triggered on hover
    item.tooltip = element.getPath()
    item.command = {
      title: 'Go to definition',
      command: 'slang.project.setInstance',
      arguments: [element, { revealHierarchy: false, revealFile: true, revealInstance: true }],
    }

    return item
  }
}
