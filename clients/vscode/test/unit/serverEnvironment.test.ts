import tape from 'tape'
import * as fs from 'fs'
import * as os from 'os'
import * as path from 'path'
import { loadServerEnvironment } from '../../src/lib/serverEnvironment'

tape('Server environment merges JSONC layers and reloads literal values', (assert) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'slang-env-'))
  const home = path.join(root, 'user')
  const errors: string[] = []
  const write = (base: string, file: string, text: string) => {
    const target = path.join(base, '.slang', file)
    fs.mkdirSync(path.dirname(target), { recursive: true })
    fs.writeFileSync(target, text)
  }
  try {
    assert.deepEqual(
      loadServerEnvironment(root, home, (e) => errors.push(e)),
      {}
    )
    write(
      root,
      'server.json',
      `{
      // Workspace defaults
      "env": {"SHARED": "workspace", "WORKSPACE_ONLY": "kept", "EMPTY": "",},
    }`
    )
    write(home, 'server.json', '{"env":{"SHARED":"user","USER_ONLY":"kept"}}')
    write(
      root,
      'local/server.json',
      `{
      "env": {"SHARED": "local", "LITERAL": "https://example.test/* \\\" ,} $VAR \\u263a"}, /* comment */
    }`
    )
    assert.deepEqual(
      loadServerEnvironment(root, home, (e) => errors.push(e)),
      {
        SHARED: 'local',
        WORKSPACE_ONLY: 'kept',
        USER_ONLY: 'kept',
        EMPTY: '',
        LITERAL: 'https://example.test/* " ,} $VAR ☺',
      }
    )
    write(root, 'local/server.json', '{"env":{}}')
    assert.equal(loadServerEnvironment(root, home, (e) => errors.push(e)).SHARED, 'user')
    fs.unlinkSync(path.join(home, '.slang/server.json'))
    assert.equal(loadServerEnvironment(root, home, (e) => errors.push(e)).SHARED, 'workspace')
    assert.deepEqual(
      loadServerEnvironment(undefined, home, (e) => errors.push(e)),
      {}
    )
    assert.deepEqual(errors, [])
  } finally {
    fs.rmSync(root, { recursive: true, force: true })
  }
  assert.end()
})

tape(
  'Invalid server environments report their file and do not apply partial overrides',
  (assert) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'slang-env-'))
    const file = path.join(root, '.slang/server.json')
    fs.mkdirSync(path.dirname(file))
    try {
      for (const invalid of [
        '{',
        '{,}',
        '{"env":{"A":"ok",,}}',
        '[]',
        '{"env":null}',
        '{"env":[]}',
        '{"env":{"OK":"x","BAD":42}}',
        '{"env":{"BAD=NAME":"x"}}',
        '{"env":{"BAD":"\\u0000"}}',
        '{"env":{}} /* unfinished',
      ]) {
        fs.writeFileSync(file, invalid)
        const errors: string[] = []
        assert.deepEqual(
          loadServerEnvironment(root, undefined, (e) => errors.push(e)),
          {}
        )
        assert.equal(errors.length, 1)
        assert.ok(errors[0].includes(file))
      }
    } finally {
      fs.rmSync(root, { recursive: true, force: true })
    }
    assert.end()
  }
)
