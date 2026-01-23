import * as fs from 'fs'
import * as http from 'http'
import * as path from 'path'
import * as compressing from 'compressing'
import * as os from 'os'

export async function createFakeAssets(root: string, platform: 'linux' | 'windows' | 'mac') {
  await fs.promises.mkdir(root, { recursive: true })

  await fs.promises.copyFile(
    path.resolve(process.cwd(), 'test/unit/assets/release.json'),
    path.join(root, 'release.json')
  )

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

export function withFakeGitHub(assetsRoot: string, body: () => Promise<void>): Promise<void> {
  return new Promise((resolve, reject) => {
    const server = http
      .createServer(async (req, res) => {
        if (!req.url) {
          res.statusCode = 400
          res.end()
          return
        }

        res.setHeader('Connection', 'close')

        const { pathname } = new URL(req.url, 'http://localhost')
        const filePath = path.join(assetsRoot, pathname)

        try {
          const stat = await fs.promises.stat(filePath)
          res.setHeader('Content-Length', stat.size)
          fs.createReadStream(filePath).pipe(res)
        } catch {
          res.statusCode = 404
          res.end()
        }
      })
      .listen(9999, async () => {
        try {
          await body()
          resolve()
        } catch (e) {
          reject(e)
        } finally {
          await new Promise<void>((r) => server.close(() => r()))
        }
      })
  })
}
