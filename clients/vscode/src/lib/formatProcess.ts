// SPDX-License-Identifier: MIT

import * as child_process from 'child_process'

/// Default time limit for an external formatter run.
export const DEFAULT_FORMATTER_TIMEOUT_MS = 2000

export type FormatProcessResult =
  | { kind: 'ok'; stdout: string }
  | { kind: 'failed'; message: string }
  | { kind: 'timeout' }
  | { kind: 'cancelled' }

export interface FormatProcessOptions {
  cwd?: string
  /// Milliseconds before the formatter is stopped; 0 disables the limit.
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

    // The formatter leads its own process group so that stopping it also stops anything a wrapper script started.
    const ownGroup = process.platform !== 'win32'
    let child: child_process.ChildProcess
    try {
      child = child_process.spawn(binPath, args, {
        cwd: options.cwd,
        stdio: ['pipe', 'pipe', 'pipe'],
        detached: ownGroup,
      })
    } catch (err) {
      resolve({ kind: 'failed', message: (err as Error).message })
      return
    }

    let settled = false
    let killTimer: ReturnType<typeof setTimeout> | undefined
    let timeoutTimer: ReturnType<typeof setTimeout> | undefined
    const stdout: Buffer[] = []
    const stderr: Buffer[] = []
    // Formatted text is about the size of the input, so far more than that means a runaway formatter.
    const maxOutputBytes = 2 * Buffer.byteLength(input) + (1 << 20)
    let outputBytes = 0

    const finish = (result: FormatProcessResult) => {
      if (settled) {
        return
      }
      settled = true
      clearTimeout(timeoutTimer)
      options.signal?.removeEventListener('abort', onAbort)
      resolve(result)
    }

    const kill = (signal: 'SIGTERM' | 'SIGKILL') => {
      try {
        if (ownGroup && child.pid !== undefined) {
          process.kill(-child.pid, signal)
        } else {
          child.kill(signal)
        }
      } catch {
        // The process group is already gone.
      }
    }

    // Resolves immediately rather than waiting for the process to exit.
    const stop = (result: FormatProcessResult) => {
      if (settled) {
        return
      }
      kill('SIGTERM')
      // Force anything that ignores the request for 500ms.
      killTimer = setTimeout(() => kill('SIGKILL'), 500)
      killTimer.unref()
      finish(result)
    }

    const onAbort = () => stop({ kind: 'cancelled' })
    options.signal?.addEventListener('abort', onAbort)
    if (options.timeoutMs > 0) {
      timeoutTimer = setTimeout(() => {
        // A formatter that already exited finished in time; 'close' delivers its result once the pipes drain.
        if (child.exitCode === null) {
          stop({ kind: 'timeout' })
        }
      }, options.timeoutMs)
    }

    const collect = (chunks: Buffer[]) => (chunk: Buffer) => {
      if (settled) {
        return
      }
      chunks.push(chunk)
      outputBytes += chunk.length
      if (outputBytes > maxOutputBytes) {
        stop({ kind: 'failed', message: 'Formatter produced too much output' })
      }
    }
    child.stdout?.on('data', collect(stdout))
    child.stderr?.on('data', collect(stderr))
    // A formatter that exits without reading all of stdin raises EPIPE here; its exit status decides.
    child.stdin?.on('error', () => {})

    child.on('error', (err) => {
      finish({ kind: 'failed', message: err.message })
    })

    child.on('close', (code, signal) => {
      clearTimeout(killTimer)
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
