export interface HierarchyLookupChild {
  instName: string
  path: string
}

export interface ResolvedHierarchyChild<T extends HierarchyLookupChild> {
  child: T | undefined
  nextIndex: number
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
