import * as vscode from 'vscode'

/* Wrapper for vscode.LogOutputChannel.
Example usage:
  let logger: Logger = createLogger("Verilog");
  logger.info("Info message");
  let child_logger = logger.getChild("ChildA");
  child_logger.info("Message from child");
  -> The output would be
  Info message
  [ChildA] Message from child"
*/

export interface Logger {
  getChild(name: string): Logger

  trace(message: string, _data?: unknown): void

  info(message: string, _data?: unknown): void

  debug(message: string, _data?: unknown): void

  warn(message: string, _data?: unknown): void

  error(message: string, _data?: unknown): void

  show(): void
}

export class StubLogger implements Logger {
  getChild(_name: string): Logger {
    return new StubLogger()
  }

  trace(_message: string, _data?: unknown): void {}

  info(_message: string, _data?: unknown): void {}

  debug(_message: string, _data?: unknown): void {}

  warn(_message: string, _data?: unknown): void {}

  error(_message: string, _data?: unknown): void {}

  show(): void {}
}
export class OutputLogger {
  private name: string
  private parentLogger: vscode.LogOutputChannel | Logger

  constructor(name: string, parentLogger: vscode.LogOutputChannel | Logger) {
    this.name = name
    this.parentLogger = parentLogger
  }

  getChild(name: string): Logger {
    return new OutputLogger(name, this)
  }

  private log(level: keyof Logger, message: string, data?: unknown): void {
    let formattedMessage =
      this.parentLogger instanceof OutputLogger ? `[${this.name}] ${message}` : `${message}`
    if (data) {
      formattedMessage += JSON.stringify(data)
    }

    // @ts-ignore
    this.parentLogger[level](formattedMessage)
  }

  trace(message: string, data?: unknown): void {
    this.log('trace', message, data)
  }

  info(message: string, data?: unknown): void {
    this.log('info', message, data)
  }

  debug(message: string, data?: unknown): void {
    this.log('debug', message, data)
  }

  warn(message: string, data?: unknown): void {
    this.log('warn', message, data)
  }

  error(message: string, data?: unknown): void {
    this.log('error', message, data)
  }

  show(): void {
    this.parentLogger.show()
  }
}

export function createLogger(name: string): Logger {
  return new OutputLogger(name, vscode.window.createOutputChannel(name, { log: true }))
}
