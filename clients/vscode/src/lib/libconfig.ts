import { readFile, writeFile } from 'fs/promises'
import * as process from 'process'
import * as vscode from 'vscode'
import { JSONSchemaType } from './jsonSchema'
import { Logger, StubLogger, createLogger } from './logger'
import { IConfigurationPropertySchema } from './vscodeConfigs'
import fs = require('fs')
import { getPlatform } from './platform'

export async function fileExists(filePath: string): Promise<boolean> {
  try {
    await fs.promises.access(filePath, fs.constants.F_OK)
    return true
  } catch {
    return false
  }
}

class ExtensionNode {
  nodeName: string | undefined
  configPath: string | undefined
  _parentNode: ExtensionComponent | undefined

  onConfigUpdated(func: () => Promise<void> | void): vscode.Disposable {
    return vscode.workspace.onDidChangeConfiguration(async (e) => {
      if (e.affectsConfiguration(this.configPath!)) {
        void func()
      }
    })
  }

  compile(nodeName: string, parentNode?: ExtensionComponent): void {
    this._parentNode = parentNode
    this.nodeName = nodeName

    if (parentNode) {
      this.configPath = [parentNode.configPath, this.nodeName].join('.')
    } else {
      // root node
      this.configPath = this.nodeName
    }
  }
}

export abstract class ExtensionComponent extends ExtensionNode {
  /**
   * Container for extensions functionality and config
   */
  logger: Logger
  title: string | undefined
  private children: ExtensionNode[]

  constructor() {
    super()
    this.logger = new StubLogger()
    this.children = []
  }

  public async activateExtension(
    /**
     * Activates the extension component
     * @param nodeName The name of the node in the configuration tree
     * @param title The display title for the extension, main logger
     * @param context The VSCode extension context
     * @param incompatibleExtensions Optional array of extension IDs that are incompatible with this extension
     */
    nodeName: string,
    title: string,
    context: vscode.ExtensionContext,
    incompatibleExtensions?: string[]
  ): Promise<void> {
    this.title = title
    this.compile(nodeName)

    await this.postOrderTraverse(async (node: ExtensionNode) => {
      if (node instanceof ExtensionComponent || node instanceof CommandNode) {
        await node.activate(context)
      }
      if (node instanceof ConfigObject) {
        node.getValue()
      }
    })

    if (context.extensionMode !== vscode.ExtensionMode.Production) {
      const updateCommand = `${nodeName}.updateConfig`
      vscode.commands.registerCommand(updateCommand, async () => {
        // update package.json
        {
          let filePath = context.extensionPath + '/package.json'
          const data = await readFile(filePath, { encoding: 'utf-8' })
          let json = JSON.parse(data)

          // update config properties
          this.updateJson(json, updateCommand)

          const updatedJson = JSON.stringify(json, null, 2) + '\n'
          await writeFile(filePath, updatedJson, { encoding: 'utf-8' })
        }

        // update config.md
        {
          let filePath = context.extensionPath + '/CONFIG.md'
          await writeFile(filePath, this.getConfigMd(), { encoding: 'utf-8' })
        }
      })
    }

    if (incompatibleExtensions !== undefined) {
      for (let id of incompatibleExtensions) {
        if (vscode.extensions.getExtension(id) !== undefined) {
          vscode.window.showErrorMessage(`Please uninstall incompatible extension: ${id}`)
        }
      }
    }
  }

  preOrderConfigTraverse<T extends JSONSchemaType>(func: (obj: ConfigObject<T>) => void): void {
    this.preOrderTraverse((obj: ExtensionNode) => {
      if (obj instanceof ConfigObject) {
        func(obj)
      }
    })
  }

  preOrderComponentTraverse(func: (obj: ExtensionComponent) => void): void {
    this.preOrderTraverse((obj: ExtensionNode) => {
      if (obj instanceof ExtensionComponent) {
        func(obj)
      }
    })
  }

