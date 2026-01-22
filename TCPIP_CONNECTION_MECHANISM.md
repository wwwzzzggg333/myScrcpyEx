# Scrcpy TCP/IP 连接机制深度解析

## 问题引出

当使用 `scrcpy --tcpip=192.168.1.100:5555` 连接设备时：
- **Android设备上的服务端是如何启动的？**
- **服务端监听的地址和端口是如何确定的？**

---

## 🔍 完整连接流程剖析

### 流程概览

```
┌──────────┐                                    ┌──────────────┐
│  PC客户端 │                                    │ Android设备   │
│ (scrcpy) │                                    │              │
└─────┬────┘                                    └──────┬───────┘
      │                                                │
      │ 1. adb connect 192.168.1.100:5555              │
      │ ─────────────────────────────────────────────> │
      │                                                │
      │ 2. adb push scrcpy-server.jar                  │
      │ ─────────────────────────────────────────────> │
      │                                                │
      │ 3. 建立 adb 隧道 (reverse 或 forward)           │
      │    关键：确定 socket 名称和端口                  │
      │                                                │
      │ 4. adb shell 启动 Java 服务端                   │
      │ ─────────────────────────────────────────────> │
      │                                                │
      │                                         ┌──────▼─────┐
      │                                         │ Java Server│
      │                                         │  启动并读取 │
      │                                         │  socket名称 │
      │                                         └──────┬─────┘
      │                                                │
      │ 5. 服务端连接到 localabstract socket             │
      │    (通过 adb tunnel 映射到 PC)                   │
      │ <──────────────────────────────────────────────┤
      │                                                │
      │ 6. 三路连接建立:                                 │
      │    - 视频流 socket                              │
      │    - 音频流 socket                              │
      │    - 控制流 socket                              │
      │ <─────────────────────────────────────────────>│
      │                                                │
```

---

## 📝 详细步骤分解

### 步骤 1: 建立 ADB 连接

```bash
# 用户执行
scrcpy --tcpip=192.168.1.100:5555

# scrcpy 内部执行
adb connect 192.168.1.100:5555
```

**代码位置:** `server.c:sc_server_configure_tcpip_known_address()`

```c
// 如果设备未连接,先连接
if (!sc_adb_connect(&server->intr, ip_port, 0)) {
    return false;
}
```

此时建立的是 **ADB 协议连接** (通常在 5555 端口),用于后续的所有 adb 命令。

---

### 步骤 2: 推送服务端 JAR 文件

```c
// server.c:push_server()
bool push_server(struct sc_intr *intr, const char *serial) {
    char *server_path = get_server_path();
    
    // 实际执行:
    // adb -s 192.168.1.100:5555 push <本地路径> /data/local/tmp/scrcpy-server.jar
    bool ok = sc_adb_push(intr, serial, server_path, 
                          SC_DEVICE_SERVER_PATH, 0);
    
    return ok;
}
```

**关键常量:**
```c
#define SC_DEVICE_SERVER_PATH "/data/local/tmp/scrcpy-server.jar"
```

---

### 步骤 3: 生成唯一的 Socket 名称

**关键代码:** `server.c:run_server()`

```c
// 生成 8 位十六进制的 scid (scrcpy 实例 ID)
uint32_t scid = scrcpy_generate_scid();  // 例如: 0x12AB34CD

// 构造 socket 名称
int r = asprintf(&server->device_socket_name, 
                 SC_SOCKET_NAME_PREFIX "%08x", scid);
// 结果: "scrcpy_12ab34cd"
```

**为什么需要唯一 ID？**
- 允许同时运行多个 scrcpy 实例
- 每个实例使用不同的 socket 名称,避免冲突
- 例如:
  - 实例1: `scrcpy_12ab34cd`
  - 实例2: `scrcpy_56ef78ab`
  - 实例3: `scrcpy_9abc0def`

---

### 步骤 4: 建立 ADB 隧道 (关键步骤)

**重点：服务端并不直接监听 TCP 端口！**

#### 4.1 隧道类型选择

