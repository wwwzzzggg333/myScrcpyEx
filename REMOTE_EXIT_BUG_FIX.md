# --remote 模式窗口关闭后进程不退出 Bug 修复

## 问题描述

### 症状

使用 `--remote` 参数连接成功后，当关闭 scrcpy 投屏窗口时：
- ❌ 客户端进程没有退出
- ❌ CMD 窗口持续打印音频缓冲区警告信息
- ❌ 需要手动 Ctrl+C 才能终止进程

**控制台持续输出**：
```
DEBUG: [Audio] Buffering threshold exceeded, skipping 12 samples
DEBUG: [Audio] Buffering threshold exceeded, skipping 203 samples
DEBUG: [Audio] Buffering threshold exceeded, skipping 6 samples
DEBUG: [Audio] Buffering threshold exceeded, skipping 872 samples
...（持续输出，无法自动停止）
```

### 复现条件

1. 使用 `--remote` 参数连接到服务端
2. 连接成功，视频和音频正常工作
3. 关闭投屏窗口（点击 X 按钮或按 Ctrl+W）
4. 观察到客户端进程仍在运行，持续打印日志

---

## 根本原因

### 标准模式 vs Remote 模式的退出流程差异

#### 标准模式的退出流程

在标准模式下（通过 ADB 连接），`run_server()` 函数会：

1. 启动 ADB 进程和服务端进程
2. 建立连接
3. **监听服务端进程状态**（使用 `sc_process_observer`）
4. 当窗口关闭时：
   - 调用 `sc_server_stop()` 设置停止标志
   - **中断所有 socket**（`net_interrupt()`）
   - 终止服务端进程
   - 等待进程退出

关键代码（`server.c` 第 1280-1291 行）：
```c
if (server->video_socket != SC_SOCKET_NONE) {
    net_interrupt(server->video_socket);
}

if (server->audio_socket != SC_SOCKET_NONE) {
    net_interrupt(server->audio_socket);
}

if (server->control_socket != SC_SOCKET_NONE) {
    net_interrupt(server->control_socket);
}
```

#### Remote 模式的退出流程（修复前）

在修复前，remote 模式的 `run_server()` 函数：

```c
// 建立连接
bool ok = sc_server_connect_to_remote(server, &server->info);

// 通知连接成功
server->cbs->on_connected(server, server->cbs_userdata);

// 等待停止信号
sc_mutex_lock(&server->mutex);
while (!server->stopped) {
    sc_cond_wait(&server->cond_stopped, &server->mutex);
}
sc_mutex_unlock(&server->mutex);

return 0;  // ❌ 直接返回，没有中断 socket！
```

**问题所在**：
- ✅ 等待循环能够正确响应停止信号
- ❌ **但没有中断 socket 连接**
- ❌ demuxer（视频/音频解复用器）还在继续从 socket 读取数据
- ❌ 导致进程无法退出

### 为什么 demuxer 还在运行？

scrcpy 的架构中：
- **Demuxer** 运行在独立线程中，持续从 socket 读取数据
- **Audio player** 运行在独立线程中，持续处理音频数据
- **Screen** 运行在主线程中，处理视频显示和用户输入

当窗口关闭时：
1. **Screen** 线程退出（窗口已关闭）
2. **`sc_server_stop()`** 被调用（设置停止标志）
3. **但是**：如果不中断 socket，demuxer 会继续阻塞在 `recv()` 调用上
4. **结果**：demuxer 和 audio player 持续运行，无法退出

---

## 修复方案

### 代码修改

**文件**：`app/src/server.c`  
**函数**：`run_server()`  
**修改位置**：第 1102-1131 行

/* Started by Cursor 10137777 20260124160000000 */
**修改内容**：
```c
// 通知连接成功
server->cbs->on_connected(server, server->cbs_userdata);

// 等待停止信号
sc_mutex_lock(&server->mutex);
while (!server->stopped) {
    sc_cond_wait(&server->cond_stopped, &server->mutex);
}
sc_mutex_unlock(&server->mutex);

// ✅ 新增：收到停止信号后，中断所有 socket
LOGD("Stopping remote connection...");

if (server->video_socket != SC_SOCKET_NONE) {
    net_interrupt(server->video_socket);
}

if (server->audio_socket != SC_SOCKET_NONE) {
    net_interrupt(server->audio_socket);
}

if (server->control_socket != SC_SOCKET_NONE) {
    net_interrupt(server->control_socket);
}

LOGI("Remote connection stopped");

return 0;
```
/* Ended by Cursor 10137777 20260124160000000 */

