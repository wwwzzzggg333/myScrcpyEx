# --remote 参数连接断开 Bug 修复报告

## 问题描述

### 症状

使用 `--remote` 参数连接到通过 `adb forward` 转发的服务端时，连接虽然能够成功建立（video、audio、control 三个 socket 都连接成功），但几乎立即断开，出现以下错误：

**客户端日志**：
```
INFO: Video stream connected (1st connection)
INFO: Audio stream connected
INFO: Control stream connected
DEBUG: Server connected
...
DEBUG: Demuxer 'video': end of frames
WARN: Device disconnected
```

**服务端日志**：
```
[server] DEBUG: Controller stopped
[server] DEBUG: Device message sender stopped
[server] DEBUG: Audio encoder stopped
[server] DEBUG: Screen streaming stopped
```

### 复现步骤

1. 在 PC 上执行 `adb forward tcp:50001 localabstract:scrcpy_12345678`
2. 推送服务端到设备：`adb push scrcpy-server /data/local/tmp/scrcpy-server.jar`
3. 在 `adb shell` 中启动服务端：
   ```bash
   CLASSPATH=/data/local/tmp/scrcpy-server.jar \
   app_process / com.genymobile.scrcpy.Server 3.3.4 \
   scid=12345678 log_level=debug tunnel_forward=true cleanup=false
   ```
4. 在 PC 上执行客户端：`scrcpy --remote=127.0.0.1:50001 --verbosity=debug`
5. 观察到连接建立后立即断开

---

## 根本原因

### 问题定位

在 `app/src/server.c` 文件的 `sc_server_connect_to_remote()` 函数中（原第 835-843 行），存在以下错误代码：

```c
// 发送初始字节并读取设备信息（如果有 control socket）
if (control_socket != SC_SOCKET_NONE) {
    // 发送第一个字节以握手
    uint8_t dummy = 0;
    ssize_t w = net_send_all(control_socket, &dummy, 1);
    if (w != 1) {
        LOGE("Failed to send initial byte");
        goto fail;
    }
}
```

### 协议分析

通过对比标准的 `sc_server_connect_to()` 函数（第 600-750 行），发现在 **forward 模式**下，客户端与服务端的通信协议如下：

1. **连接阶段**：客户端依次建立 3 个 TCP 连接（video、audio、control）
2. **握手阶段**：**客户端不发送任何握手字节**
3. **信息交换**：客户端直接从第一个 socket 读取设备信息（`device_read_info()`）

而在原来的 `sc_server_connect_to_remote()` 实现中，错误地添加了发送握手字节的代码，这导致：
- 服务端收到意外的字节数据（`0x00`）
- 服务端无法正确解析该字节，认为协议错误
- 服务端立即关闭所有连接并停止所有编码器

### 误解来源

这个错误可能源于对 **reverse 模式**和 **forward 模式**的混淆：

- **Reverse 模式**（`tunnel_forward=false`）：服务端主动连接到客户端，可能需要握手
- **Forward 模式**（`tunnel_forward=true`）：客户端主动连接到服务端，**不需要握手**

`--remote` 参数使用的是 forward 模式的连接方式，因此不应该发送握手字节。

---

## 修复方案

### 代码修改

**文件**：`app/src/server.c`  
**函数**：`sc_server_connect_to_remote()`  
**修改位置**：第 834-838 行（修改后）

**修改前**（错误代码）：
```c
if (control_socket != SC_SOCKET_NONE) {
    // Disable Nagle's algorithm for the control socket
    bool ok = net_set_tcp_nodelay(control_socket, true);
    (void) ok; // error already logged
}

// 发送初始字节并读取设备信息（如果有 control socket）
if (control_socket != SC_SOCKET_NONE) {
    // 发送第一个字节以握手
    uint8_t dummy = 0;
    ssize_t w = net_send_all(control_socket, &dummy, 1);
    if (w != 1) {
        LOGE("Failed to send initial byte");
        goto fail;
    }
}

// 从第一个可用 socket 读取设备信息
sc_socket info_socket = video_socket != SC_SOCKET_NONE ? video_socket :
                       (audio_socket != SC_SOCKET_NONE ? audio_socket :
                        control_socket);
```

**修改后**（正确代码）：
```c
if (control_socket != SC_SOCKET_NONE) {
    // Disable Nagle's algorithm for the control socket
    bool ok = net_set_tcp_nodelay(control_socket, true);
    (void) ok; // error already logged
}

// 从第一个可用 socket 读取设备信息
// 注意：在 forward 模式下，客户端不发送握手字节，直接读取设备信息
sc_socket info_socket = video_socket != SC_SOCKET_NONE ? video_socket :
                       (audio_socket != SC_SOCKET_NONE ? audio_socket :
                        control_socket);
```

