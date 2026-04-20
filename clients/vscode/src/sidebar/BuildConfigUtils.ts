import * as path from 'path'
import * as vscode from 'vscode'
import { Config } from '../config.gen'

export interface BuildFileParams {
  name?: string
  top?: string
}

export interface BuildCommandArgs {
  sourceFile: string
  selectionIndex: number
}

export interface BuildQuickPickItem extends vscode.QuickPickItem {
  type: 'filelist' | 'command'
  filePath?: string
  buildCommand?: BuildCommandArgs
}

// Fill `{name}` / `{top}`-style placeholders, leaving missing values as wildcards.
export function formatString(template: string, vars: BuildFileParams): string {
  return template.replace(/{(\w+)}/g, (_, key: string) => vars[key as keyof BuildFileParams] || '*')
}

// Normalize a path-ish label so it is safe and readable in generated filenames.
function sanitizeGeneratedBuildName(value: string): string {
  return value
    .replace(/[^A-Za-z0-9._/-]+/g, '_')
    .replace(/[\\/]+/g, '__')
    .replace(/_+/g, '_')
    .replace(/__+/g, '__')
    .replace(/^[_./-]+|[_./-]+$/g, '')
}

// Keep whole path components from the end until the generated stem hits the length limit.
function buildGeneratedStem(components: string[], maxLength: number): string {
  let stem = ''
  for (let i = components.length - 1; i >= 0; i--) {
    const component = components[i]
    if (!component) {
      continue
    }
    const candidate = stem ? `${component}__${stem}` : component
    if (candidate.length > maxLength) {
      break
    }
    stem = candidate
  }
  return stem
}

// Resolve executable-like tokens relative to the workspace while leaving flags untouched.
export function resolveCommandToken(token: string, workspaceFolder: string): string {
  if (path.isAbsolute(token)) {
    return path.normalize(token)
  }
  if (token.startsWith('-') || token.startsWith('+')) {
    return token
  }
  if (token.startsWith('.') || token.includes('/') || token.includes('\\')) {
    return path.join(workspaceFolder, token)
  }
  return token
}

// Derive a stable per-source output path that keeps as much of the source path as will fit.
export function getGeneratedBuildOutputPath(
  workspaceFolder: string,
  sourceFile: string,
  selectionIndex: number,
  buildName?: string
): string {
  const relativeSource = path.relative(workspaceFolder, sourceFile)
  const sourceKey = (
    relativeSource && !relativeSource.startsWith('..') ? relativeSource : sourceFile
  )
    .split(path.sep)
    .join('/')
  const sourceComponents = sourceKey
    .split('/')
    .map((component, index, all) =>
      sanitizeGeneratedBuildName(
        index === all.length - 1 ? component.replace(/\.[^/.]+$/, '') : component
      )
    )
    .filter(Boolean)
  const buildStem = sanitizeGeneratedBuildName(buildName?.trim() || '')
  const readableStem = buildGeneratedStem(
    [...(buildStem ? [buildStem] : []), ...sourceComponents],
    120
  )

  return path.join(
    workspaceFolder,
    '.slang',
    'local',
    'builds',
    `${readableStem || `build${selectionIndex}`}-build${selectionIndex}.f`
  )
}

interface BuildSelectionEntry {
  selectionIndex: number
  selectionGlob: string
  command?: string | null
}

interface BuildSelectionMatch {
  entries: BuildSelectionEntry[]
  files: vscode.Uri[]
}

export interface BuildSelectionResult {
  items: BuildQuickPickItem[]
  allGlobs: string[]
  directBuildCount: number
  commandBuildCount: number
}

