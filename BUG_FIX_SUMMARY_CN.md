# Bug 修复总结 - 中文版

## 🔍 问题诊断

### 你遇到的问题

使用 `scrcpy --remote=127.0.0.1:50001` 连接到通过 `adb forward` 转发的服务端时：
- ✅ 连接能成功建立（所有 3 个 socket 都连接成功）
- ❌ 但几乎立即断开，显示 "Device disconnected"
- ❌ 服务端所有组件停止（Controller、Audio encoder、Screen streaming）

### 根本原因

**在 `app/src/server.c` 的 `sc_server_connect_to_remote()` 函数中，错误地添加了发送握手字节的代码！**

```c
// ❌ 错误的代码（已删除）
uint8_t dummy = 0;
ssize_t w = net_send_all(control_socket, &dummy, 1);
```

**为什么这是错误的？**

scrcpy 在 **forward 模式**下的通信协议是：
1. 客户端连接到服务端（建立 3 个 TCP 连接）
2. **客户端不发送任何数据**
3. 客户端直接从 video socket 读取设备信息（64 字节）
4. 服务端开始发送视频/音频流

当客户端错误地发送了 `0x00` 字节后，服务端无法识别这个数据，认为协议错误，立即关闭连接。

---

## ✅ 修复方案

### 修改的文件

**文件**：`app/src/server.c`  
**行数**：删除了第 836-843 行（发送握手字节的代码）

### 修改内容

/* Started by Cursor 10137777 20260124153500000 */
**删除了错误的代码**：
```c
// 这段代码已被删除
if (control_socket != SC_SOCKET_NONE) {
    uint8_t dummy = 0;
    ssize_t w = net_send_all(control_socket, &dummy, 1);
    if (w != 1) {
        LOGE("Failed to send initial byte");
        goto fail;
    }
}
```

**保留了正确的代码**：
```c
// 从第一个可用 socket 读取设备信息
// 注意：在 forward 模式下，客户端不发送握手字节，直接读取设备信息
sc_socket info_socket = video_socket != SC_SOCKET_NONE ? video_socket :
                       (audio_socket != SC_SOCKET_NONE ? audio_socket :
                        control_socket);

if (info_socket != SC_SOCKET_NONE) {
    bool ok = device_read_info(&server->intr, info_socket, info);
    if (!ok) {
        LOGW("Could not read device info, using default name");
        // 不算致命错误，继续执行
    }
}
```
/* Ended by Cursor 10137777 20260124153500000 */

---

## 🔨 如何编译和测试

### 步骤 1: 编译客户端

在 Windows 环境下，进入 scrcpy 源码目录：

```powershell
cd D:\code\Android\myScrcpyEx-master

# 如果使用 Meson 构建系统
meson setup build --buildtype=release
meson compile -C build

# 编译后的可执行文件位于：
# build\app\scrcpy.exe
```

### 步骤 2: 准备服务端

```powershell
# 确保服务端 JAR 文件是最新的（通常在 server\build\outputs\ 目录）
# 或使用现有的 scrcpy-server.jar
```

### 步骤 3: 启动服务端

在 **终端 1** 中：

```powershell
# 建立 ADB forward
adb forward tcp:50001 localabstract:scrcpy_12345678

# 推送服务端
adb push scrcpy-server /data/local/tmp/scrcpy-server.jar

# 启动服务端
adb shell
export CLASSPATH=/data/local/tmp/scrcpy-server.jar
app_process / com.genymobile.scrcpy.Server 3.3.4 scid=12345678 log_level=debug tunnel_forward=true cleanup=false
```

**预期输出**：
```
[server] INFO: Device: [HUAWEI] HUAWEI BLK-AL00 (Android 12)
[server] DEBUG: Using video encoder: OMX.hisi.video.encoder.avc
[server] DEBUG: Using audio encoder: c2.android.opus.encoder
[server] DEBUG: Display: using SurfaceControl API
```

### 步骤 4: 启动客户端

在 **终端 2** 中（使用修复后的版本）：

```powershell
# 使用编译后的版本
D:\code\Android\myScrcpyEx-master\build\app\scrcpy.exe --remote=127.0.0.1:50001 --verbosity=debug

# 或者如果已经安装到系统路径
scrcpy --remote=127.0.0.1:50001 --verbosity=debug
```

### 步骤 5: 验证结果

✅ **预期成功的迹象**：

