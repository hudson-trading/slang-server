import { ConfigObject, ExtensionComponent, PathConfigObject } from './libconfig'

export class Runner extends ExtensionComponent {
  runAtFileLocation: ConfigObject<boolean> = new ConfigObject({
    default: false,
    description: 'Run at file location',
  })
}

export class ToolConfig extends Runner {
  path: PathConfigObject
  args: ConfigObject<string> = new ConfigObject({
    default: '',
    description: 'Arguments to pass to the tool',
  })
  toolName: string
  constructor(toolName: string) {
    super()
    this.toolName = toolName
    this.path = new PathConfigObject(
      {
        description: 'Path to the tool',
      },
      {
        windows: `${toolName}.exe`,
        linux: toolName,
        mac: toolName,
      }
    )
  }
}

export class ToggleToolConfig extends ToolConfig {
  enabled: ConfigObject<boolean> = new ConfigObject({
    default: false,
    description: 'Enable this tool',
  })
}
