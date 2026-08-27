import * as fs from 'fs'
import * as path from 'path'

const { runTests } = require('@vscode/test-electron') as {
  runTests(options: {
    extensionDevelopmentPath: string
    extensionTestsPath: string
    launchArgs: string[]
    extensionTestsEnv: Record<string, string>
    version: string
  }): Promise<number>
}

async function main() {
  const extensionDevelopmentPath = path.resolve(__dirname, '../..')
  const extensionTestsPath = path.resolve(__dirname, 'suite')
  const workspacePath = path.resolve(extensionDevelopmentPath, 'integration/workspace')
  const serverPath = path.resolve(
    process.env.SLANG_SERVER_PATH ??
      path.join(extensionDevelopmentPath, '../../build/bin/slang-server')
  )

  if (!fs.existsSync(serverPath)) {
    throw new Error(`slang-server binary not found at ${serverPath}`)
  }

  await runTests({
    extensionDevelopmentPath,
    extensionTestsPath,
    launchArgs: [workspacePath, '--disable-extensions'],
    extensionTestsEnv: {
      SLANG_SERVER_PATH: serverPath,
      SLANG_SERVER_TESTS: 'YES',
    },
    version: '1.96.2',
  })
}

main().catch((error: unknown) => {
  console.error(error)
  process.exitCode = 1
})
