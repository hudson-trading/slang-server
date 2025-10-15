// SPDX-License-Identifier: MIT
import * as vscode from 'vscode'

import * as child_process from 'child_process'
import { glob } from 'glob'
import path from 'path'
import * as process from 'process'
import * as vscodelc from 'vscode-languageclient/node'
import { LanguageClient, LanguageClientOptions, ServerOptions } from 'vscode-languageclient/node'
import { ExternalFormatter } from './ExternalFormatter'
import {
  ActivityBarComponent,
  CommandNode,
  ConfigObject,
  EditorButton,
  fileExists,
  PathConfigObject,
  ViewContainerSpec,
} from './lib/libconfig'
import { ProjectComponent } from './sidebar/ProjectComponent'
import * as slang from './SlangInterface'
import { anyVerilogSelector, getWorkspaceFolder } from './utils'

export var ext: SlangExtension

export class SlangExtension extends ActivityBarComponent {
  ////////////////////////////////////////////////
  /// top level configs
  ////////////////////////////////////////////////

  formatDirs: ConfigObject<string[]> = new ConfigObject({
    default: [],
    description: 'Directories to format',
  })

  ////////////////////////////////////////////////
  /// extension subcomponents
  ////////////////////////////////////////////////

  svFormat: ExternalFormatter = new ExternalFormatter()
  verilogFormat: ExternalFormatter = new ExternalFormatter()

  // Side bar
  project: ProjectComponent = new ProjectComponent()

  ////////////////////////////////////////////////
  /// top level commands and configs
  ////////////////////////////////////////////////
  expandDir: vscode.Uri | undefined = undefined
  rewriterPath: ConfigObject<string> = new ConfigObject({
    default: '',
    description:
      'Rewriter command for macro expansion; e.g. `path/to/slang_rewriter --expand-macros`. This will shortly be part of the language server, and will not have to be set separately.',
  })

  rewrite: EditorButton = new EditorButton(
    {
      title: 'Expand Macros',
      shortTitle: 'Expand macros',
      languages: ['verilog', 'systemverilog'],
      icon: '$(open-preview)',
    },
    async () => {
      let doc = vscode.window.activeTextEditor?.document
      if (doc === undefined) {
        vscode.window.showErrorMessage('Open a verilog document to expand macros')
        return
      }

      if (ext.rewriterPath.getValue() === '') {
        vscode.window.showErrorMessage('`slang.rewriter` not provided in settings.json')
        return
      }
      let expDir = this.expandDir
      if (expDir === undefined) {
        return
      }

      let expanded = child_process.execSync(ext.rewriterPath.getValue() + ' ' + doc.uri.fsPath, {
        cwd: getWorkspaceFolder(),
      })

      let uri: vscode.Uri = vscode.Uri.joinPath(expDir, doc.uri.path.split('/').pop()!)
      await vscode.workspace.fs.writeFile(uri, expanded)
      await vscode.workspace.openTextDocument(uri)
      await vscode.commands.executeCommand('vscode.diff', uri, doc.uri)
    }
  )

  client: LanguageClient | undefined
  private isRestarting: boolean = false

  path: PathConfigObject = new PathConfigObject(
    {
      description: 'Path to the slang-server (not slang)',
    },
    {
      windows: 'slang-server.exe',
      linux: 'slang-server',
      mac: 'slang-server',
    }
  )

  args: ConfigObject<string[]> = new ConfigObject({
    default: [],
    description:
      'Arguments to pass to the slang-server. These are different from slang flags; for those open `.slang/server.json`',
  })

  debugArgs: ConfigObject<string[]> = new ConfigObject({
    default: [],
    description: 'Arguments to pass to slang-server when debugging',
  })

  /// The final config from slang-server json files
  slangConfig: slang.Config = {}