### 修改说明

1. **保留**：等待停止信号的循环（这部分是正确的）
2. **新增**：在退出前中断所有 socket 连接
3. **效果**：
   - 所有阻塞的 `recv()` 调用会立即返回错误
   - demuxer 会检测到错误并停止
   - audio player 会停止处理
   - 所有线程正常退出

### 技术细节

#### `net_interrupt()` 的作用

在 Windows 和 Linux 上，`net_interrupt()` 会：
- 关闭 socket 的接收和发送功能（`shutdown()`）
- 不关闭 socket 句柄本身
- 导致所有阻塞的 I/O 操作立即返回错误

这样做的好处：
- ✅ 立即唤醒阻塞的线程
- ✅ 不会导致资源泄漏（socket 稍后在 `sc_server_destroy()` 中关闭）
- ✅ 允许线程安全地清理资源

#### 完整的退出流程（修复后）

```
用户关闭窗口
    ↓
event_loop() 收到 SDL_QUIT 事件
    ↓
返回 SCRCPY_EXIT_SUCCESS
    ↓
scrcpy.c 开始清理流程
    ↓
调用 sc_server_stop()
    ↓
    ├─ 设置 server->stopped = true
    ├─ 发送 sc_cond_signal(&server->cond_stopped)
    └─ 调用 sc_intr_interrupt(&server->intr)
    ↓
run_server() 中的等待循环被唤醒
    ↓
中断所有 socket（net_interrupt）
    ↓
    ├─ video demuxer 检测到错误，停止读取
    ├─ audio demuxer 检测到错误，停止读取
    └─ controller 检测到错误，停止读取
    ↓
run_server() 返回 0
    ↓
sc_server_join() 等待线程退出
    ↓
sc_demuxer_join() 等待 demuxer 退出
    ↓
sc_server_destroy() 关闭所有 socket
    ↓
进程正常退出
```

---

## 验证方法

### 测试步骤

1. **重新编译客户端**：
   ```powershell
   cd D:\code\Android\myScrcpyEx-master
   meson compile -C build
   ```

2. **启动服务端**：
   ```powershell
   adb forward tcp:50001 localabstract:scrcpy_12345678
   adb push scrcpy-server /data/local/tmp/scrcpy-server.jar
   adb shell CLASSPATH=/data/local/tmp/scrcpy-server.jar \
     app_process / com.genymobile.scrcpy.Server 3.3.4 \
     scid=12345678 log_level=debug tunnel_forward=true cleanup=false
   ```

3. **启动客户端**：
   ```powershell
   .\build\app\scrcpy.exe --remote=127.0.0.1:50001 --verbosity=debug
   ```

4. **测试退出**：
   - 方法 1：点击窗口右上角的 X 按钮
   - 方法 2：按 Alt+F4
   - 方法 3：按 Ctrl+W（如果配置了快捷键）

### 预期结果

修复后应该看到：

**控制台输出**：
```
DEBUG: Stopping remote connection...
INFO: Remote connection stopped
DEBUG: Video demuxer stopped
DEBUG: Audio demuxer stopped
（进程正常退出，CMD 窗口关闭或返回提示符）
```

✅ **成功的标志**：
- 窗口关闭后，CMD 窗口停止打印日志
- 进程在 1-2 秒内自动退出
- 不需要按 Ctrl+C
- 任务管理器中看不到残留的 scrcpy.exe 进程

❌ **如果仍然失败**：
- 检查是否使用了修复后的版本（重新编译）
- 检查 `app/src/server.c` 第 1113-1126 行是否已添加
- 查看完整的日志，确认是否有其他错误

---

## 与第一个 Bug 的关系

这是 `--remote` 参数实现中的**第二个 bug**，与第一个握手协议 bug 不同：

| Bug | 问题 | 原因 | 影响 |
|-----|------|------|------|
| **Bug #1** | 连接立即断开 | 错误发送握手字节 | 无法建立连接 |
| **Bug #2** | 窗口关闭后进程不退出 | 没有中断 socket | 进程残留 |

两个 bug 都是因为 remote 模式跳过了标准流程中的关键步骤：
- Bug #1：误以为需要握手，实际上 forward 模式不需要
- Bug #2：忘记中断 socket，导致 demuxer 无法停止

---

## 标准模式为什么没有这个问题？

标准模式中，`run_server()` 函数的结构是：

