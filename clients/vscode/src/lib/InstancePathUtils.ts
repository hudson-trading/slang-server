// Matches dotted instance paths like "top.sub.module" or "top.sub[0].module[1]"
// but not file paths (containing /) or SystemVerilog file extensions (.sv, .v, etc.)

// Lookbehind rejects word chars, '.', '/', '\' — ensuring we match from the
// start of an identifier, not mid-word or mid-file-path.
// Lookahead also rejects continuations like `.foo` or `/child`, so we don't
// backtrack to a shorter dotted prefix of a longer file or path string.
const RE_INSTANCE_PATHS = /(?<![/\\\w$.])[\w$]+(\[\d+\])?(\.[\w$]+(\[\d+\])?)+(?![\w$./\\[])/g
const RE_SV_EXTENSION = /\.(sv|svh|v|vh)$/i

export interface InstancePathMatch {
  path: string
  index: number
}

export interface HierarchyLookupChild {
  instName: string
  path: string
}

export interface ResolvedHierarchyChild<T extends HierarchyLookupChild> {
  child: T | undefined
  nextIndex: number
}

// Extract hierarchy-like instance paths from free-form text such as log lines.
export function findInstancePaths(line: string): InstancePathMatch[] {
  RE_INSTANCE_PATHS.lastIndex = 0
  const results: InstancePathMatch[] = []
  for (const match of line.matchAll(RE_INSTANCE_PATHS)) {
    const startIndex = match.index
    if (startIndex === undefined) {
      continue
    }
    if (RE_SV_EXTENSION.test(match[0])) {
      continue
    }
    results.push({ path: match[0], index: startIndex })
  }
  return results
}

// Split a scope path into parts, handling array indices and package-qualified
// names such as `pkg::member`.
export function splitHierarchyPath(path: string): string[] {
  const parts = []
  let current = ''
  for (let i = 0; i < path.length; i++) {
    const ch = path[i]
    const nextCh = path[i + 1]

    if (ch === '.' || (ch === ':' && nextCh === ':')) {
      if (current.length > 0) {
        parts.push(current)
        current = ''
      }
      if (ch === ':') {
        i++
      }
      continue
    }

    if (ch === '[' && current.length > 0) {
      parts.push(current)
      current = ''
    }

    current += ch
    if (ch === ']') {
      parts.push(current)
      current = ''
    }
  }

  if (current.length > 0) {
    parts.push(current)
  }

  return parts
}

// Match the next hierarchy segment against child names, tolerating both
// `foo` -> `[7]` and `foo[7]` naming schemes for generate and instance arrays.
export function resolveHierarchyChild<T extends HierarchyLookupChild>(
  children: readonly T[],
  parts: readonly string[],
  index: number,
  lastResolvedPath?: string
): ResolvedHierarchyChild<T> {
  const part = parts[index]
  if (!part) {
    return { child: undefined, nextIndex: index }
  }

  let child = children.find((candidate) => candidate.instName === part)
  if (child) {
    return { child, nextIndex: index }
  }

  const nextPart = parts[index + 1]
  if (nextPart?.startsWith('[')) {
    child = children.find((candidate) => candidate.instName === `${part}${nextPart}`)
    if (child) {
      return { child, nextIndex: index + 1 }
    }
  }

  if (part.startsWith('[') && lastResolvedPath) {
    child = children.find((candidate) => candidate.path === `${lastResolvedPath}${part}`)
    if (child) {
      return { child, nextIndex: index }
    }
  }

  return { child: undefined, nextIndex: index }
}
