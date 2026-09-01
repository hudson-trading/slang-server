import tape from 'tape'
import { findInstancePaths } from '../../src/lib/InstancePathUtils'

tape('matches simple dotted instance path', (t) => {
  const results = findInstancePaths('top.sub.module')
  t.equal(results.length, 1)
  t.equal(results[0].path, 'top.sub.module')
  t.equal(results[0].index, 0)
  t.end()
})

tape('matches instance path with array indices', (t) => {
  const results = findInstancePaths('top.sub[0].module[1]')
  t.equal(results.length, 1)
  t.equal(results[0].path, 'top.sub[0].module[1]')
  t.end()
})

tape('matches instance path in a log line', (t) => {
  const results = findInstancePaths('Error at top.sub.module during simulation')
  t.equal(results.length, 1)
  t.equal(results[0].path, 'top.sub.module')
  t.equal(results[0].index, 9)
  t.end()
})

tape('matches multiple instance paths on one line', (t) => {
  const results = findInstancePaths('comparing top.a.b and top.c.d')
  t.equal(results.length, 2)
  t.equal(results[0].path, 'top.a.b')
  t.equal(results[1].path, 'top.c.d')
  t.end()
})

tape('ignores .sv file extensions', (t) => {
  const results = findInstancePaths('error in mymodule.sv')
  t.equal(results.length, 0)
  t.end()
})

tape('ignores .v file extensions', (t) => {
  const results = findInstancePaths('error in mymodule.v')
  t.equal(results.length, 0)
  t.end()
})

tape('ignores .svh file extensions', (t) => {
  const results = findInstancePaths('included header.svh')
  t.equal(results.length, 0)
  t.end()
})

tape('ignores .vh file extensions', (t) => {
  const results = findInstancePaths('included header.vh')
  t.equal(results.length, 0)
  t.end()
})

tape('matches instance path that happens to contain sv-like segment in the middle', (t) => {
  const results = findInstancePaths('top.sv_wrapper.module')
  t.equal(results.length, 1)
  t.equal(results[0].path, 'top.sv_wrapper.module')
  t.end()
})

tape('ignores file paths with slashes', (t) => {
  const results = findInstancePaths('/path/to/thing.with.dots')
  t.equal(results.length, 0)
  t.end()
})

tape('ignores relative file paths', (t) => {
  const results = findInstancePaths('src/design/top.sub.module')
  t.equal(results.length, 0)
  t.end()
})

tape('ignores file paths with backslashes', (t) => {
  const results = findInstancePaths('C:\\path\\to\\thing.with.dots')
  t.equal(results.length, 0)
  t.end()
})

tape('matches instance path after colon', (t) => {
  const results = findInstancePaths('signal: top.sub.module')
  t.equal(results.length, 1)
  t.equal(results[0].path, 'top.sub.module')
  t.end()
})

tape('matches instance path after parenthesis', (t) => {
  const results = findInstancePaths('(top.sub.module)')
  t.equal(results.length, 1)
  t.equal(results[0].path, 'top.sub.module')
  t.end()
})

tape('ignores .sv file but matches real instance path on same line', (t) => {
  const results = findInstancePaths('In file top.sv, see top.dut.core')
  t.equal(results.length, 1)
  t.equal(results[0].path, 'top.dut.core')
  t.end()
})

tape('ignores single word with no dots', (t) => {
  const results = findInstancePaths('just a plain word')
  t.equal(results.length, 0)
  t.end()
})

tape('handles dollar sign identifiers', (t) => {
  const results = findInstancePaths('top.$unit.module')
  t.equal(results.length, 1)
  t.equal(results[0].path, 'top.$unit.module')
  t.end()
})

tape('does not match path followed by slash', (t) => {
  const results = findInstancePaths('top.sub.module/child')
  t.equal(results.length, 0)
  t.end()
})

tape('ignores sv extension at end of longer path', (t) => {
  const results = findInstancePaths('path.to.file.sv')
  t.equal(results.length, 0)
  t.end()
})
