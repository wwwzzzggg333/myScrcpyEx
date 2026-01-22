# --remote 参数测试指南

## 功能概述

新增的 `--remote` 参数允许 scrcpy 客户端直接连接到已启动的服务端，跳过所有 ADB 相关操作。

**使用格式**：
```bash
scrcpy --remote=IP:PORT
```

**连接机制**：
- 客户端会连接 **3 次到同一个端口**
- 服务端通过**连接顺序**区分不同的流：
  - 第 1 个连接 - 视频流
  - 第 2 个连接 - 音频流
  - 第 3 个连接 - 控制流

---

## 编译项目

### Windows 环境

```bash
# 1. 配置构建
meson setup builddir --buildtype=release

# 2. 编译
meson compile -C builddir

# 3. 可执行文件位置
# builddir/app/scrcpy.exe
```

### Linux/macOS 环境

```bash
# 1. 配置构建
meson setup builddir --buildtype=release

# 2. 编译
meson compile -C builddir

# 3. 可执行文件位置
# builddir/app/scrcpy
```

---

## 测试场景

### 场景 1: 基本功能测试

#### 准备工作

在另一台 PC 或同一台 PC 上，使用标准 scrcpy 启动服务端并配置端口转发：

```bash
# 步骤 1: 启动标准 scrcpy（这会启动服务端）
scrcpy -s <device_serial>

# 步骤 2: 在另一个终端查看 adb forward 列表
adb forward --list

# 输出示例：
# <device_serial> tcp:27183 localabstract:scrcpy_12ab34cd

# 步骤 3: 手动建立端口转发到远程可访问的端口
# 注意：只需要一个端口转发！所有 3 个连接都会使用这个端口
adb forward tcp:50001 localabstract:scrcpy_12ab34cd
```

**重要说明**：
- ✅ **正确**：只需要一个 `adb forward`，客户端会连接 3 次到同一个端口
- ❌ **错误**：不需要 3 个 `adb forward` 到不同端口
- 服务端在同一个 localabstract socket 上按顺序 accept 3 个连接

#### 测试命令

```bash
# 在测试 PC 上执行
scrcpy --remote=192.168.1.100:50001
```

**预期结果**：
- 客户端成功连接到远程服务端
- 显示 "Remote Device" 作为设备名称
- 视频、音频、控制功能正常工作

---

### 场景 2: 参数组合测试

#### 测试 2.1: 禁用音频

```bash
scrcpy --remote=192.168.1.100:50001 --no-audio
```

**预期结果**：
- 只建立 2 个连接到端口 50001（第 1 个：视频，第 2 个：控制）
- 跳过音频连接

#### 测试 2.2: 仅音频模式

```bash
scrcpy --remote=192.168.1.100:50001 --no-video --no-control
```

**预期结果**：
- 只建立 1 个连接到端口 50001（音频流）
- 无视频显示，无控制功能

#### 测试 2.3: 无窗口模式

```bash
scrcpy --remote=192.168.1.100:50001 --no-window
```

**预期结果**：
- 后台运行，无窗口显示
- 音频正常播放

---

### 场景 3: 错误处理测试

#### 测试 3.1: 无效 IP 格式

```bash
scrcpy --remote=invalid:50001
```

**预期结果**：
- 立即报错：`Invalid IP address: invalid`
- 程序退出

#### 测试 3.2: 无效端口

```bash
scrcpy --remote=192.168.1.100:99999
```

**预期结果**：
- 立即报错：`Invalid port: 99999`
- 程序退出

#### 测试 3.3: 缺少端口号

```bash
scrcpy --remote=192.168.1.100
```

**预期结果**：
- 立即报错：`Invalid remote address format, expected IP:PORT`
- 程序退出

#### 测试 3.4: 端口不可达

```bash
scrcpy --remote=192.168.1.100:60000
```

**预期结果**：
- 尝试连接 50 次（每次间隔 200ms）
- 最终报错：`Failed to connect to video stream`
- 程序退出

---

### 场景 4: 参数冲突测试

#### 测试 4.1: 与 -s 冲突

```bash
scrcpy --remote=192.168.1.100:50001 -s deviceserial
```

**预期结果**：
- 立即报错：`--remote cannot be used with device selection options (-s, --tcpip, -d, -e)`
- 程序退出

#### 测试 4.2: 与 --tcpip 冲突

```bash
scrcpy --remote=192.168.1.100:50001 --tcpip=192.168.1.200:5555
```

**预期结果**：
- 立即报错：`--remote cannot be used with device selection options (-s, --tcpip, -d, -e)`
- 程序退出

#### 测试 4.3: 与 -d 冲突

```bash
scrcpy --remote=192.168.1.100:50001 -d
```

**预期结果**：
- 立即报错：`--remote cannot be used with device selection options (-s, --tcpip, -d, -e)`
- 程序退出

#### 测试 4.4: 与 --list-encoders 冲突

