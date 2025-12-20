import * as fs from 'fs'
import * as http from 'http'
import * as path from 'path'

export function withFakeGitHub(assetsRoot: string, body: () => Promise<void>): Promise<void> {
  return new Promise((resolve, reject) => {
    const server = http
      .createServer(async (req, res) => {
        if (!req.url) {
          res.statusCode = 400
          res.end()
          return
        }

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
          server.close()
        }
      })
  })
}