  preOrderTraverse(func: (obj: ExtensionNode) => void): void {
    func(this)
    this.children.forEach((obj: ExtensionNode) => {
      if (obj instanceof ExtensionComponent) {
        obj.preOrderTraverse(func)
      } else {
        func(obj)
      }
    })
  }

  async postOrderTraverse(func: (obj: ExtensionNode) => Promise<void> | void): Promise<void> {
    for (const obj of this.children) {
      if (obj instanceof ExtensionComponent) {
        await obj.postOrderTraverse(func)
      } else {
        await func(obj)
      }
    }
    await func(this)
  }

  compile(nodeName: string, parentNode?: ExtensionComponent): void {
    super.compile(nodeName, parentNode)

    if (parentNode) {
      this.logger = parentNode.logger?.getChild(this.nodeName!)
    } else {
      // root node
      this.logger = createLogger(this.title!)
    }

    const subTypeProperties = Object.getOwnPropertyNames(this).filter(
      (childName) => !childName.startsWith('_')
    )
    for (let childName of subTypeProperties) {
      // get the property values
      let obj: any = (this as any)[childName]
      if (!(obj instanceof ExtensionNode)) {
        continue
      }

      obj.compile(childName, this)
      this.children.push(obj)
    }
  }

  getRoot(): ExtensionComponent {
    if (!this._parentNode) {
      return this
    }
    return this._parentNode.getRoot()
  }

  showErrorMessage(msg: string) {
    vscode.window.showErrorMessage(`[${this.configPath}] ${msg}`)
  }

  /**
   * Override this. Activated in a preorder traversal
   */
  async activate(_context: vscode.ExtensionContext): Promise<void> {}

  getViews(): ViewComponent[] {
    const views: ViewComponent[] = []
    this.preOrderTraverse((child) => {
      if (child instanceof ViewComponent) {
        views.push(child)
      }
    })
    return views
  }

  getCommands(): CommandNode[] {
    return this.children.filter((child) => child instanceof CommandNode) as CommandNode[]
  }

  updateJson(json: any, updateCommand: string): void {
    /// Update package.json from compiled values
    {
      // config
      let props: any = {}
      this.preOrderConfigTraverse((obj: ConfigObject<any>) => {
        props[obj.configPath!] = obj.getConfigJson()
      })
      json.contributes.configuration.properties = props
    }

    {
      // view containers
      let contiainers: any = { activitybar: [], panel: [] }
      let views: any = {}
      let viewsWelcome: any = []
      let viewsTitleButtons: any = []
      let viewsInlineButtons: any = []
      let webviewButtons: any = []
      let binds: any = []
      this.preOrderComponentTraverse((cont: ExtensionComponent) => {
        if (!(cont instanceof ViewContainerComponent)) {
          return
        }
        if (cont instanceof ActivityBarComponent) {
          contiainers.activitybar.push(cont.obj)
        }
        if (cont instanceof PanelComponent) {
          contiainers.panel.push(cont.obj)
        }
        // get views from containers
        views[cont.obj.id] = []
        for (let view of cont.getViews()) {
          // remove vobj.welcome if exists, ad id
          let vobjCopy: any = {
            id: view.configPath,
            ...view.obj,
          }
          delete vobjCopy.welcome
          // vobjCopy.id = view.configPath
          views[cont.obj.id].push(vobjCopy)

          if (view.obj.welcome) {
            let welc: any = {
              view: view.configPath,
              ...view.obj.welcome,
            }
            viewsWelcome.push(welc)
          }

          for (let button of view.getCommands()) {
            if (button instanceof ButtonNode) {
              if (button instanceof ViewButton) {
                viewsTitleButtons.push(button.getButtonWhen())
              } else if (button instanceof TreeItemButton) {
                viewsInlineButtons.push(button.getButtonWhen())
                if (button.obj.isSubmenu) {
                  // add submenu number for priority
                  viewsInlineButtons[viewsInlineButtons.length - 1].group +=
                    `@${viewsInlineButtons.length}`
                }
              } else if (button instanceof WebviewButton) {
                webviewButtons.push(button.getButtonWhen())
              }

              if (button.obj.keybind) {
                let bind: { key: any; command: string; when: string; mac?: string } = {
                  key: button.obj.keybind,
                  command: button.configPath!,
                  when: button.getBindWhen(),
                }
                if (bind.key.includes('cmd+')) {
                  bind.mac = bind.key
                  bind.key = bind.key.replace('cmd+', 'ctrl+')
                }
                binds.push(bind)
              }
            }
          }
        }
      })
      json.contributes.viewsContainers = contiainers
      json.contributes.views = views
      json.contributes.viewsWelcome = viewsWelcome
      if (json.contributes.menus === undefined) {
        json.contributes.menus = {}
      }
      json.contributes.menus['view/title'] = viewsTitleButtons
      json.contributes.menus['view/item/context'] = viewsInlineButtons
      json.contributes.menus['webview/context'] = webviewButtons
      json.contributes.keybindings = binds
    }

    {
      // editor buttons
      let editorButtons: any = []
      this.preOrderTraverse((node: ExtensionNode) => {
        if (node instanceof EditorButton) {
          editorButtons.push(node.getButtonWhen())
        }
      })
      json.contributes.menus['editor/title'] = editorButtons
    }

    {
      // commands
      let commands: any = []
      this.preOrderTraverse((node: ExtensionNode) => {
        if (node instanceof CommandNode) {
          let cmd = { command: node.configPath, ...node.obj }
          commands.push(cmd)
        }
      })

      // TODO: maybe remove this for production?
      commands.push({
        command: updateCommand,
        title: 'Extdev: update config (package.json and CONFIG.md)',
      })
      json.contributes.commands = commands
    }
  }