Scrcpy 优先尝试 `adb reverse`,失败则回退到 `adb forward`。

**代码:** `adb_tunnel.c:sc_adb_tunnel_open()`

```c
bool sc_adb_tunnel_open(...) {
    if (!force_adb_forward) {
        // 优先尝试 adb reverse
        if (enable_tunnel_reverse_any_port(tunnel, intr, serial,
                                           device_socket_name, port_range)) {
            return true;  // 成功
        }
        
        LOGW("'adb reverse' failed, fallback to 'adb forward'");
    }
    
    // 回退到 adb forward
    return enable_tunnel_forward_any_port(tunnel, intr, serial,
                                          device_socket_name, port_range);
}
```

#### 4.2 adb reverse 模式 (默认,推荐)

**原理图:**
```
┌─────────────────────────────────────────────────────────────────┐
│                         Android 设备                             │
│                                                                  │
│  ┌──────────────┐         adb reverse           ┌─────────────┐ │
│  │ scrcpy-server│  ───> localabstract:scrcpy_XXX │ adb daemon  │ │
│  │ (Java进程)    │       (Unix domain socket)     │             │ │
│  └──────────────┘                                └──────┬──────┘ │
│                                                          │        │
└──────────────────────────────────────────────────────────┼────────┘
                                                           │
                                    通过 adb 协议传输       │
                                    (通常是 TCP 5555端口)  │
                                                           │
┌──────────────────────────────────────────────────────────┼────────┐
│                           PC                             │        │
│  ┌────────────┐                                 ┌────────▼──────┐ │
│  │   scrcpy   │ <─── accept() 连接 ────────────│   监听socket   │ │
│  │  (客户端)  │      localhost:27183           │  (server_socket)│ │
│  └────────────┘                                 └───────────────┘ │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

**执行的命令:**
```bash
# 1. PC 监听本地端口 (例如 27183)
# 2. 建立反向隧道
adb -s 192.168.1.100:5555 reverse localabstract:scrcpy_12ab34cd tcp:27183
```

**含义:**
- 设备上连接到 `localabstract:scrcpy_12ab34cd` 的请求
- 会被转发到 PC 的 `localhost:27183`

**代码实现:** `adb_tunnel.c:enable_tunnel_reverse_any_port()`

```c
static bool enable_tunnel_reverse_any_port(...) {
    uint16_t port = port_range.first;  // 默认 27183
    
    for (;;) {
        // 执行 adb reverse
        if (!sc_adb_reverse(intr, serial, device_socket_name, port,
                            SC_ADB_NO_STDOUT)) {
            return false;  // adb reverse 命令失败
        }
        
        // PC 创建 server socket 监听该端口
        sc_socket server_socket = net_socket();
        bool ok = listen_on_port(intr, server_socket, port);
        
        if (ok) {
            // 成功!记录 socket 和端口
            tunnel->server_socket = server_socket;
            tunnel->local_port = port;
            tunnel->enabled = true;
            return true;
        }
        
        // 端口被占用,尝试下一个
        sc_adb_reverse_remove(intr, serial, device_socket_name, ...);
        port++;
        
        if (port > port_range.last) {
            LOGE("Could not listen on any port in range");
            return false;
        }
    }
}
```

**端口分配:**
- 默认范围: 27183-27199 (17个端口)
- scrcpy 需要 1 个端口 (PC监听,接受 3 路连接)
- 如果端口被占用,自动尝试下一个

#### 4.3 adb forward 模式 (回退方案)

**原理图:**
```
┌───────────────────────────────────────────────────────────────┐
│                         Android 设备                           │
│                                                                │
│  ┌──────────────┐                              ┌─────────────┐│
│  │ scrcpy-server│ ───> 监听 localabstract:XXX  │ adb daemon  ││
│  │ (Java进程)    │      (等待连接)              │             ││
│  └──────────────┘                              └──────┬──────┘│
│                                                        │       │
└────────────────────────────────────────────────────────┼───────┘
                                                         │
                                  通过 adb 协议传输       │
                                  (TCP 5555端口)         │
                                                         │
