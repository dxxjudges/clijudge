#!/usr/bin/env node
'use strict';

const { spawnSync } = require('child_process');
const path = require('path');
const os = require('os');
const fs = require('fs');

const root = path.join(__dirname, '..');

let bin;
if (process.platform === 'win32') {
  // arm64 优先原生二进制; 缺失时回退 x64 (Windows 11 ARM 可仿真运行)
  const arm64 = path.join(root, 'prebuilds', 'windows-arm64', 'clijudge-windows-arm64.exe');
  bin = process.arch === 'arm64' && fs.existsSync(arm64)
    ? arm64
    : path.join(root, 'prebuilds', 'windows', 'clijudge.exe');
} else if (process.platform === 'linux') {
  if (process.arch === 'arm64') {
    bin = path.join(root, 'prebuilds', 'linux-arm64', 'clijudge-linux-arm64.bin');
  } else if (process.arch === 'x64') {
    bin = path.join(root, 'prebuilds', 'linux', 'clijudge-linux.bin');
  } else {
    console.error('clijudge: no prebuilt binary for linux/' + process.arch + ' (supported: x64, arm64)');
    process.exit(1);
  }
} else {
  console.error('clijudge: unsupported platform "' + process.platform + '" (supported: win32, linux)');
  process.exit(1);
}

if (!fs.existsSync(bin)) {
  console.error('clijudge: binary missing: ' + bin);
  process.exit(1);
}

// 默认数据目录放到用户目录, 避免 npm 升级时清空题目数据;
// 已设置 CLIJUDGE_DATA_DIR 时完全尊重用户选择
if (!process.env.CLIJUDGE_DATA_DIR) {
  if (process.platform === 'win32') {
    const base = process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local');
    process.env.CLIJUDGE_DATA_DIR = path.join(base, 'clijudge', 'data');
  } else {
    const base = process.env.XDG_DATA_HOME || path.join(os.homedir(), '.local', 'share');
    process.env.CLIJUDGE_DATA_DIR = path.join(base, 'clijudge', 'data');
  }
}

if (process.platform !== 'win32') {
  try {
    fs.chmodSync(bin, 0o755);
  } catch (e) {
    /* best effort */
  }
}

const result = spawnSync(bin, process.argv.slice(2), { stdio: 'inherit' });
if (result.error) {
  console.error('clijudge: failed to launch: ' + result.error.message);
  process.exit(1);
}
process.exit(result.status === null ? 1 : result.status);