```bash
scrcpy --remote=192.168.1.100:50001 --list-encoders
```

**预期结果**：
- 立即报错：`--remote cannot be used with --list-* options`
- 程序退出

---

### 场景 5: 网络环境测试

#### 测试 5.1: 本地回环测试

```bash
# 在同一台机器上测试
scrcpy --remote=127.0.0.1:50001
```

#### 测试 5.2: 局域网测试

```bash
# 跨机器测试
scrcpy --remote=192.168.1.100:50001
```

#### 测试 5.3: 通过 SSH 隧道测试

```bash
# 步骤 1: 建立 SSH 隧道（只需要一个端口！）
ssh -L 50001:localhost:50001 user@remote-server

# 步骤 2: 在本地连接
scrcpy --remote=127.0.0.1:50001
```

---

## 日志输出示例

### 成功连接

```
INFO: Remote connection mode: connecting to 192.168.1.100:50001
INFO: Connecting to video stream at port 50001...
INFO: Video stream connected
INFO: Connecting to audio stream at port 50002...
INFO: Audio stream connected
INFO: Connecting to control stream at port 50003...
INFO: Control stream connected
INFO: All streams connected successfully
INFO: Device: Remote Device
```

### 连接失败

```
INFO: Remote connection mode: connecting to 192.168.1.100:50001
INFO: Connecting to video stream at port 50001...
DEBUG: Remaining connection attempts: 50
DEBUG: Remaining connection attempts: 49
...
ERROR: Failed to connect to video stream
ERROR: Server connection failed
```

---

## 调试技巧

### 1. 查看详细日志

```bash
scrcpy --remote=192.168.1.100:50001 -V debug
```

### 2. 使用 tcpdump 监控网络流量

```bash
# Linux/macOS
sudo tcpdump -i any port 50001 or port 50002 or port 50003

# Windows (使用 Wireshark)
# 过滤器: tcp.port in {50001 50002 50003}
```

### 3. 测试端口连通性

```bash
# Linux/macOS
nc -zv 192.168.1.100 50001
nc -zv 192.168.1.100 50002
nc -zv 192.168.1.100 50003

# Windows
Test-NetConnection -ComputerName 192.168.1.100 -Port 50001
Test-NetConnection -ComputerName 192.168.1.100 -Port 50002
Test-NetConnection -ComputerName 192.168.1.100 -Port 50003
```

---

## 已知限制

1. **不支持设备自动发现**：必须手动指定 IP 和端口
2. **不支持服务端自动启动**：服务端必须预先启动
3. **不支持 --list-* 选项**：这些选项需要 ADB 支持
4. **端口必须连续**：video、audio、control 必须使用连续的 3 个端口
5. **仅支持 IPv4**：暂不支持 IPv6 和域名解析

---

## 故障排查

### 问题 1: 连接超时

**症状**：客户端一直尝试连接，最终超时失败

**可能原因**：
- 服务端未启动
- 防火墙阻止连接
- IP 地址或端口错误
- 网络不通

**解决方法**：
1. 确认服务端已启动并监听在指定端口
2. 检查防火墙规则
3. 使用 `ping` 和 `telnet`/`nc` 测试连通性
4. 检查 adb forward 配置

### 问题 2: 连接后立即断开

**症状**：客户端显示连接成功，但立即断开

**可能原因**：
- 服务端版本不匹配
- 端口映射配置错误
- 网络不稳定

**解决方法**：
1. 确保客户端和服务端版本一致
2. 检查 adb forward 配置是否正确
3. 使用有线网络而非 Wi-Fi

### 问题 3: 视频有但无音频

**症状**：视频正常显示，但没有声音

**可能原因**：
- 音频端口未正确转发
- 服务端音频捕获失败

**解决方法**：
1. 检查端口 50002 是否正确转发
2. 尝试使用 `--no-audio` 然后重新启用
3. 检查服务端日志

---

## 测试检查清单

- [ ] 编译成功，无错误
- [ ] 基本连接功能正常
- [ ] 视频流正常显示
- [ ] 音频流正常播放
- [ ] 控制功能（鼠标、键盘）正常
- [ ] `--no-audio` 参数生效
- [ ] `--no-video` 参数生效
- [ ] 无效 IP 格式正确报错
- [ ] 无效端口正确报错
- [ ] 与 `-s` 参数冲突检测正常
- [ ] 与 `--tcpip` 参数冲突检测正常
- [ ] 与 `--list-*` 参数冲突检测正常
- [ ] 连接超时处理正常
- [ ] 日志输出清晰易懂

---

## 总结

`--remote` 参数为 scrcpy 提供了更灵活的连接方式，特别适用于：
- 远程服务器场景
- 复杂网络环境
- 需要跳过 ADB 的场景
- 多客户端连接同一服务端

完成以上测试后，请将测试结果记录并报告任何发现的问题。