┌────────────────────────────────────────────────────────┼───────┐
│                           PC                           │       │
│  ┌────────────┐                             ┌──────────▼─────┐ │
│  │   scrcpy   │ ───> connect() ───────────>│ localhost:27183 │ │
│  │  (客户端)  │                             │  (adb forward)  │ │
│  └────────────┘                             └─────────────────┘ │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**执行的命令:**
```bash
adb -s 192.168.1.100:5555 forward tcp:27183 localabstract:scrcpy_12ab34cd
```

**含义:**
- PC 连接到 `localhost:27183`
- 会被转发到设备的 `localabstract:scrcpy_12ab34cd`

**代码实现:** `adb_tunnel.c:enable_tunnel_forward_any_port()`

```c
static bool enable_tunnel_forward_any_port(...) {
    tunnel->forward = true;  // 标记为 forward 模式
    
    uint16_t port = port_range.first;
    for (;;) {
        // 执行 adb forward
        if (sc_adb_forward(intr, serial, port, device_socket_name,
                           SC_ADB_NO_STDOUT)) {
            // 成功!记录端口
            tunnel->local_port = port;
            tunnel->enabled = true;
            return true;
        }
        
        // 端口被占用,尝试下一个
        port++;
        if (port > port_range.last) {
            return false;
        }
    }
}
```

---

### 步骤 5: 启动 Android 服务端

**执行的完整命令:**

```bash
adb -s 192.168.1.100:5555 shell \
    CLASSPATH=/data/local/tmp/scrcpy-server.jar \
    app_process \
    / \
    com.genymobile.scrcpy.Server \
    2.8 \
    scid=12ab34cd \
    log_level=info \
    video_codec=h264 \
    audio_codec=opus \
    max_size=0 \
    video_bit_rate=8000000 \
    audio_bit_rate=128000 \
    tunnel_forward=false \
    control=true \
    ...更多参数
```

**代码:** `server.c:execute_server()`

```c
static sc_pid execute_server(struct sc_server *server,
                              const struct sc_server_params *params) {
    const char *cmd[128];
    unsigned count = 0;
    
    // 构建命令
    cmd[count++] = sc_adb_get_executable();  // "adb"
    cmd[count++] = "-s";
    cmd[count++] = serial;                   // "192.168.1.100:5555"
    cmd[count++] = "shell";
    cmd[count++] = "CLASSPATH=" SC_DEVICE_SERVER_PATH;
    cmd[count++] = "app_process";
    cmd[count++] = "/";  // 工作目录 (unused)
    cmd[count++] = "com.genymobile.scrcpy.Server";  // 主类
    cmd[count++] = SCRCPY_VERSION;  // 版本 "2.8"
    
    // 添加所有参数
    ADD_PARAM("scid=%08x", params->scid);
    ADD_PARAM("log_level=%s", log_level_to_server_string(params->log_level));
    
    // tunnel_forward 参数告诉服务端使用哪种连接模式
    if (server->tunnel.forward) {
        ADD_PARAM("tunnel_forward=true");  // forward 模式
    }
    // 默认是 reverse 模式,不需要传参
    
    // ... 添加更多参数
    
    cmd[count++] = NULL;
    
    // 执行命令
    sc_pid pid = sc_adb_execute(cmd, 0);
    
    return pid;
}
```

**关键参数说明:**

| 参数 | 值示例 | 说明 |
|------|--------|------|
| `scid` | `12ab34cd` | Socket 名称的一部分 |
| `tunnel_forward` | `true/false` | 是否使用 forward 模式 |
| `log_level` | `info` | 日志级别 |
| `video_codec` | `h264` | 视频编码器 |
| `max_size` | `1920` | 最大分辨率 |

---

### 步骤 6: Java 服务端读取 Socket 名称

**Java 服务端代码逻辑 (server/src/main/.../Server.java):**

