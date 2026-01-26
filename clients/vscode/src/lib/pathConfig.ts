import * as fs from 'fs'
import * as fsPromises from 'fs/promises'
import * as path from 'path'
import * as process from 'process'
import * as vscode from 'vscode'
import which from 'which'
import { ConfigObject, ExtensionComponent } from './libconfig'
import { Logger, StubLogger } from './logger'
import { PlatformMap, getPlatform } from './platform'
import { IConfigurationPropertySchema } from './vscodeConfigs'
import {
  installFromGithub,
  latestRelease,
  GithubInstallerConfig,
  isUpdateAvailable,
} from './install'

export interface PathConfigOptions {
  envVar?: string // Environment variable to check first (e.g., 'SLANG_SERVER_PATH')
  installer?: GithubInstallerConfig // GitHub release installer config
}

type PathConfigSchema = Omit<IConfigurationPropertySchema, 'default'>

export class PathConfigObject extends ConfigObject<string> {
  platformDefaults: PlatformMap
  private options: PathConfigOptions

  /** True if the binary was installed/managed by the extension (not found on PATH or user-configured) */
  managedInstall: boolean = false

  constructor(
    obj: PathConfigSchema,
    platformDefaults: PlatformMap,
    options: PathConfigOptions = {}
  ) {
    super({
      ...obj,
      default: '',
    })
    this.platformDefaults = platformDefaults
    this.options = options
  }

  compile(nodeName: string, parentNode?: ExtensionComponent | undefined): void {
    super.compile(nodeName, parentNode)
  }

  getValue(): string {
    let toolpath = vscode.workspace.getConfiguration().get(this.configPath!, '')
    if (toolpath === '') {
      return this.platformDefaults[getPlatform()]
    }
    return toolpath
  }

  async findToolPath(): Promise<string> {
    // get configured path from settings.json
    let toolpath = vscode.workspace.getConfiguration().get(this.configPath!, '')

    // path has not been configured in settings.json
    if (toolpath === '') {
      // start by checking to see if we have a cached value
      if (path.isAbsolute(this.cachedValue)) {
        return this.cachedValue
      }

      // if we don't have a cached value, then we check to see if its on the path
      toolpath = this.platformDefaults[getPlatform()]
      const whichResult = await which(toolpath, { nothrow: true })
      if (whichResult !== '' && whichResult !== null) {
        console.error(`which ${toolpath} found ${whichResult}`)
        toolpath = whichResult

        // we return early since we found it on the path and we don't
        // need to do further checks (like existance and directory)
        this.cachedValue = toolpath
        return toolpath
      } else {
        // not found on path
        console.error(`which ${toolpath} failed`)

        return ''
      }
    }

    this.cachedValue = toolpath

    try {
      const stats = await fs.promises.stat(toolpath)
      if (!stats.isFile()) {
        vscode.window.showErrorMessage(
          `File "${this.configPath}: ${toolpath}" is not a file, please reconfigure`
        )
      }
    } catch {
      // I believe it only throws if the file DNE
      // see: https://stackoverflow.com/a/53530146
      // probably would be good to verify this though.
      vscode.window.showErrorMessage(
        `File "${this.configPath}: ${toolpath}" doesn't exist, please reconfigure`
      )
    }

    return toolpath
  }

  getMarkdownString(): string {
    // Skip deprecated configs from documentation
    if ('deprecationMessage' in this.obj) {
      return ''
    }

    let out = `- \`${this.configPath}\`: path\n\n`
    out += `  Platform Defaults:\n\n`
    out += `    linux:   \`${this.platformDefaults.linux}\`\n\n`
    out += `    mac:     \`${this.platformDefaults.mac}\`\n\n`
    out += `    windows: \`${this.platformDefaults.windows}\`\n\n`
    return out
  }