  getConfigMd(): string {
    let out = '# Configuration Settings\n\n'
    this.preOrderConfigTraverse((obj: ConfigObject<any>) => {
      out += obj.getMarkdownString()
    })
    return out + '\n'
  }
}

////////////////////////////////////////////////////
// Commands and Buttons
////////////////////////////////////////////////////
type iconType = string | { dark: string; light: string }

interface CommandConfigSpec {
  // command: string - this is filled in via configPath
  title: string
  shortTitle?: string
  icon?: iconType
  // From vscode api
  category?: string
  enablement?: string
  keybind?: string
}
interface ContextCommandSpec extends CommandConfigSpec {
  // Since context commands are from buttons, they typically won't be labeled with the extension name
  shown?: boolean
}

interface EditorButtonSpec extends ContextCommandSpec {
  languages: string[]
}

interface WebviewButtonSpec extends ContextCommandSpec {
  editorId: string
  webviewSection: string
  group: string
}

interface ViewButtonSpec extends ContextCommandSpec {
  // Whether the keybinds for this should apply to the container rather than just that view
  keybindContainer?: boolean
}
interface TreeItemButtonSpec extends ContextCommandSpec {
  // These can only take light/dark svgs for an icon
  icon?: iconType

  isSubmenu?: boolean
  // Contexts (viewItem) in which this button should be shown. Empty implies always
  viewItems?: string[]
  // Override the view when clause for external view buttons
  viewOverride?: string
}

// interface ContextMenuButtonSpec extends ContextCommandSpec {
//   // These can only take light/dark svgs for an icon
//   icon?: iconType
//   // Contexts (viewItem) in which this button should be shown. Empty implies always
//   viewItems?: string[]
//   // Override the view when clause for external view buttons
//   viewOverride?: string
// }

export class CommandNode<
  Spec extends ContextCommandSpec = CommandConfigSpec,
