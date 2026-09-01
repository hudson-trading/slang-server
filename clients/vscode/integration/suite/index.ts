import assert from 'assert'
import * as vscode from 'vscode'

const extensionId = 'Hudson-River-Trading.vscode-slang'
const timeoutMs = 15_000

interface HierarchyItem {
  inst: { instName: string }
}

interface ModuleViewItem {
  data: { declName?: string; instPath?: string }
}

interface InstancesView {
  getChildren(item?: ModuleViewItem): Promise<ModuleViewItem[]>
  revealPath(moduleName: string, instancePath: string): Promise<void>
}

interface ProjectComponent {
  includeData: boolean
  includeParams: boolean
  instancesView: InstancesView
  top: HierarchyItem | undefined
  getChildren(item?: HierarchyItem): Promise<HierarchyItem[]>
  getTreeItem(item: HierarchyItem): Promise<vscode.TreeItem>
}

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
  const extensionApi = (await extension.activate()) as { project: ProjectComponent }

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
    await vscode.commands.executeCommand('slang.project.setTopLevel', uri)

    const project = extensionApi.project
    assert.equal(project.includeData, true, 'data signals should be visible by default')
    assert.ok(project.top, 'top-level instance was not populated')
    const topChildren = await project.getChildren(project.top)
    assert.ok(
      topChildren.some((item) => item.inst.instName === 'data_signal'),
      'default hierarchy should include data signals'
    )

    for (const name of ['u_child', 'params_only']) {
      const item = topChildren.find((child) => child.inst.instName === name)
      assert.ok(item, `missing hierarchy item ${name}`)
      assert.equal(
        (await project.getTreeItem(item)).collapsibleState,
        vscode.TreeItemCollapsibleState.None,
        `${name} should not be expandable without visible children`
      )
    }

    const emptyPackage = (await project.getChildren()).find(
      (item) => item.inst.instName === 'empty_pkg'
    )
    assert.ok(emptyPackage, 'missing empty package')
    assert.equal(
      (await project.getTreeItem(emptyPackage)).collapsibleState,
      vscode.TreeItemCollapsibleState.None,
      'empty package should not be expandable'
    )

    assert.equal(project.includeParams, false, 'module parameters should be hidden by default')
    const paramsPackage = (await project.getChildren()).find(
      (item) => item.inst.instName === 'params_pkg'
    )
    assert.ok(paramsPackage, 'missing parameter package')
    assert.ok(
      (await project.getChildren(paramsPackage)).some(
        (item) => item.inst.instName === 'PACKAGE_PARAM'
      ),
      'package parameters should remain visible'
    )

    const modules = await project.instancesView.getChildren()
    const childModule = modules.find((item) => item.data.declName === 'child')
    assert.ok(childModule, 'missing child module in Modules view')
    const childInstance = (await project.instancesView.getChildren(childModule)).find(
      (item) => item.data.instPath === 'top.u_child'
    )
    assert.ok(childInstance, 'missing top.u_child in Modules view')

    const revealCalls: [string, string][] = []
    const revealPath = project.instancesView.revealPath.bind(project.instancesView)
    project.instancesView.revealPath = async (moduleName, instancePath) => {
      revealCalls.push([moduleName, instancePath])
      await revealPath(moduleName, instancePath)
    }
    try {
      await vscode.commands.executeCommand(
        'slang.project.instancesView.gotoInstantiation',
        childInstance
      )
    } finally {
      project.instancesView.revealPath = revealPath
    }
    assert.deepEqual(
      revealCalls[revealCalls.length - 1],
      ['top', 'top'],
      'Go to Instantiation should reveal the parent module instance'
    )

    const lenses = await vscode.commands.executeCommand<vscode.CodeLens[]>(
      'vscode.executeCodeLensProvider',
      uri
    )
    const gotoInstantiation = lenses.find((lens) => {
      const params = lens.command?.arguments?.[0] as { hierPath?: string } | undefined
      return lens.command?.title === 'Go to Instantiation' && params?.hierPath === 'top.u_child'
    })
    assert.ok(gotoInstantiation?.command, 'missing child module CodeLens')
    editor.selection = new vscode.Selection(moduleLine, 0, moduleLine, 0)
    await executeAndExpectLocation(
      gotoInstantiation.command.command,
      gotoInstantiation.command.arguments?.[0],
      uri,
      instanceLine
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
