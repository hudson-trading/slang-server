import tape from 'tape'

import { resolveHierarchyChild, splitHierarchyPath } from '../../src/lib/InstancePathUtils'

tape('splitHierarchyPath keeps array indices as separate parts', (t) => {
  t.deepEqual(splitHierarchyPath('top.u1[0].u2[3][4]'), ['top', 'u1', '[0]', 'u2', '[3]', '[4]'])
  t.end()
})

tape('splitHierarchyPath tolerates vaporview-style dotted indices', (t) => {
  t.deepEqual(splitHierarchyPath('top.u1.[0].u2.[3]'), ['top', 'u1', '[0]', 'u2', '[3]'])
  t.end()
})

tape('splitHierarchyPath handles package-qualified members', (t) => {
  t.deepEqual(splitHierarchyPath('test_pkg::WIDTH'), ['test_pkg', 'WIDTH'])
  t.deepEqual(splitHierarchyPath('test_pkg::array_t[3]'), ['test_pkg', 'array_t', '[3]'])
  t.end()
})

tape('resolveHierarchyChild matches a combined array child name', (t) => {
  const children = [
    {
      instName: 'stage_group[7]',
      path: 'chip_top.cluster.tile.link.stage_group[7]',
    },
  ]

  const resolved = resolveHierarchyChild(
    children,
    ['stage_group', '[7]'],
    0,
    'chip_top.cluster.tile.link'
  )

  t.equal(resolved.child, children[0])
  t.equal(resolved.nextIndex, 1, 'combined child consumes the bracket segment too')
  t.end()
})

tape('resolveHierarchyChild matches bracket children by resolved path', (t) => {
  const children = [
    {
      instName: '[7]',
      path: 'chip_top.cluster.tile.link.stage_group[7]',
    },
  ]

  const resolved = resolveHierarchyChild(
    children,
    ['[7]'],
    0,
    'chip_top.cluster.tile.link.stage_group'
  )

  t.equal(resolved.child, children[0])
  t.equal(resolved.nextIndex, 0)
  t.end()
})
