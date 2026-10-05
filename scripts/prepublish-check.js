'use strict';

const fs = require('fs');
const path = require('path');

const required = [
  ['prebuilds', 'windows', 'clijudge.exe'],
  ['prebuilds', 'linux', 'clijudge-linux.bin'],
  ['prebuilds', 'windows-arm64', 'clijudge-windows-arm64.exe'],
  ['prebuilds', 'linux-arm64', 'clijudge-linux-arm64.bin'],
];

const missing = required.filter((p) => !fs.existsSync(path.join(__dirname, '..', ...p)));
if (missing.length > 0) {
  console.error('prepublish check failed, missing binaries:');
  for (const p of missing) console.error('  ' + p.join('/'));
  process.exit(1);
}
console.log('prepublish check ok: all four platform binaries present');