// Gather build sources from config and expand them into quick-pick items.
export async function createBuildSelectionItems(
  config: Config,
  findFiles: (globs: string[]) => Promise<vscode.Uri[]>
): Promise<BuildSelectionResult> {
  const builds = config.builds ?? []
  const buildEntries = getBuildSelectionEntries(builds)
  const buildEntriesByGlob = groupBuildEntriesByGlob(buildEntries)
  const buildGlobs = [...buildEntriesByGlob.keys()]

  const directBuildGlob = config.buildPattern ?? undefined
  const allGlobs = [...(directBuildGlob ? [directBuildGlob.replace('{}', '*')] : []), ...buildGlobs]
  const allResults = await Promise.all(allGlobs.map((glob) => findFiles([glob])))
  const dotfFiles = directBuildGlob ? (allResults[0] ?? []) : []
  const buildResultsOffset = directBuildGlob ? 1 : 0
  const buildMatches = buildGlobs.map((selectionGlob, resultIndex) => ({
    entries: buildEntriesByGlob.get(selectionGlob) ?? [],
    files: allResults[resultIndex + buildResultsOffset] ?? [],
  }))

  const items: BuildQuickPickItem[] = []
  addDirectBuildItems(items, directBuildGlob, dotfFiles)
  addConfiguredBuildItems(items, builds, buildMatches)

  return {
    items,
    allGlobs,
    directBuildCount: items.filter((item) => item.type === 'filelist').length,
    commandBuildCount: items.filter((item) => item.type === 'command').length,
  }
}

// Build a flat list of configured build entries with their resolved selection indices.
function getBuildSelectionEntries(builds: Config['builds'] = []): BuildSelectionEntry[] {
  return builds.flatMap((build, selectionIndex) => {
    const selectionGlob = build.glob?.trim()
    if (!selectionGlob) {
      return []
    }
    return [{ selectionIndex, selectionGlob, command: build.command }]
  })
}

// Group build entries by glob so shared globs are only searched once.
function groupBuildEntriesByGlob(
  entries: BuildSelectionEntry[]
): Map<string, BuildSelectionEntry[]> {
  const buildEntriesByGlob = new Map<string, BuildSelectionEntry[]>()
  for (const entry of entries) {
    const existingEntries = buildEntriesByGlob.get(entry.selectionGlob)
    if (existingEntries) {
      existingEntries.push(entry)
    } else {
      buildEntriesByGlob.set(entry.selectionGlob, [entry])
    }
  }
  return buildEntriesByGlob
}

// Add direct .f build files discovered from buildPattern.
function addDirectBuildItems(
  items: BuildQuickPickItem[],
  directBuildGlob: string | undefined,
  dotfFiles: vscode.Uri[]
) {
  if (!directBuildGlob || dotfFiles.length === 0) {
    return
  }

  const globPre =
    directBuildGlob.indexOf('{}') >= 0
      ? directBuildGlob.substring(0, directBuildGlob.indexOf('{}'))
      : ''
  const commonPrefixLen = dotfFiles[0].fsPath.indexOf(globPre) + globPre.length
  const commonPrefix = dotfFiles[0].fsPath.substring(0, commonPrefixLen)

  for (const file of dotfFiles) {
    items.push({
      label: file.fsPath.replace(commonPrefix, ''),
      type: 'filelist',
      filePath: file.fsPath,
    })
  }
}

// Add direct and command-backed build entries from the builds config list.
function addConfiguredBuildItems(
  items: BuildQuickPickItem[],
  builds: Config['builds'] = [],
  buildMatches: BuildSelectionMatch[]
) {
  for (const match of buildMatches) {
    if (match.files.length === 0) {
      continue
    }

    if (items.length > 0) {
      items.push({ label: '', kind: vscode.QuickPickItemKind.Separator, type: 'command' })
    }

    for (const file of match.files) {
      const relPath = vscode.workspace.asRelativePath(file)
      for (const entry of match.entries) {
        if (entry.command?.trim()) {
          const buildName = builds[entry.selectionIndex]?.name?.trim()
          items.push({
            label: '$(gear) ' + relPath,
            description: buildName || 'via command',
            type: 'command',
            buildCommand: { sourceFile: file.fsPath, selectionIndex: entry.selectionIndex },
          })
        } else {
          items.push({
            label: relPath,
            description: 'from builds',
            type: 'filelist',
            filePath: file.fsPath,
          })
        }
      }
    }
  }
}