> extends ExtensionNode {
  obj: Spec
  func: (...args: any[]) => any
  thisArg?: any
  constructor(obj: Spec, func: (...args: any[]) => any, thisArg?: any) {
    super()
    this.obj = obj
    this.func = func
    this.thisArg = thisArg
  }

  async activate(context: vscode.ExtensionContext): Promise<void> {
    context.subscriptions.push(
      vscode.commands.registerCommand(this.configPath!, this.func, this.thisArg)
    )
    if (this._parentNode !== undefined) {
      this.obj.title = this._parentNode.getRoot().title + ': ' + this.obj.title
    }
  }
}

interface ButtonSpec {
  command: string
  group: string
  when: string
}
abstract class ButtonNode<Spec extends ContextCommandSpec> extends CommandNode<Spec> {
  async activate(context: vscode.ExtensionContext): Promise<void> {
    // same thing, but don't add the extension title; it's implied
    if (this.obj.shown) {
      await super.activate(context)
    } else {
      context.subscriptions.push(
        vscode.commands.registerCommand(this.configPath!, this.func, this.thisArg)
      )
    }
  }

  abstract getButtonWhen(): ButtonSpec

  abstract getBindWhen(): string
}
export class ViewButton extends ButtonNode<ViewButtonSpec> {
  getButtonWhen() {
    return {
      command: this.configPath!,
      group: 'navigation',
      when: `view == ${this._parentNode!.configPath}`,
    }
  }

  getBindWhen() {
    return this.obj.keybindContainer
      ? `sideBarFocus && activeViewlet == workbench.view.extension.${
          this._parentNode?.getRoot().configPath
        } && !inputFocus`
      : `focusedView == ${this._parentNode!.configPath} && !inputFocus`
  }
}

export class WebviewButton extends ButtonNode<WebviewButtonSpec> {
  getButtonWhen(): ButtonSpec {
    return {
      command: this.configPath!,
      group: this.obj.group,
      when: `activeCustomEditorId == ${this.obj.editorId} && webviewSection == ${this.obj.webviewSection}`,
    }
  }

  getBindWhen(): string {
    return `activeCustomEditorId == ${this.obj.editorId} && !inputFocus`
  }
}

export class TreeItemButton extends ButtonNode<TreeItemButtonSpec> {
  getButtonWhen(): ButtonSpec {
    let obj: ButtonSpec = {
      command: this.configPath!,
      group: this.obj.isSubmenu ? 'submenu' : 'inline',
      when: `view == ${this.obj.viewOverride ?? this._parentNode!.configPath}`,
    }
    if (this.obj.viewItems) {
      // empty implies no restrictions
      if (this.obj.viewItems.length > 0) {
        obj.when += ' && ' + this.obj.viewItems.map((id) => `viewItem == ${id}`).join(' || ')
      }
    }
    return obj
  }

  getBindWhen(): string {
    return `focusedView == ${this.obj.viewOverride ?? this._parentNode!.configPath} && !inputFocus`
  }
}

export class EditorButton extends ButtonNode<EditorButtonSpec> {
  getButtonWhen(): ButtonSpec {
    return {
      command: this.configPath!,
      when: this.obj.languages.map((lang) => `resourceLangId == ${lang}`).join(' || '),
      group: 'navigation',
    }
  }
  getBindWhen(): string {
    return this.obj.languages.map((lang) => `resourceLangId == ${lang}`).join(' || ')
  }
}

export class ExternalEditorButton extends EditorButton {
  constructor(spec: EditorButtonSpec) {
    super(spec, async () => {})
  }

  async activate() {}
}

////////////////////////////////////////////////////
// Views
////////////////////////////////////////////////////

export interface ViewContainerSpec {
  id: string
  title: string
  icon: string
}
class ViewContainerComponent extends ExtensionComponent {
  obj: ViewContainerSpec
  constructor(obj: ViewContainerSpec) {
    super()
    this.obj = obj
  }
}
export class ActivityBarComponent extends ViewContainerComponent {}
export class PanelComponent extends ViewContainerComponent {}

export interface WelcomeSpec {
  // view: string // filled in automatically
  contents: string
  enablement?: string
  group?: string
  when?: string
}