### 修改说明

1. **删除**：发送握手字节的代码块（原第 836-843 行）
2. **添加**：注释说明为何不需要发送握手字节
3. **保留**：设置 TCP_NODELAY 选项（这是正确的）
4. **保留**：读取设备信息的逻辑（这是正确的）

---

## 验证结果

### 预期行为

修复后，使用相同的测试步骤：

1. ✅ 客户端成功连接到服务端（3 个 socket）
2. ✅ 客户端成功读取设备信息
3. ✅ 视频流、音频流正常工作
4. ✅ 控制流（触摸、按键）正常工作
5. ✅ 连接保持稳定，不会意外断开

### 测试命令

```bash
# 1. 建立 ADB forward
adb forward tcp:50001 localabstract:scrcpy_12345678

# 2. 推送服务端
adb push scrcpy-server /data/local/tmp/scrcpy-server.jar

# 3. 启动服务端
adb shell CLASSPATH=/data/local/tmp/scrcpy-server.jar \
  app_process / com.genymobile.scrcpy.Server 3.3.4 \
  scid=12345678 log_level=debug tunnel_forward=true cleanup=false

# 4. 启动客户端（在新的终端窗口）
scrcpy --remote=127.0.0.1:50001 --verbosity=debug
```

---

## 相关知识点

### Forward 模式 vs Reverse 模式

| 特性 | Forward 模式 | Reverse 模式 |
|------|-------------|-------------|
| **连接方向** | 客户端 → 服务端 | 服务端 → 客户端 |
| **ADB 命令** | `adb forward` | `adb reverse` |
| **服务端参数** | `tunnel_forward=true` | `tunnel_forward=false` |
| **握手字节** | ❌ 不发送 | ✅ 可能需要 |
| **使用场景** | 远程连接、SSH 隧道 | 本地 USB 连接 |

### scrcpy 连接协议

```
Forward 模式连接流程：
1. 客户端连接到服务端（3 次 TCP 连接）
2. 客户端从第 1 个 socket 读取设备信息（64 字节）
3. 服务端开始发送视频/音频数据
4. 客户端可以通过 control socket 发送控制指令

关键点：
- 客户端不发送任何握手数据
- 设备信息由服务端主动发送
- 数据流立即开始传输
```

---

## 经验教训

### 1. 协议一致性

实现新功能时，必须严格遵守现有的通信协议。`--remote` 参数本质上是绕过 ADB 直接连接，但**连接建立后的通信协议必须与标准 forward 模式完全一致**。

### 2. 参考实现

在实现 `sc_server_connect_to_remote()` 时，应该：
- ✅ 参考 `sc_server_connect_to()` 函数的 forward 分支（第 637-713 行）
- ✅ 仅替换连接方式（直接 TCP 连接 vs ADB tunnel）
- ✅ 保持握手和数据交换逻辑完全一致
- ❌ 不要自行添加额外的握手逻辑

### 3. 调试技巧

- 使用 Wireshark 抓包分析 TCP 通信内容
- 对比标准模式和 remote 模式的数据包差异
- 在服务端（Java）添加详细日志，记录接收到的数据

---

## 后续优化建议

### 1. 代码重构

考虑将 `sc_server_connect_to()` 和 `sc_server_connect_to_remote()` 合并，减少代码重复：

```c
static bool
sc_server_connect_sockets(struct sc_server *server, 
                         struct sc_server_info *info,
                         uint32_t host, uint16_t port) {
    // 统一的 socket 连接逻辑
    // 可被 ADB tunnel 和 remote 模式共用
}
```

### 2. 单元测试

为连接逻辑添加单元测试，确保：
- 不同模式下的连接行为一致
- 异常情况正确处理
- 资源正确释放

### 3. 文档完善

在代码注释中明确说明：
- Forward 模式的通信协议
- 为什么不需要握手字节
- 与 reverse 模式的区别

---

## 总结

这个 bug 是由于错误理解了 forward 模式的通信协议导致的。修复方法很简单：删除多余的握手字节发送代码，让客户端直接读取设备信息。

**修复日期**：2026-01-24  
**影响范围**：仅影响 `--remote` 参数功能  
**向后兼容**：✅ 不影响任何现有功能  
**测试状态**：⏳ 待实际测试验证

---

## 相关文档

- [REMOTE_PARAMETER_IMPLEMENTATION.md](REMOTE_PARAMETER_IMPLEMENTATION.md) - --remote 参数实现总结
- [TCPIP_CONNECTION_MECHANISM.md](TCPIP_CONNECTION_MECHANISM.md) - scrcpy TCP/IP 连接机制详解
- [SERVER_STARTUP_OPERATIONS.md](SERVER_STARTUP_OPERATIONS.md) - scrcpy 服务端启动流程分析
