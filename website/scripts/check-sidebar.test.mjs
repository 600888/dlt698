import assert from 'node:assert/strict';
import test from 'node:test';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { validateSidebar } from './check-sidebar.mjs';

test('detects the same route reused in different module groups', () => {
  const errors = validateSidebar([
    { text: '传输层', items: [{ text: 'TCP 通道', link: '/transport/tcp' }] },
    { text: '附录', items: [{ text: 'TCP 通道', link: '/transport/tcp' }] },
  ]);
  assert.equal(errors.length, 1);
  assert.match(errors[0], /Duplicate link.*传输层.*附录/);
});

test('allows a module index link repeated across scopes only when absent', () => {
  assert.deepEqual(validateSidebar([
    { text: '模块导读', link: '/transport/' },
    { text: '返回首页', link: '/' },
  ]), []);
});

test('normalizes trailing slashes, index pages, extensions and fragments', () => {
  const errors = validateSidebar([
    { text: 'A', link: '/protocol/frame/' },
    { text: 'B', link: '/protocol/frame/index.md#校验' },
    { text: 'C', link: '/protocol/frame' },
  ]);
  assert.equal(errors.length, 2);
  assert(errors.every(error => error.includes('Duplicate link')));
});

test('reports missing pages instead of silently passing', () => {
  const docsRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'dlt698-sidebar-test-'));
  try {
    fs.mkdirSync(path.join(docsRoot, 'transport'), { recursive: true });
    fs.writeFileSync(path.join(docsRoot, 'transport/tcp.md'), '# TCP\n');
    const errors = validateSidebar([
      { text: 'TCP 通道', link: '/transport/tcp' },
      { text: '串口', link: '/transport/serial' },
    ], { docsRoot });
    assert.equal(errors.length, 1);
    assert.match(errors[0], /Missing page.*transport\/serial/);
  } finally {
    fs.rmSync(docsRoot, { recursive: true, force: true });
  }
});

test('accepts a section header without a link', () => {
  assert.deepEqual(validateSidebar([
    { sectionHeaderText: '接口说明' },
    { dividerType: 'solid' },
  ]), []);
});