```java
public class Server {
    public static void main(String... args) {
        // 解析命令行参数
        Options options = Options.parse(args);
        
        // 从 scid 构造 socket 名称
        String socketName = "scrcpy_" + 
            String.format("%08x", options.getScid());
        // 例如: "scrcpy_12ab34cd"
        
        // 根据 tunnel_forward 参数决定连接方式
        if (options.getTunnelForward()) {
            // forward 模式: 服务端监听
            listenOnSocket(socketName);
        } else {
            // reverse 模式: 服务端主动连接
            connectToSocket(socketName);
        }
    }
    
    private void connectToSocket(String socketName) {
        // 连接到 Unix domain socket (localabstract)
        LocalSocket socket = new LocalSocket();
        socket.connect(new LocalSocketAddress(
            socketName, 
            LocalSocketAddress.Namespace.ABSTRACT
        ));
        
        // 通过 adb reverse 映射,实际连接到 PC
    }
}
```

**LocalAbstract Socket 说明:**
- **类型:** Unix domain socket (本地进程间通信)
- **命名空间:** ABSTRACT (不对应文件系统路径)
- **地址格式:** `localabstract:scrcpy_12ab34cd`
- **特点:**
  - 只在 Android 设备内可见
  - 不占用 TCP/IP 端口
  - 通过 adb reverse/forward 映射到 PC

---

### 步骤 7: 建立三路连接

**连接顺序:**

```c
// server.c:sc_server_connect_to()

if (!tunnel->forward) {
    // === reverse 模式: PC accept 连接 ===
    
    // 1. 接受视频流连接
    video_socket = net_accept_intr(&server->intr, tunnel->server_socket);
    
    // 2. 接受音频流连接
    audio_socket = net_accept_intr(&server->intr, tunnel->server_socket);
    
    // 3. 接受控制流连接
    control_socket = net_accept_intr(&server->intr, tunnel->server_socket);
    
} else {
    // === forward 模式: PC connect 连接 ===
    
    uint32_t tunnel_host = IPV4_LOCALHOST;  // 127.0.0.1
    uint16_t tunnel_port = tunnel->local_port;  // 例如 27183
    
    // 1. 连接视频流
    video_socket = connect_to_server(server, attempts, delay,
                                     tunnel_host, tunnel_port);
    
    // 2. 连接音频流
    audio_socket = net_socket();
    net_connect_intr(&server->intr, audio_socket, tunnel_host, tunnel_port);
    
    // 3. 连接控制流
    control_socket = net_socket();
    net_connect_intr(&server->intr, control_socket, tunnel_host, tunnel_port);
}
```

**连接时序:**

```
reverse 模式:
  Java Server        adb reverse          PC Client
       │                  │                    │
       ├─ connect #1 ───> │ ──────────────> accept #1 (video)
       │                  │                    │
       ├─ connect #2 ───> │ ──────────────> accept #2 (audio)
       │                  │                    │
       └─ connect #3 ───> │ ──────────────> accept #3 (control)

forward 模式:
  Java Server        adb forward         PC Client
       │                  │                    │
       ├─ listen ─────────┤                    │
       │                  │                    │
       │ <───────────────── accept #1 <──── connect #1 (video)
       │                  │                    │
       │ <───────────────── accept #2 <──── connect #2 (audio)
       │                  │                    │
       │ <───────────────── accept #3 <──── connect #3 (control)
```

---

## 🎯 关键问题解答

### Q1: 服务端监听什么地址和端口？

**答案: 服务端不监听 TCP 端口！**

服务端监听/连接的是 **Unix domain socket** (localabstract)：
- **地址:** `localabstract:scrcpy_XXXXXXXX`
- **命名空间:** ABSTRACT (抽象命名空间)
- **作用域:** 仅在 Android 设备本地可见

**为什么不用 TCP 端口？**
1. **安全性:** LocalAbstract socket 不暴露到网络
2. **权限:** 不需要 INTERNET 权限
3. **简洁:** 避免端口冲突
4. **性能:** 本地 socket 比 TCP loopback 更快

### Q2: PC 连接到哪个地址和端口？

**答案:**

**reverse 模式 (默认):**
- PC 监听: `127.0.0.1:27183` (或 27184-27199)
- PC 不主动连接,而是 accept 设备的连接

