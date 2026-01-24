# 窗口关闭后进程不退出问题 - 修复总结（中文）

## 🔍 问题现象

使用 `scrcpy --remote=127.0.0.1:50001` 连接成功后：
- ✅ 视频和音频正常工作
- ✅ 可以正常控制设备
- ❌ **关闭窗口后，CMD 窗口持续打印音频警告**
- ❌ **进程不退出，需要手动 Ctrl+C 终止**

控制台持续输出：
```
DEBUG: [Audio] Buffering threshold exceeded, skipping 12 samples
DEBUG: [Audio] Buffering threshold exceeded, skipping 203 samples
DEBUG: [Audio] Buffering threshold exceeded, skipping 6 samples
...（无限循环）
```

---

## 💡 问题原因

当你关闭投屏窗口时，scrcpy 会调用 `sc_server_stop()` 通知服务器线程停止。

**在标准模式下**（通过 ADB 连接）：
1. 收到停止信号
2. ✅ **中断所有 socket 连接**
3. demuxer（音视频解复用器）检测到错误并停止
4. 进程正常退出

**在 remote 模式下**（修复前）：
1. 收到停止信号
2. ❌ **直接返回，没有中断 socket**
3. demuxer 还在继续从 socket 读取数据
4. 音频处理器还在继续工作
5. 进程无法退出！

**简单来说**：忘记告诉音视频处理线程"该停下来了"，它们就一直干活。

---

## ✅ 修复方案

在 `app/src/server.c` 的 `run_server()` 函数中，remote 模式退出前添加中断 socket 的代码：

```c
// 等待停止信号
sc_mutex_lock(&server->mutex);
while (!server->stopped) {
    sc_cond_wait(&server->cond_stopped, &server->mutex);
}
sc_mutex_unlock(&server->mutex);

// ✅ 新增：收到停止信号后，中断所有 socket
LOGD("Stopping remote connection...");

if (server->video_socket != SC_SOCKET_NONE) {
    net_interrupt(server->video_socket);  // 中断视频流
}

if (server->audio_socket != SC_SOCKET_NONE) {
    net_interrupt(server->audio_socket);  // 中断音频流
}

if (server->control_socket != SC_SOCKET_NONE) {
    net_interrupt(server->control_socket);  // 中断控制流
}

LOGI("Remote connection stopped");

return 0;
```

**效果**：
- `net_interrupt()` 会立即中断所有阻塞的读写操作
- demuxer 检测到 socket 错误，停止读取
- 音频处理器停止处理
- 所有线程正常退出

---

## 🔨 如何测试修复

### 步骤 1: 重新编译

```powershell
cd D:\code\Android\myScrcpyEx-master
meson compile -C build
```

### 步骤 2: 启动服务端

```powershell
# 终端 1
adb forward tcp:50001 localabstract:scrcpy_12345678
adb push scrcpy-server /data/local/tmp/scrcpy-server.jar
adb shell CLASSPATH=/data/local/tmp/scrcpy-server.jar app_process / com.genymobile.scrcpy.Server 3.3.4 scid=12345678 log_level=debug tunnel_forward=true cleanup=false
```

### 步骤 3: 启动客户端

```powershell
# 终端 2
.\build\app\scrcpy.exe --remote=127.0.0.1:50001 --verbosity=debug
```

### 步骤 4: 测试退出

关闭投屏窗口（点击 X 或按 Alt+F4）

### 预期结果

✅ **修复成功的标志**：

控制台输出：
```
DEBUG: Stopping remote connection...
INFO: Remote connection stopped
DEBUG: Video demuxer stopped
DEBUG: Audio demuxer stopped
（CMD 窗口停止打印，进程自动退出）
```

- ✅ 窗口关闭后 1-2 秒内进程自动退出
- ✅ CMD 窗口不再持续打印日志
- ✅ 不需要按 Ctrl+C
- ✅ 任务管理器中没有残留进程

❌ **如果还是不行**：
- 检查是否重新编译了
- 检查 `app/src/server.c` 第 1113-1126 行是否有新代码
- 查看完整日志排查其他问题

---

## 📊 两个 Bug 的对比

到目前为止，`--remote` 参数实现中发现并修复了 2 个 bug：

| Bug | 问题 | 症状 | 原因 | 修复位置 |
|-----|------|------|------|---------|
| **#1** | 连接立即断开 | 连接成功后马上断开，看不到画面 | 错误发送握手字节 | `sc_server_connect_to_remote()` 函数 |
| **#2** | 进程不退出 | 关闭窗口后进程残留，持续打印日志 | 没有中断 socket | `run_server()` 函数 remote 分支 |

两个 bug 都是因为 remote 模式跳过了标准流程中的关键步骤。

---

## 🎯 完整的退出流程（修复后）

```
1. 用户点击窗口 X 按钮
   ↓
2. SDL 发送 SDL_QUIT 事件
   ↓
3. event_loop() 返回 SCRCPY_EXIT_SUCCESS
   ↓
4. scrcpy.c 调用 sc_server_stop()
   ↓
5. sc_server_stop() 设置停止标志并发送信号
   ↓
6. run_server() 的等待循环被唤醒
   ↓
7. ✅ 中断所有 socket（net_interrupt）
   ↓
8. demuxer 检测到 socket 错误，停止读取
   ↓
9. 音频处理器停止
   ↓
10. 所有线程退出
    ↓
11. 进程正常退出 ✅
```

---

## 📚 相关文档

1. **[REMOTE_EXIT_BUG_FIX.md](REMOTE_EXIT_BUG_FIX.md)** - 详细技术分析（英文）
2. **[REMOTE_MODE_BUG_FIX.md](REMOTE_MODE_BUG_FIX.md)** - Bug #1 修复详情
3. **[BUG_FIX_SUMMARY_CN.md](BUG_FIX_SUMMARY_CN.md)** - Bug #1 修复总结（中文）
4. **[REMOTE_PARAMETER_IMPLEMENTATION.md](REMOTE_PARAMETER_IMPLEMENTATION.md)** - 完整实现文档

---

## ❓ 常见问题

### Q1: 为什么标准模式没有这个问题？

因为标准模式的 `run_server()` 函数在退出前会执行完整的清理流程，包括中断 socket。而 remote 模式之前跳过了这个步骤。

### Q2: 什么是 `net_interrupt()`？

`net_interrupt()` 是一个 socket 操作，它会：
- 关闭 socket 的接收和发送功能（调用系统的 `shutdown()`）
- 立即唤醒所有阻塞在该 socket 上的读写操作
- 但不关闭 socket 句柄（会在稍后的 `net_close()` 中关闭）

### Q3: 修复后会不会影响其他功能？

不会。这个修复只影响 remote 模式的退出流程，不影响任何其他功能。

### Q4: 如果我用的是 `--no-audio`，还需要这个修复吗？

需要。即使禁用了音频，视频 demuxer 也可能在窗口关闭后继续运行。这个修复会中断所有启用的 socket。

---

## ✨ 总结

### 问题
关闭窗口后，remote 模式的客户端进程不退出，持续打印音频警告。

### 原因
没有中断 socket 连接，导致 demuxer 持续运行。

### 修复
在收到停止信号后，添加 `net_interrupt()` 中断所有 socket。

### 结果
窗口关闭后进程正常退出，不再有残留。

---

**修复日期**：2026-01-24  
**测试状态**：⏳ 等待用户验证

测试完成后请告知结果，如有问题随时反馈！🚀