  async setupLanguageClient(): Promise<void> {
    this.logger.info('starting language server')

    // Check for environment variable set in launch.json when debugging in vscode
    let slangServerPath = process.env.SLANG_SERVER_PATH
    if (!slangServerPath) {
      slangServerPath = await this.path.getValueAsync()
    } else {
      this.logger.info(`Using slang-server from environment variable: ${slangServerPath}`)
    }

    if (slangServerPath === '') {
      await vscode.window.showErrorMessage(
        `"slang.path not configured. Configure the abs path at slang.path, add to PATH, or disable in config.`
      )
      return
    }
    // check if it exists
    const exists = await fileExists(slangServerPath)
    if (!exists) {
      vscode.window.showErrorMessage(
        `File "${slangServerPath}" set for slang.path doesn't exist, please reconfigure`
      )
      return
    }
    // this.logger.info("using path " + slangServerPath)
    const serverOptions: ServerOptions = {
      run: { command: slangServerPath, args: this.args.getValue() },
      debug: { command: slangServerPath, args: this.debugArgs.getValue() },
    }

    const clientOptions: LanguageClientOptions = {
      documentSelector: anyVerilogSelector,
    }

    this.client = new LanguageClient('slang-server', serverOptions, clientOptions)
    this.context.subscriptions.push(
      this.client.onDidChangeState(({ oldState, newState }) => {
        if (newState === vscodelc.State.Running) {
          // clangd starts or restarts after crash.
          this.client!.onNotification('slang/setConfig', (config: slang.Config) => {
            // Set after initialization
            this.slangConfig = config
          })
        } else if (
          oldState === vscodelc.State.Running &&
          newState === vscodelc.State.Stopped &&
          !this.isRestarting
        ) {
          vscode.window
            .showErrorMessage(
              'Slang language server has crashed. You can restart it using the "Restart Language Server" command.',
              'Restart Now'
            )
            .then((selection) => {
              if (selection === 'Restart Now') {
                this.restartLanguageServer.func()
              }
            })
        }
      })
    )

    await this.client.start()
    this.logger.info('language server started')
    await this.project.onStart()
  }

  restartLanguageServer: CommandNode = new CommandNode(
    {
      title: 'Restart Language Server',
    },
    async () => {
      this.isRestarting = true
      try {
        if (this.client !== undefined && this.client.isRunning()) {
          await this.client.restart()
          await this.project.onStart()
          this.logger.info('"' + this.client.name + '" language server restarted')
        } else {
          await this.setupLanguageClient()
        }
        // This needs to be after to ensure that the server is up
        await this.project.clearTopLevel.func()
        await this.showOutput.func()
      } finally {
        this.isRestarting = false
      }
    }
  )

  showOutput: CommandNode = new CommandNode(
    {
      title: 'Show Output',
    },
    async () => {
      if (this.client !== undefined) {
        this.client.outputChannel.show()
      }
    }
  )

  async stopServer() {
    if (this.client !== undefined) {
      await this.client.stop()
      this.client = undefined
    }
  }

  context: vscode.ExtensionContext

  constructor(context: vscode.ExtensionContext, obj: ViewContainerSpec) {
    super(obj)
    this.context = context
  }

  async activate(context: vscode.ExtensionContext) {
    context.subscriptions.push(
      this.onConfigUpdated(async () => {
        this.isRestarting = true
        try {
          if (this.client !== undefined) {
            await this.client.stop()
          }
          await this.setupLanguageClient()
        } finally {
          this.isRestarting = false
        }
      })
    )
    await this.setupLanguageClient()

    if (context.storageUri !== undefined) {
      this.expandDir = vscode.Uri.joinPath(context.storageUri, '.sv_cache', 'expanded')
    }

    /////////////////////////////////////////////
    // Configure Format on save
    /////////////////////////////////////////////

    this.onConfigUpdated(() => {
      void this.checkFormatDirs()
    })
    await this.checkFormatDirs()

    this.logger.info(`${context.extension.id} activation finished.`)
  }

  private async checkFormatDirs() {
    let dirs = this.formatDirs.getValue()
    this.svFormat.activateFormatter(dirs, ['sv', 'svh'], 'systemverilog')
    this.verilogFormat.activateFormatter(dirs, ['v', 'vh'], 'verilog')
  }

  public async findFiles(globs: string[], useGlob = false): Promise<vscode.Uri[]> {
    let ws = getWorkspaceFolder()
    if (ws === undefined) {
      return []
    }

    // TODO: maybe use this.slangConfig.exclude.excludeDirs

    const find = async (str: string): Promise<vscode.Uri[]> => {
      let ret: vscode.Uri[]
      if (path.isAbsolute(str)) {
        ret = (await glob(str)).map((p) => vscode.Uri.file(p))
      } else if (useGlob) {
        ret = (await glob(path.join(ws, str))).map((p) => vscode.Uri.file(p))
      } else {
        ret = await vscode.workspace.findFiles(new vscode.RelativePattern(ws, str))
      }
      return ret
    }
    let uriList: vscode.Uri[][] = await Promise.all(globs.map(find))
    let uris = uriList.reduce((acc, curr) => acc.concat(curr), [])
    return uris
  }
}

export async function deactivate(): Promise<void> {
  await ext.stopServer()
  ext.logger.info('Deactivated')
}

export async function activate(context: vscode.ExtensionContext) {
  ext = new SlangExtension(context, {
    id: 'slang',
    title: 'Slang',
    icon: '$(chip)',
  })
  await ext.activateExtension('slang', 'Slang', context, [
    'AndrewNolte.vscode-system-verilog',
    'AndrewNolte.vscode-slang',
    'mshr-h.veriloghdl',
    'eirikpre.systemverilog',
    'IMCTradingBV.svlangserver',
  ])
}