**forward 模式 (回退):**
- PC 连接到: `127.0.0.1:27183` (或 27184-27199)
- 该端口由 adb forward 映射到设备的 localabstract socket

### Q3: scid (Socket ID) 如何生成？

```c
// scrcpy.c:scrcpy_generate_scid()
static uint32_t scrcpy_generate_scid(void) {
    struct sc_rand rand;
    sc_rand_init(&rand);
    // 使用 31 位随机数 (避免 Java 端有符号数问题)
    return sc_rand_u32(&rand) & 0x7FFFFFFF;
}
```

**示例:**
- scid: `0x12ab34cd`
- socket 名称: `scrcpy_12ab34cd`

### Q4: 为什么优先使用 adb reverse？

**性能和可靠性对比:**

| 特性 | adb reverse | adb forward |
|------|-------------|-------------|
| **连接方向** | 设备 → PC | PC → 设备 |
| **连接建立** | 设备主动连接 | PC 主动连接 |
| **同步性** | PC 先监听,无需等待 | 需要等待设备启动 |
| **可靠性** | 更高 | 稍低 |
| **兼容性** | Android 5.0+ | 所有版本 |

**代码注释解释:**
```c
// adb_tunnel.c 注释:
// At the application level, the device part is "the server" because it
// serves video stream and control. However, at the network level, the
// client listens and the server connects to the client. That way, the
// client can listen before starting the server app, so there is no
// need to try to connect until the server socket is listening on the
// device.

// 应用层面:设备是"服务端"(提供视频流)
// 网络层面:PC监听,设备连接(reverse模式)
// 优点:PC可以先监听,无需等待设备服务启动
```

---

## 📊 完整时序图

```
时间 │ PC Client                │ ADB                  │ Android Device
─────┼──────────────────────────┼──────────────────────┼────────────────────
  0  │ scrcpy --tcpip=IP:5555   │                      │
     │                          │                      │
  1  │ adb connect IP:5555      │                      │
     │ ──────────────────────────> 建立 ADB 连接 ─────>│
     │                          │                      │
  2  │ adb push server.jar      │                      │
     │ ──────────────────────────> 推送文件 ──────────>│
     │                          │                      │
  3  │ 生成 scid: 12ab34cd      │                      │
     │ socket: scrcpy_12ab34cd  │                      │
     │                          │                      │
  4  │ 监听 localhost:27183     │                      │
     │ (server_socket)          │                      │
     │                          │                      │
  5  │ adb reverse              │                      │
     │ ──────────────────────────> 建立隧道 ──────────>│
     │ localabstract:scrcpy_*   │ <-> tcp:27183        │
     │                          │                      │
  6  │ adb shell app_process    │                      │
     │ ──────────────────────────> 启动服务端 ────────>│
     │                          │                      │ Java Server 启动
     │                          │                      │ 解析参数
     │                          │                      │
  7  │                          │                      │ 连接 localabstract:
     │                          │ <─── connect #1 ─────┤   scrcpy_12ab34cd
     │ accept(video) <──────────┤                      │
     │                          │                      │
  8  │                          │ <─── connect #2 ─────┤
     │ accept(audio) <──────────┤                      │
     │                          │                      │
  9  │                          │ <─── connect #3 ─────┤
     │ accept(control) <────────┤                      │
     │                          │                      │
 10  │ 发送第一个字节(握手)       │                      │
     │ ───────────────────────────────────────────────>│
     │                          │                      │
 11  │                          │ <────────────────────┤ 发送设备信息
     │ 接收设备名称 <───────────┤                      │
     │                          │                      │
 12  │ 开始接收视频/音频流        │ <─────────────────────┤ 开始编码和发送
     │ 开始发送控制消息          │ ──────────────────────>│ 接收输入控制
     │                          │                      │
```

---

## 💡 实际示例

### 示例 1: reverse 模式 (默认)