export interface ViewSpec {
  // id: string // filled in automatically
  name: string
  welcome?: WelcomeSpec
  type?: 'tree' | 'webview'
  initialSize?: number
  icon?: iconType
  visibility?: 'collapsed' | 'hidden' | 'visible'
  contextualTitle?: string
}

export class ViewComponent extends ExtensionComponent {
  obj: ViewSpec
  constructor(obj: ViewSpec) {
    super()
    this.obj = obj
  }
}

export interface ConfigObjectSpec extends IConfigurationPropertySchema {
  // no additional fields for now
  deprecationMessage?: string
  [key: string]: any
}

////////////////////////////////////////////////////
// Config Leaf
////////////////////////////////////////////////////
export class ConfigObject<T extends JSONSchemaType> extends ExtensionNode {
  protected obj: ConfigObjectSpec
  default: T
  cachedValue: T

  constructor(obj: ConfigObjectSpec) {
    super()
    this.obj = obj
    this.default = obj.default
    this.cachedValue = obj.default
  }

  getValue(): T {
    this.cachedValue = vscode.workspace.getConfiguration().get(this.configPath!, this.default!)
    return this.cachedValue
  }

  private inferSchema(value: any): any {
    if (typeof value === 'string') {
      return { type: 'string' }
    } else if (typeof value === 'number') {
      return { type: 'number' }
    } else if (typeof value === 'boolean') {
      return { type: 'boolean' }
    } else if (Array.isArray(value)) {
      // Infer items schema from first element, or assume string if array is empty
      const itemSchema = value.length > 0 ? this.inferSchema(value[0]) : { type: 'string' }
      return {
        type: 'array',
        items: itemSchema,
      }
    } else if (typeof value === 'object' && value !== null) {
      // Infer properties schema from object keys
      const properties: any = {}
      for (const key in value) {
        if (Object.prototype.hasOwnProperty.call(value, key)) {
          properties[key] = this.inferSchema(value[key])
        }
      }
      return {
        type: 'object',
        properties: properties,
      }
    } else {
      return {}
    }
  }

  getConfigJson(): any {
    // Note: Can't use reflection since TypeScript types are erased at runtime
    // Instead, we infer from default values and merge with any explicit schema properties
    if (this.obj['type'] === undefined) {
      const inferredSchema = this.inferSchema(this.default)
      if (Object.keys(inferredSchema).length === 0) {
        throw Error(`Was not able to deduce type for ${this.configPath}`)
      }
      // Merge inferred schema, but preserve any explicitly provided properties
      // (like 'items', 'properties', 'enum')
      for (const key in inferredSchema) {
        if (this.obj[key] === undefined) {
          this.obj[key] = inferredSchema[key]
        }
      }
    }
    return this.obj
  }

  getMarkdownString(): string {
    let cfgjson = this.getConfigJson()

    // Skip deprecated configs from documentation
    if ('deprecationMessage' in cfgjson) {
      return ''
    }

    let out = `- \`${this.configPath}\`: ${cfgjson.type} = `
    if (cfgjson.type === 'string') {
      out += `"${this.default}"\n\n`
    } else if (cfgjson.type === 'array') {
      if (this.default instanceof Array && this.default.length > 0) {
        out += `["${this.default.join('", "')}"]\n\n`
      } else {
        out += `[]\n\n`
      }
    } else {
      out += `${this.default}\n\n`
    }
    out += `  ${cfgjson.description}\n\n`
    if ('enum' in cfgjson) {
      out += `  Options:\n`
      for (let option of cfgjson.enum) {
        out += `  - ${option}\n`
      }
    }
    return out
  }
}

export function getShell(): string {
  if (vscode.env.shell !== '') {
    return vscode.env.shell
  }

  if (process.env.SHELL !== undefined) {
    return process.env.SHELL
  }

  if (getPlatform() === 'windows') {
    return 'cmd.exe'
  } else {
    return '/bin/bash'
  }
}
