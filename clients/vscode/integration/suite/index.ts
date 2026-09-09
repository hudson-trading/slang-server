import assert from 'assert'
import * as vscode from 'vscode'

const extensionId = 'Hudson-River-Trading.vscode-slang'
const timeoutMs = 15_000

async function waitFor(description: string, predicate: () => boolean): Promise<void> {
  const deadline = Date.now() + timeoutMs
  while (!predicate()) {
    if (Date.now() >= deadline) {
      throw new Error(`Timed out waiting for ${description}`)
    }
    await new Promise((resolve) => setTimeout(resolve, 25))
  }
}

async function executeAndExpectLocation(
  command: string,
  args: unknown,
  uri: vscode.Uri,
  line: number
): Promise<void> {
  await vscode.commands.executeCommand(command, args)
  await waitFor(`${command} to select line ${line + 1}`, () => {
    const editor = vscode.window.activeTextEditor
    return (
      editor?.document.uri.toString() === uri.toString() && editor.selection.start.line === line
    )
  })
}

export async function run(): Promise<void> {
  const workspace = vscode.workspace.workspaceFolders?.[0]
  assert.ok(workspace, 'test workspace was not opened')

  const extension = vscode.extensions.getExtension(extensionId)
  assert.ok(extension, `${extensionId} was not loaded`)
  await extension.activate()

  const uri = vscode.Uri.joinPath(workspace.uri, 'hierarchy.sv')
  const document = await vscode.workspace.openTextDocument(uri)
  const editor = await vscode.window.showTextDocument(document)
  const lines = document.getText().split('\n')
  const moduleLine = lines.findIndex((line) => line.includes('module child'))
  const instanceLine = lines.findIndex((line) => line.includes('child u_child'))
  const nestedInstanceLine = lines.findIndex((line) => line.includes('leaf nested_leaf'))
  const generatedInstanceLine = lines.findIndex((line) => line.includes('leaf generated_leaf'))
  assert.notEqual(moduleLine, -1)
  assert.notEqual(instanceLine, -1)
  assert.notEqual(nestedInstanceLine, -1)
  assert.notEqual(generatedInstanceLine, -1)

  try {
    await vscode.commands.executeCommand('slang.setTopLevel', uri.fsPath)

    editor.selection = new vscode.Selection(instanceLine, 0, instanceLine, 0)
    await executeAndExpectLocation(
      'slang.showModuleDefinition',
      { moduleName: 'child', takeFocus: true },
      uri,
      moduleLine
    )

    editor.selection = new vscode.Selection(moduleLine, 0, moduleLine, 0)
    await executeAndExpectLocation(
      'slang.showHierLocation',
      { hierPath: 'top.u_child', takeFocus: true },
      uri,
      instanceLine
    )

    await executeAndExpectLocation(
      'slang.project.setInstance',
      'top.branch_array[0].nested_leaf',
      uri,
      nestedInstanceLine
    )
    await executeAndExpectLocation(
      'slang.project.setInstance',
      'top.generated[1].generated_leaf',
      uri,
      generatedInstanceLine
    )

    const edit = new vscode.WorkspaceEdit()
    edit.insert(
      uri,
      new vscode.Position(instanceLine, 0),
      '// unsaved shift 1\n// unsaved shift 2\n'
    )
    assert.equal(await vscode.workspace.applyEdit(edit), true)
    assert.equal(document.isDirty, true)

    editor.selection = new vscode.Selection(moduleLine, 0, moduleLine, 0)
    await executeAndExpectLocation(
      'slang.showHierLocation',
      { hierPath: 'top.u_child', takeFocus: true },
      uri,
      instanceLine + 2
    )

    const scope = await vscode.commands.executeCommand<unknown[]>('slang.getScope', 'top')
    assert.ok(Array.isArray(scope) && scope.length > 0, 'language server stopped responding')
  } finally {
    if (document.isDirty) {
      await vscode.commands.executeCommand('workbench.action.files.revert')
    }
    await vscode.commands.executeCommand('workbench.action.closeAllEditors')
  }
}
