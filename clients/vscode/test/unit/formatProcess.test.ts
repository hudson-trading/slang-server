import tape from 'tape'
import { runFormatterProcess } from '../../src/lib/formatProcess'

const node = process.execPath

function script(source: string): string[] {
  return ['-e', source]
}

tape('runFormatterProcess returns formatted stdout', async (assert) => {
  const result = await runFormatterProcess(
    node,
    script('process.stdin.pipe(process.stdout)'),
    'module m;\nendmodule\n',
    { timeoutMs: 5000 }
  )
  assert.deepEqual(result, { kind: 'ok', stdout: 'module m;\nendmodule\n' })
  assert.end()
})

tape('runFormatterProcess handles input larger than a pipe buffer', async (assert) => {
  const input = 'x'.repeat(1 << 20)
  const result = await runFormatterProcess(
    node,
    script('process.stdin.pipe(process.stdout)'),
    input,
    {
      timeoutMs: 10000,
    }
  )
  assert.equal(result.kind, 'ok')
  assert.equal(result.kind === 'ok' && result.stdout.length, input.length)
  assert.end()
})

tape('runFormatterProcess reports stderr and exit codes as failures', async (assert) => {
  const stderr = await runFormatterProcess(
    node,
    script('process.stdout.write("x"); process.stderr.write("bad syntax")'),
    '',
    { timeoutMs: 5000 }
  )
  assert.deepEqual(stderr, { kind: 'failed', message: 'bad syntax' })

  const exit = await runFormatterProcess(node, script('process.exit(3)'), '', { timeoutMs: 5000 })
  assert.deepEqual(exit, { kind: 'failed', message: 'Formatter exited with exit code 3' })

  const empty = await runFormatterProcess(node, script(''), 'input', { timeoutMs: 5000 })
  assert.deepEqual(empty, { kind: 'failed', message: 'Formatter produced no output' })
  assert.end()
})

tape('runFormatterProcess reports a missing binary', async (assert) => {
  const result = await runFormatterProcess('slang-server-no-such-formatter', [], '', {
    timeoutMs: 5000,
  })
  assert.equal(result.kind, 'failed')
  assert.end()
})

tape('runFormatterProcess stops a formatter that exceeds the timeout', async (assert) => {
  const start = Date.now()
  const result = await runFormatterProcess(node, script('setTimeout(() => {}, 60000)'), '', {
    timeoutMs: 200,
  })
  assert.deepEqual(result, { kind: 'timeout' })
  assert.ok(Date.now() - start < 5000, 'resolves promptly')
  assert.end()
})

tape('runFormatterProcess with timeout 0 waits for the formatter', async (assert) => {
  const result = await runFormatterProcess(
    node,
    script('setTimeout(() => process.stdout.write("done"), 300)'),
    '',
    { timeoutMs: 0 }
  )
  assert.deepEqual(result, { kind: 'ok', stdout: 'done' })
  assert.end()
})

tape('runFormatterProcess cancels a running formatter', async (assert) => {
  const abort = new AbortController()
  const run = runFormatterProcess(node, script('setTimeout(() => {}, 60000)'), '', {
    timeoutMs: 0,
    signal: abort.signal,
  })
  setTimeout(() => abort.abort(), 100)
  assert.deepEqual(await run, { kind: 'cancelled' })

  const preAborted = new AbortController()
  preAborted.abort()
  const skipped = await runFormatterProcess(node, script(''), '', {
    timeoutMs: 0,
    signal: preAborted.signal,
  })
  assert.deepEqual(skipped, { kind: 'cancelled' })
  assert.end()
})
