import * as fs from 'fs'
import * as path from 'path'
import * as compressing from 'compressing'
import * as os from 'os'

export async function createFakeAssets(root: string, platform: 'linux' | 'windows' | 'mac') {
  await fs.promises.mkdir(root, { recursive: true })

  const workDir = await fs.promises.mkdtemp(path.join(os.tmpdir(), 'slang-'))

  async function makeTarGz(name: string, binName: string) {
    const dir = path.join(workDir, name)
    await fs.promises.mkdir(dir, { recursive: true })

    const binPath = path.join(dir, binName)
    await fs.promises.writeFile(binPath, '#!/bin/sh\necho slang\n')
    await fs.promises.chmod(binPath, 0o755)

    await compressing.tgz.compressFile(binPath, path.join(root, `${name}.tar.gz`))
  }

  async function makeZip(name: string, binName: string) {
    const dir = path.join(workDir, name)
    await fs.promises.mkdir(dir)

    const binPath = path.join(dir, binName)
    await fs.promises.writeFile(binPath, 'echo slang\r\n')

    await compressing.zip.compressFile(binPath, path.join(root, `${name}.zip`))
  }

  if (platform === 'windows') {
    await makeZip('slang-server-windows-x64', 'slang-server.exe')
  } else if (platform === 'mac') {
    await makeTarGz('slang-server-macos', 'slang-server')
  } else if (platform === 'linux') {
    await makeTarGz('slang-server-linux-x64-gcc', 'slang-server')
    await makeTarGz('slang-server-linux-x64-clang', 'slang-server')
  }
}