  /**
   * Resolve the tool path by checking (in order):
   * 1. Environment variable for debugging (if configured)
   * 2. User settings / cached value / PATH
   * 3. Previously installed binary in extension storage
   * 4. GitHub release installer (if configured)
   */
  async resolveToolPath(
    context: vscode.ExtensionContext,
    logger?: Logger
  ): Promise<string | undefined> {
    const log = logger ?? new StubLogger()
    const config = this.options.installer
    const toolName = this.platformDefaults[getPlatform()]

    // 1. Check environment variable if configured
    if (this.options.envVar) {
      const envPath = process.env[this.options.envVar]
      if (envPath) {
        log.info(`Using ${toolName} from environment variable ${this.options.envVar}: ${envPath}`)
        return envPath
      }
    }

    log.info(`Finding ${toolName}...`)

    // 2. Check configured path / cached value / PATH
    const configuredPath = await this.findToolPath()
    if (configuredPath !== '') {
      log.info(`Using ${toolName} at ${configuredPath}`)
      return configuredPath
    }

    // 3. Check if already installed in extension storage
    if (config) {
      const storagePath = context.globalStorageUri.fsPath
      const installBase = path.join(storagePath, 'install')

      await fsPromises.mkdir(installBase, { recursive: true })

      // Look for any existing installation
      const existingBinary = await this.findExistingBinary(installBase)
      if (existingBinary !== null) {
        log.info(`Using previously installed ${toolName} at ${existingBinary}`)
        this.cachedValue = existingBinary
        this.managedInstall = true
        return existingBinary
      }

      // 4. Prompt user to install from GitHub
      try {
        const shouldInstall = await this.promptInstall(config.githubRepo)
        if (!shouldInstall) {
          await vscode.window.showErrorMessage(`${toolName} is required but was not installed.`)
          return undefined
        }

        const binaryPath = await vscode.window.withProgress(
          {
            location: vscode.ProgressLocation.Notification,
            title: `Installing ${toolName}...`,
            cancellable: false,
          },
          async () => {
            return installFromGithub(storagePath, config, this.platformDefaults)
          }
        )

        vscode.window.showInformationMessage(`Installed ${toolName} at ${binaryPath}`)
        this.cachedValue = binaryPath
        this.managedInstall = true
        log.info(`Installed ${toolName} at ${binaryPath}`)
        return binaryPath
      } catch (err: any) {
        await vscode.window.showErrorMessage(
          `Failed to install ${toolName}: ${err?.message ?? err}`
        )
        return undefined
      }
    }

    return undefined
  }

  private async findExistingBinary(root: string): Promise<string | null> {
    const binaryName = this.platformDefaults[getPlatform()]
    if (!binaryName) {
      return null
    }

    try {
      const entries = await fsPromises.readdir(root, { recursive: true })
      for (const e of entries) {
        if (e.endsWith(binaryName)) {
          return path.join(root, e)
        }
      }
    } catch {
      // Directory doesn't exist yet
    }
    return null
  }

  private async promptInstall(githubRepo: string): Promise<boolean> {
    const binaryName = this.platformDefaults[getPlatform()]
    const msg =
      `${binaryName} is required but was not found.\n` +
      `Would you like to install it from [${githubRepo}](https://github.com/${githubRepo}/releases)?`

    const install = `Install ${binaryName}`
    const resp = await vscode.window.showInformationMessage(msg, install)
    return resp === install
  }

  /**
   * Check for updates in the background without blocking startup.
   * If an update is available, prompts the user and offers to update.
   * @param context Extension context for storage path
   * @param logger Optional logger
   * @param installedVersion The currently installed version (avoids subprocess call)
   * @returns Promise that resolves to true if an update was installed
   */
  async maybeInstallUpdate(
    context: vscode.ExtensionContext,
    logger: Logger,
    installedVersion: string | null
  ): Promise<boolean> {
    const config = this.options.installer
    if (!config) {
      return false
    }

    const toolName = this.platformDefaults[getPlatform()]
    const storagePath = context.globalStorageUri.fsPath

    try {
      const release = await latestRelease(config)
      const needsUpdate =
        installedVersion === null || isUpdateAvailable(release.tag_name, installedVersion)

      if (!needsUpdate) {
        logger.info(`${toolName} is up to date (${installedVersion})`)
        return false
      }

      const binaryName = this.platformDefaults[getPlatform()]

      const update = `Update ${binaryName}`
      const resp = await vscode.window.showInformationMessage(
        `A newer version of ${binaryName} is available.\n\n` +
          `Installed: ${installedVersion ?? 'unknown'}\n` +
          `Latest: ${release.tag_name}`,
        update
      )

      if (resp !== update) {
        return false
      }

      const newBinaryPath = await vscode.window.withProgress(
        {
          location: vscode.ProgressLocation.Notification,
          title: `Updating ${toolName}...`,
          cancellable: false,
        },
        async () => {
          return installFromGithub(storagePath, config, this.platformDefaults)
        }
      )

      this.cachedValue = newBinaryPath
      logger.info(`Updated ${toolName} to ${release.tag_name} at ${newBinaryPath}`)

      return true
    } catch (err: any) {
      // Silently ignore update check failures (e.g., offline)
      logger.warn(`Background update check failed: ${err?.message ?? err}`)
      return false
    }
  }
}
