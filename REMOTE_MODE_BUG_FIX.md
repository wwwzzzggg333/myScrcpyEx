# Remote 模式 Bug 修复记录

## 问题描述

**日期**: 2026-01-24  
**错误信息**: `ERROR: OOM: ./../app/src/file_pusher.c:44 sc_file_pusher_init()`

### 错误现象

使用 `--remote` 参数连接服务端时，客户端连接成功但立即崩溃退出，提示内存溢出错误。

```bash
scrcpy --remote=127.0.0.1:27183 --verbosity=debug
```

**错误日志**:
```
INFO: Remote connection mode: connecting to 127.0.0.1:27183
DEBUG: Remaining connection attempts: 3
INFO: Video stream connected (1st connection)
INFO: Connecting to audio stream (2nd connection)...
INFO: Audio stream connected
INFO: Connecting to control stream (3rd connection)...
INFO: Control stream connected
INFO: All streams connected successfully
DEBUG: Server connected
ERROR: OOM: ./../app/src/file_pusher.c:44 sc_file_pusher_init()
```

---

## 根本原因分析

### 1. 问题根源

在 `app/src/scrcpy.c` 第 564-570 行：

```c
const char *serial = s->server.serial;
assert(serial);

struct sc_file_pusher *fp = NULL;

if (options->video_playback && options->control) {
    if (!sc_file_pusher_init(&s->file_pusher, serial,
                             options->push_target)) {
        goto end;
    }
    fp = &s->file_pusher;
    file_pusher_initialized = true;
}
```

**问题**：
- `file_pusher` 初始化需要 `serial`（设备序列号）
- 在 **remote 模式**下，`server->serial` 为 `NULL`
- 因为 remote 模式跳过了所有 ADB 操作，没有设置设备序列号

### 2. 调用链分析

```
scrcpy()
  └─> await_for_server()  // 等待服务器连接
  └─> sc_file_pusher_init(&s->file_pusher, serial, ...)
        └─> strdup(serial)  // serial 为 NULL，导致失败
              └─> LOG_OOM()  // 报告内存溢出错误
```

### 3. 为什么 file_pusher 在 remote 模式下不适用？

`file_pusher` 依赖 ADB 命令：
- `sc_adb_push()` - 推送文件到设备
- `sc_adb_install()` - 安装 APK

在 remote 模式下：
- ✅ 没有 ADB 连接
- ✅ 无法执行 `adb push` 或 `adb install`
- ✅ file_pusher 功能不可用

---

## 修复方案

### 方案 1: 在 server.c 中设置假的 serial（已实现）

**文件**: `app/src/server.c`  
**位置**: 第 1097-1104 行

```c
// 在 remote 模式下设置一个假的 serial，用于 file_pusher 等模块
// 注意：remote 模式下 file_pusher 的功能可能受限
server->serial = strdup("remote");
if (!server->serial) {
    LOG_OOM();
    goto error_connection_failed;
}
```

**优点**：
- 防止崩溃
- 保持代码结构一致

**缺点**：
- file_pusher 虽然初始化成功，但实际功能不可用
- 可能误导用户

### 方案 2: 在 scrcpy.c 中跳过 file_pusher 初始化（已实现）

**文件**: `app/src/scrcpy.c`  
**位置**: 第 571 行

```c
// 在 remote 模式下跳过 file_pusher 初始化，因为它需要 ADB
if (options->video_playback && options->control && !options->remote_host) {
    if (!sc_file_pusher_init(&s->file_pusher, serial,
                             options->push_target)) {
        goto end;
    }
    fp = &s->file_pusher;
    file_pusher_initialized = true;
}
```

**优点**：
- 明确表示 remote 模式不支持文件推送
- 避免不必要的初始化
- 更清晰的语义

**缺点**：
- 需要在多处检查 `remote_host`

---

## 最终实现

**采用双重保护策略**：

1. **在 `server.c` 中设置假 serial**：防止其他可能依赖 `serial` 的模块崩溃
2. **在 `scrcpy.c` 中跳过 file_pusher**：明确禁用不可用的功能

---

## 测试验证

### 测试步骤

1. 启动 scrcpy 服务端：
```bash
adb push scrcpy-server /data/local/tmp/scrcpy-server.jar
adb shell
CLASSPATH=/data/local/tmp/scrcpy-server.jar \
app_process / com.genymobile.scrcpy.Server 3.3.4 \
scid=12345678 log_level=debug tunnel_forward=true cleanup=false
```

2. 设置端口转发：
```bash
adb forward tcp:27183 localabstract:scrcpy_12345678
```

3. 启动客户端（修复后）：
```bash
scrcpy --remote=127.0.0.1:27183
```

### 预期结果

✅ 客户端成功连接  
✅ 视频、音频、控制流正常工作  
✅ 不再出现 OOM 错误  
✅ 文件拖放功能被禁用（符合预期）

---

## 相关功能影响

### Remote 模式下不可用的功能

| 功能 | 状态 | 原因 |
|-----|------|------|
| 文件拖放 | ❌ 不可用 | 需要 `adb push` |
| APK 安装 | ❌ 不可用 | 需要 `adb install` |
| 视频镜像 | ✅ 可用 | 通过 TCP socket |
| 音频传输 | ✅ 可用 | 通过 TCP socket |
| 触控控制 | ✅ 可用 | 通过 TCP socket |
| 键盘输入 | ✅ 可用 | 通过 TCP socket |
| 剪贴板同步 | ✅ 可用 | 通过 TCP socket |

### 建议

如果用户在 remote 模式下需要文件传输功能，可以：
1. 使用其他工具（如 `scp`、`rsync`）
2. 通过网络共享文件夹
3. 使用 HTTP 文件服务器

---


## 总结

- ✅ 修复了 remote 模式下的崩溃问题
- ✅ 明确了 remote 模式的功能限制
- ✅ 保持了代码的健壮性
- ✅ 添加了完整的追溯标记

**修改文件**：
1. `app/src/server.c` - 设置假 serial
2. `app/src/scrcpy.c` - 跳过 file_pusher 初始化