```c
// 1. 启动进程
sc_process_execute();

// 2. 建立连接
sc_server_connect_to();

// 3. 通知连接成功
server->cbs->on_connected();

// 4. 监听进程并等待终止
sc_process_observer_timedwait(&observer, deadline);

// 5. ✅ 中断 socket（即使进程还在运行）
net_interrupt(server->video_socket);
net_interrupt(server->audio_socket);
net_interrupt(server->control_socket);

// 6. 终止进程（如果还在运行）
sc_process_terminate(pid);

return 0;
```

关键区别：
- 标准模式会监听进程状态，当窗口关闭时会执行完整的清理流程
- Remote 模式跳过了进程管理，直接进入等待循环，**忘记了在退出前中断 socket**

---

## 经验教训

### 1. 线程同步和资源清理

在多线程应用中，当主线程要退出时：
1. ✅ 设置停止标志（`server->stopped = true`）
2. ✅ 通知所有等待的线程（`sc_cond_signal()`）
3. ✅ **中断所有阻塞操作**（`net_interrupt()`） ← 容易忘记！
4. ✅ 等待所有线程退出（`sc_thread_join()`）
5. ✅ 释放资源（`net_close()`, `free()` 等）

### 2. 复用代码时要考虑完整流程

在实现 remote 模式时：
- ✅ 我们正确地复用了连接逻辑（`sc_server_connect_to_remote()`）
- ❌ 但忽略了清理逻辑（socket 中断）
- 教训：复用时要考虑完整的生命周期（初始化、运行、清理）

### 3. 对比测试

如果能在实现新功能时，对比标准流程和新流程的关键步骤：
```
标准模式退出：
  1. 监听进程 → 2. 中断socket → 3. 终止进程 → 4. 返回

Remote 模式退出（修复前）：
  1. 等待信号 → 2. 返回  ← 少了中断 socket！

Remote 模式退出（修复后）：
  1. 等待信号 → 2. 中断socket → 3. 返回  ← 正确！
```

---

## 后续优化建议

### 1. 统一退出逻辑

可以将 socket 中断逻辑提取为独立函数：

```c
static void
sc_server_interrupt_sockets(struct sc_server *server) {
    if (server->video_socket != SC_SOCKET_NONE) {
        net_interrupt(server->video_socket);
    }
    if (server->audio_socket != SC_SOCKET_NONE) {
        net_interrupt(server->audio_socket);
    }
    if (server->control_socket != SC_SOCKET_NONE) {
        net_interrupt(server->control_socket);
    }
}
```

然后在标准模式和 remote 模式中都调用这个函数，减少代码重复。

### 2. 添加超时保护

虽然当前实现应该能正常工作，但可以添加超时保护，防止 demuxer 卡住：

```c
// 中断 socket
sc_server_interrupt_sockets(server);

// 等待 demuxer 停止（最多等待 1 秒）
sc_tick deadline = sc_tick_now() + SC_TICK_FROM_SEC(1);
// ... 等待逻辑 ...
```

### 3. 更详细的日志

在退出过程中添加更多日志，帮助调试：

```c
LOGD("Stopping remote connection...");
LOGD("Interrupting video socket...");
net_interrupt(server->video_socket);
LOGD("Interrupting audio socket...");
net_interrupt(server->audio_socket);
LOGD("Interrupting control socket...");
net_interrupt(server->control_socket);
LOGI("All sockets interrupted, waiting for threads to exit...");
```

---

## 总结

### 问题
Remote 模式下，窗口关闭后进程不退出，因为没有中断 socket 连接，导致 demuxer 持续运行。

### 解决
在收到停止信号后，添加 `net_interrupt()` 调用来中断所有 socket，使 demuxer 能够检测到错误并停止。

### 结果
修复后，窗口关闭时进程能够正常退出，不再有残留。

---

**修复日期**：2026-01-24  
**Bug 编号**：#2（--remote 参数相关）  
**影响范围**：仅影响 `--remote` 参数功能  
**向后兼容**：✅ 不影响任何现有功能  
**测试状态**：⏳ 等待用户验证

---

## 相关文档

1. **[REMOTE_MODE_BUG_FIX.md](REMOTE_MODE_BUG_FIX.md)** - Bug #1: 握手协议错误修复
2. **[BUG_FIX_SUMMARY_CN.md](BUG_FIX_SUMMARY_CN.md)** - Bug #1 修复总结（中文）
3. **[REMOTE_PARAMETER_IMPLEMENTATION.md](REMOTE_PARAMETER_IMPLEMENTATION.md)** - --remote 参数实现总结

