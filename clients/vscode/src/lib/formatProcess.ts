// SPDX-License-Identifier: MIT

import * as child_process from 'child_process'

/// Default time limit for an external formatter run.
export const DEFAULT_FORMATTER_TIMEOUT_MS = 2000

/// Delay between asking a stopped formatter to terminate and forcing it.
const KILL_GRACE_MS = 500

export type FormatProcessResult =
  | { kind: 'ok'; stdout: string }
  | { kind: 'failed'; message: string }
  | { kind: 'timeout' }
  | { kind: 'cancelled' }

export interface FormatProcessOptions {
  cwd?: string
  /// Milliseconds before the formatter is stopped; 0 or less disables the limit.
  timeoutMs: number
  /// Aborting stops the formatter and resolves with 'cancelled'.
  signal?: AbortSignal
}

/// Run a formatter without blocking: `input` goes to stdin and stdout is the formatted text.
/// A non-zero exit, any stderr output, or empty stdout counts as a failure.
export function runFormatterProcess(
  binPath: string,
  args: string[],
  input: string,
  options: FormatProcessOptions
): Promise<FormatProcessResult> {
  return new Promise((resolve) => {
    if (options.signal?.aborted) {
      resolve({ kind: 'cancelled' })
      return
    }

    let child: child_process.ChildProcess
    try {
      child = child_process.spawn(binPath, args, {
        cwd: options.cwd,
        stdio: ['pipe', 'pipe', 'pipe'],
      })
    } catch (err) {
      resolve({ kind: 'failed', message: (err as Error).message })
      return
    }

    let settled = false
    let stopReason: 'timeout' | 'cancelled' | undefined
    let killTimer: ReturnType<typeof setTimeout> | undefined
    let timeoutTimer: ReturnType<typeof setTimeout> | undefined
    const stdout: Buffer[] = []
    const stderr: Buffer[] = []

    const finish = (result: FormatProcessResult) => {
      if (settled) {
        return
      }
      settled = true
      clearTimeout(timeoutTimer)
      options.signal?.removeEventListener('abort', onAbort)
      resolve(result)
    }

    const stop = (reason: 'timeout' | 'cancelled') => {
      if (settled || stopReason !== undefined) {
        return
      }
      stopReason = reason
      child.kill('SIGTERM')
      killTimer = setTimeout(() => {
        if (child.exitCode === null && child.signalCode === null) {
          child.kill('SIGKILL')
        }
      }, KILL_GRACE_MS)
      killTimer.unref()
      // Resolve now rather than waiting for the process to exit.
      finish({ kind: reason })
    }

    const onAbort = () => stop('cancelled')
    options.signal?.addEventListener('abort', onAbort)
    if (options.timeoutMs > 0) {
      timeoutTimer = setTimeout(() => stop('timeout'), options.timeoutMs)
    }

    child.stdout?.on('data', (chunk: Buffer) => stdout.push(chunk))
    child.stderr?.on('data', (chunk: Buffer) => stderr.push(chunk))
    // A formatter that exits without reading all of stdin raises EPIPE here; its exit status decides.
    child.stdin?.on('error', () => {})

    child.on('error', (err) => {
      finish({ kind: 'failed', message: err.message })
    })

    child.on('close', (code, signal) => {
      if (killTimer !== undefined) {
        clearTimeout(killTimer)
      }
      const out = Buffer.concat(stdout).toString('utf-8')
      const err = Buffer.concat(stderr).toString('utf-8')
      if (code !== 0) {
        const status = code === null ? `signal ${signal}` : `exit code ${code}`
        finish({
          kind: 'failed',
          message: err.length > 0 ? err : `Formatter exited with ${status}`,
        })
      } else if (err.length > 0) {
        finish({ kind: 'failed', message: err })
      } else if (out.length === 0) {
        finish({ kind: 'failed', message: 'Formatter produced no output' })
      } else {
        finish({ kind: 'ok', stdout: out })
      }
    })

    child.stdin?.end(input, 'utf-8')
  })
}