**客户端输出**：
```
INFO: Remote connection mode: connecting to 127.0.0.1:50001
INFO: Video stream connected (1st connection)
INFO: Audio stream connected
INFO: Control stream connected
DEBUG: Server connected
INFO: Texture: 1080x2408
（视频窗口正常显示）
（音频正常播放）
```

**服务端输出**：
```
（保持运行，不应该出现 "Controller stopped" 等停止消息）
```

❌ **如果仍然失败**：
- 检查是否使用了修复后的版本（重新编译）
- 检查 ADB forward 是否正常：`adb forward --list`
- 尝试增加日志级别：`log_level=verbose`
- 查看完整的客户端和服务端日志

---

## 📊 测试检查清单

完成以下测试以确保修复有效：

### 基础功能测试

- [ ] **视频流**：画面正常显示，无花屏、黑屏
- [ ] **音频流**：声音正常播放，无杂音、断断续续
- [ ] **触摸控制**：点击、滑动操作正常响应
- [ ] **键盘输入**：文字输入、快捷键正常工作
- [ ] **连接稳定性**：持续运行 5 分钟以上不断开

### 参数组合测试

```powershell
# 测试 1: 禁用音频
scrcpy --remote=127.0.0.1:50001 --no-audio

# 测试 2: 仅音频模式
scrcpy --remote=127.0.0.1:50001 --no-video --no-control

# 测试 3: 无窗口模式
scrcpy --remote=127.0.0.1:50001 --no-window

# 测试 4: 调整视频质量
scrcpy --remote=127.0.0.1:50001 -b 16M --max-fps=60

# 测试 5: 全屏模式
scrcpy --remote=127.0.0.1:50001 --fullscreen
```

### 错误处理测试

- [ ] **服务端未启动**：客户端应该显示连接失败，不崩溃
- [ ] **端口错误**：使用错误的端口号，应该显示明确的错误信息
- [ ] **中途断开**：杀掉服务端进程，客户端应该正常退出

---

## 📚 相关文档

1. **[REMOTE_MODE_BUG_FIX.md](REMOTE_MODE_BUG_FIX.md)** - 详细的技术分析和修复说明
2. **[REMOTE_PARAMETER_IMPLEMENTATION.md](REMOTE_PARAMETER_IMPLEMENTATION.md)** - --remote 参数实现总结
3. **[TCPIP_CONNECTION_MECHANISM.md](TCPIP_CONNECTION_MECHANISM.md)** - scrcpy TCP/IP 连接机制

---

## 💡 常见问题

### Q1: 修复后还是断开连接怎么办？

**检查步骤**：
1. 确认已重新编译并使用新的可执行文件
2. 检查 `app/src/server.c` 第 834-838 行是否已修改
3. 使用 Wireshark 抓包查看 TCP 通信内容
4. 比对修复前后的网络数据包

### Q2: 如何确认使用的是修复后的版本？

在客户端代码中添加版本标识：
```c
LOGI("scrcpy version: 3.3.4 (with --remote bug fix 2026-01-24)");
```

或者检查可执行文件的时间戳：
```powershell
dir build\app\scrcpy.exe
```

### Q3: 为什么要删除握手字节？

因为 scrcpy 的 **forward 模式**协议设计如下：
- 客户端建立连接后，**不主动发送任何数据**
- 服务端主动发送设备信息（64 字节）
- 然后开始发送视频/音频流

如果客户端发送了额外的字节，会破坏这个协议流程。

### Q4: reverse 模式需要握手字节吗？

**不需要**。无论是 forward 还是 reverse 模式，客户端都不发送握手字节。区别在于：
- **Forward**：客户端主动连接（`net_connect`）
- **Reverse**：客户端被动接受（`net_accept`）

---

## 🎯 总结

### 问题
客户端 `--remote` 模式错误地发送了握手字节，导致服务端断开连接。

### 解决
删除发送握手字节的代码，让客户端直接读取设备信息。

### 结果
修复后，`--remote` 参数应该能够正常工作，连接稳定，视频/音频流正常。

### 下一步
1. ✅ 重新编译客户端
2. ✅ 按照测试步骤验证修复
3. ✅ 完成测试检查清单
4. ✅ 记录测试结果

---

**修复完成日期**：2026-01-24  
**修复人员**：Cursor AI (10137777)  
**测试状态**：⏳ 等待用户验证

如有任何问题，请参考详细文档或重新运行测试。

