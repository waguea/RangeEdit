import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import test from 'node:test';

const require = createRequire(import.meta.url);
const addonPath = process.env.RANGEEDIT_ADDON;
assert.ok(addonPath, 'RANGEEDIT_ADDON must point at the built addon');

const addon = require(addonPath);

test('exports the expected native surface', () => {
  assert.equal(addon.version, '0.1.0');
  assert.equal(typeof addon.scan, 'function');
  assert.equal(typeof addon.replace, 'function');
  assert.equal(typeof addon.delete, 'function');
  assert.equal(typeof addon.createCancelToken, 'function');
});

test('scans literal UTF-16 text and reports code-unit offsets', async () => {
  const text = '😀 needle needle';
  const result = await addon.scan(text, 'needle');

  assert.equal(result.cancelled, false);
  assert.equal(result.processed, text.length);
  assert.deepEqual(result.matches, [
    { start: 3, length: 6 },
    { start: 10, length: 6 },
  ]);
});

test('honors literal case, whole-word, and max options', async () => {
  const text = 'Cat cat scatter cat';
  const result = await addon.scan(text, 'cat', {
    caseSensitive: false,
    wholeWord: true,
    maxMatches: 2,
  });

  assert.deepEqual(result.matches, [
    { start: 0, length: 3 },
    { start: 4, length: 3 },
  ]);
});

test('scans regex matches', async () => {
  const result = await addon.scan('a1 b22 c333', '[a-z](\\d+)', { regex: true });

  assert.deepEqual(result.matches, [
    { start: 0, length: 2 },
    { start: 3, length: 3 },
    { start: 7, length: 4 },
  ]);
});

test('maps regex matches back to UTF-16 offsets', async () => {
  const result = await addon.scan('😀x42', 'x(\\d+)', { regex: true });
  assert.deepEqual(result.matches, [{ start: 2, length: 3 }]);
});

test('rejects invalid regex patterns', async () => {
  await assert.rejects(() => addon.scan('abc', '(', { regex: true }), /missing/);
});

test('replaces literal matches and reports stats', async () => {
  const result = await addon.replace('a cat b cat', 'cat', 'dog');

  assert.equal(result.text, 'a dog b dog');
  assert.deepEqual(result.matches, [
    { start: 2, length: 3 },
    { start: 8, length: 3 },
  ]);
  assert.deepEqual(result.edits, [
    { start: 2, length: 3 },
    { start: 8, length: 3 },
  ]);
  assert.deepEqual(result.stats, {
    editCount: 2,
    removedUnits: 6,
    insertedUnits: 6,
    outputUnits: 11,
  });
});

test('deletes regex matches without applying text in dry-run mode', async () => {
  const result = await addon.delete('a1 b22', '\\d+', { regex: true, dryRun: true });

  assert.equal('text' in result, false);
  assert.deepEqual(result.edits, [
    { start: 1, length: 1 },
    { start: 4, length: 2 },
  ]);
  assert.deepEqual(result.stats, {
    editCount: 2,
    removedUnits: 3,
    insertedUnits: 0,
    outputUnits: 3,
  });
});

test('reports monotonic progress', async () => {
  const text = 'a'.repeat(4 * 1024 * 1024);
  const events = [];
  const result = await addon.scan(text, 'zz', {
    // The chosen algorithm must not find the absent pattern.
    algorithm: 'bmh',
    onProgress(event) {
      events.push(event);
    },
  });

  assert.equal(result.cancelled, false);
  assert.ok(events.length >= 2);
  assert.equal(events[0].processed, 0);
  assert.equal(events.at(-1).processed, text.length);
  for (const event of events) {
    assert.equal(event.total, text.length);
  }
  for (let index = 1; index < events.length; index += 1) {
    assert.ok(events[index].processed >= events[index - 1].processed);
  }
});

test('cancels a literal scan from a progress callback', async () => {
  const text = 'a'.repeat(8 * 1024 * 1024);
  const token = addon.createCancelToken();
  let progressCalls = 0;

  const result = await addon.scan(text, 'zz', {
    cancelToken: token,
    onProgress(event) {
      progressCalls += 1;
      if (event.processed >= 1024 * 1024) {
        token.cancel();
      }
    },
  });

  assert.equal(result.cancelled, true);
  assert.deepEqual(result.matches, []);
  assert.ok(progressCalls >= 1);
  assert.ok(result.processed >= 1024 * 1024);
  assert.ok(result.processed < text.length);
});

test('rejects foreign cancel token objects', () => {
  assert.throws(
    () => addon.scan('abc', 'a', { cancelToken: {} }),
    /createCancelToken/,
  );
});

test('rejects invalid option types', () => {
  assert.throws(() => addon.scan('abc', 'a', { maxMatches: -1 }), /maxMatches/);
  assert.throws(() => addon.scan('abc', 'a', { algorithm: 'simd' }), /algorithm/);
  assert.throws(() => addon.scan('abc', 'a', { onProgress: 1 }), /onProgress/);
});
