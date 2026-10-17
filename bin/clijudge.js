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
} else if (process.platform === 'linux' || process.platform === 'android') {
  // Android (Termux 等) 与 glibc Linux 二进制不兼容, 需单独的 bionic 构建;
  // Termux 下 Node 上报 process.platform === 'android', 故两种平台都走这里
  const isAndroid = process.platform === 'android'
    || fs.existsSync('/system/bin/linker64') || fs.existsSync('/system/bin/linker');
  if (isAndroid) {
    if (process.arch === 'arm64') {
      bin = path.join(root, 'prebuilds', 'android-arm64', 'clijudge-android-arm64.bin');
    } else {
      console.error('clijudge: no prebuilt binary for android/' + process.arch + ' (supported: arm64)');
      process.exit(1);
    }
  } else if (process.arch === 'arm64') {
    bin = path.join(root, 'prebuilds', 'linux-arm64', 'clijudge-linux-arm64.bin');
  } else if (process.arch === 'x64') {
    bin = path.join(root, 'prebuilds', 'linux', 'clijudge-linux.bin');
  } else {
    console.error('clijudge: no prebuilt binary for linux/' + process.arch + ' (supported: x64, arm64)');
    process.exit(1);
  }
} else {
  console.error('clijudge: unsupported platform "' + process.platform + '" (supported: win32, linux, android)');
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
