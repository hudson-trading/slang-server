import tape from 'tape'
import * as fs from 'fs'
import * as os from 'os'
import * as path from 'path'

import { findWorkspaceFiles } from '../../src/lib/findWorkspaceFiles'

tape('build-file search finds .f files in hidden workspace directories', async (t) => {
  const workspace = await fs.promises.mkdtemp(path.join(os.tmpdir(), 'slang-build-files-'))
  try {
    await fs.promises.mkdir(path.join(workspace, '.slang'))
    await fs.promises.mkdir(path.join(workspace, 'build'))
    await fs.promises.writeFile(path.join(workspace, '.slang', 'hidden.f'), '')
    await fs.promises.writeFile(path.join(workspace, 'build', 'visible.f'), '')

    const files = await findWorkspaceFiles(workspace, ['**/*.f'])
    t.deepEqual(
      files.map((file) => path.relative(workspace, file)).sort(),
      [path.join('.slang', 'hidden.f'), path.join('build', 'visible.f')],
      'default build pattern includes visible and hidden workspace directories'
    )
  } finally {
    await fs.promises.rm(workspace, { recursive: true, force: true })
  }
  t.end()
})

tape('build-file search skips Git metadata', async (t) => {
  const workspace = await fs.promises.mkdtemp(path.join(os.tmpdir(), 'slang-build-files-'))
  try {
    await fs.promises.mkdir(path.join(workspace, '.git'))
    await fs.promises.mkdir(path.join(workspace, '.slang'))
    await fs.promises.writeFile(path.join(workspace, '.git', 'metadata.f'), '')
    await fs.promises.writeFile(path.join(workspace, '.slang', 'build.f'), '')

    const files = await findWorkspaceFiles(workspace, ['**/*.f'])
    t.deepEqual(
      files.map((file) => path.relative(workspace, file)),
      [path.join('.slang', 'build.f')],
      'only the build file appears in the picker'
    )
  } finally {
    await fs.promises.rm(workspace, { recursive: true, force: true })
  }
  t.end()
})