```bash
# 1. 用户执行
$ scrcpy --tcpip=192.168.1.100:5555

# 2. scrcpy 内部执行 (可通过 adb logcat 查看)

# 连接设备
adb connect 192.168.1.100:5555

# 推送服务端
adb -s 192.168.1.100:5555 push scrcpy-server.jar /data/local/tmp/

# 生成 scid (假设为 0x1a2b3c4d)
# Socket 名称: scrcpy_1a2b3c4d

# 监听本地端口
# (scrcpy 内部在 localhost:27183 监听)

# 建立反向隧道
adb -s 192.168.1.100:5555 reverse localabstract:scrcpy_1a2b3c4d tcp:27183

# 启动服务端
adb -s 192.168.1.100:5555 shell CLASSPATH=/data/local/tmp/scrcpy-server.jar \
    app_process / com.genymobile.scrcpy.Server 2.8 \
    scid=1a2b3c4d \
    log_level=info \
    video_codec=h264 \
    audio_codec=opus \
    tunnel_forward=false \
    ...

# 3. Java Server 在设备上启动
# 连接到 localabstract:scrcpy_1a2b3c4d
# 通过 adb reverse 映射到 PC 的 localhost:27183

# 4. PC accept 3 个连接
# - video socket
# - audio socket  
# - control socket

# 5. 开始镜像
```

### 示例 2: forward 模式 (回退)

```bash
# 当 reverse 不可用时 (例如旧设备)

# 前面步骤相同,到建立隧道时:

# 使用 forward 而不是 reverse
adb -s 192.168.1.100:5555 forward tcp:27183 localabstract:scrcpy_1a2b3c4d

# 启动服务端,注意 tunnel_forward=true
adb -s 192.168.1.100:5555 shell CLASSPATH=/data/local/tmp/scrcpy-server.jar \
    app_process / com.genymobile.scrcpy.Server 2.8 \
    scid=1a2b3c4d \
    tunnel_forward=true \
    ...

# Java Server 监听 localabstract:scrcpy_1a2b3c4d
# (等待连接)

# PC 主动连接 localhost:27183
# 通过 adb forward 映射到设备的 localabstract socket
```

---

## 🔧 调试技巧

### 查看 adb 隧道

```bash
# 查看 forward 隧道
adb forward --list

# 输出示例:
# 192.168.1.100:5555 tcp:27183 localabstract:scrcpy_1a2b3c4d

# 查看 reverse 隧道 (需要 shell 权限)
adb shell dumpsys connectivity | grep scrcpy
```

### 查看服务端日志

```bash
# 实时查看 scrcpy 服务端日志
adb logcat | grep scrcpy

# 或者直接查看 stdout (scrcpy 继承了 shell 的输出)
```

### 查看进程和 Socket

```bash
# 查看 scrcpy-server 进程
adb shell ps | grep app_process

# 查看 localabstract socket (需要 root)
adb shell su -c "ls -la /proc/net/unix" | grep scrcpy
```

---

## 📚 总结

### 核心要点

1. **服务端不监听 TCP 端口**,而是使用 **Unix domain socket** (localabstract)

2. **Socket 名称由 scid 决定**: `scrcpy_XXXXXXXX` (8位十六进制随机数)

3. **adb 隧道负责映射**:
   - reverse 模式: `localabstract:scrcpy_* <-> PC:27183`
   - forward 模式: `PC:27183 <-> localabstract:scrcpy_*`

4. **PC 使用的端口**:
   - 默认: 27183-27199
   - 可通过 `--port` 自定义
   - 只需要 1 个端口(3 路连接复用)

5. **连接方向**:
   - reverse: 设备主动连接 PC (推荐)
   - forward: PC 主动连接设备 (回退)

6. **启动命令**: 通过 `adb shell app_process` 启动 Java 服务端

7. **参数传递**: 服务端通过命令行参数接收所有配置

### 安全性

- localabstract socket 不暴露到网络
- 只能通过 adb 访问
- 不需要设备上的额外权限
- scid 随机性防止冲突

---

**文档版本**: 1.0  
**更新日期**: 2026-01-23  
**基于代码**: scrcpy 2.8

