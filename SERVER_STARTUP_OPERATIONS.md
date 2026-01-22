# Scrcpy 启动时对服务端的操作详解

## 📋 目录
1. [操作概览](#操作概览)
2. [详细操作步骤](#详细操作步骤)
3. [服务端 JAR 文件管理](#服务端-jar-文件管理)
4. [隧道建立](#隧道建立)
5. [服务端启动](#服务端启动)
6. [连接建立](#连接建立)
7. [特殊模式](#特殊模式)

---

## 操作概览

当你执行 `scrcpy` 启动时，客户端对服务端执行以下**7个关键操作**：

```
┌─────────────────────────────────────────────────────────┐
│             Scrcpy 服务端启动操作流程                      │
└─────────────────────────────────────────────────────────┘

1. 启动 ADB 服务器
   └─> adb start-server

2. 选择/连接目标设备
   └─> adb devices / adb connect

3. 推送服务端 JAR 文件
   └─> adb push scrcpy-server.jar /data/local/tmp/

4. 生成唯一 Socket 名称
   └─> scrcpy_XXXXXXXX (随机 scid)

5. 建立 ADB 隧道
   ├─> adb reverse (优先)
   └─> adb forward (回退)

6. 启动服务端进程
   └─> adb shell app_process (传递所有参数)

7. 建立三路连接
   ├─> 视频流 Socket
   ├─> 音频流 Socket
   └─> 控制流 Socket
```

---

## 详细操作步骤

### 操作 1: 启动 ADB 服务器

**目的:** 确保 ADB daemon 正在运行

**执行命令:**
```bash
adb start-server
```

**代码位置:** `server.c:run_server()`

```c
// Execute "adb start-server" before "adb devices"
bool ok = sc_adb_start_server(&server->intr, 0);
if (!ok) {
    LOGE("Could not start adb server");
    goto error_connection_failed;
}
```

**作用:**
- 如果 ADB server 未运行，启动它
- 如果已运行，确认其状态
- 失败会导致整个启动流程中止

**日志输出:**
```
* daemon not running; starting now at tcp:5037
* daemon started successfully
```

---

### 操作 2: 选择/连接目标设备

**目的:** 确定要镜像的 Android 设备

#### 2.1 设备选择逻辑

```c
bool need_initial_serial = !params->tcpip_dst;

if (need_initial_serial) {
    // 从已连接设备中选择
    struct sc_adb_device_selector selector;
    
    if (params->req_serial) {
        // 使用 -s 参数指定
        selector.type = SC_ADB_DEVICE_SELECT_SERIAL;
        selector.serial = params->req_serial;
    } else if (params->select_usb) {
        // 使用 -d 选择 USB 设备
        selector.type = SC_ADB_DEVICE_SELECT_USB;
    } else if (params->select_tcpip) {
        // 使用 -e 选择 TCP/IP 设备
        selector.type = SC_ADB_DEVICE_SELECT_TCPIP;
    } else {
        // 检查环境变量或自动选择
        const char *env_serial = getenv("ANDROID_SERIAL");
        if (env_serial) {
            selector.type = SC_ADB_DEVICE_SELECT_SERIAL;
            selector.serial = env_serial;
        } else {
            selector.type = SC_ADB_DEVICE_SELECT_ALL;
        }
    }
    
    // 执行设备选择
    ok = sc_adb_select_device(&server->intr, &selector, 0, &device);
}
```

#### 2.2 执行的命令

**获取设备列表:**
```bash
adb devices -l
```

**输出示例:**
```
List of devices attached
R3CN90ABCD              device product:SM-G991B model:SM_G991B device:o1s
192.168.1.100:5555      device product:...
emulator-5554           device product:sdk_gphone...
```

#### 2.3 TCP/IP 连接处理

如果使用 `--tcpip=IP:5555`，会自动执行连接：

```c
if (params->tcpip_dst) {
    // 主动连接到指定地址
    ok = sc_server_configure_tcpip_known_address(server, tcpip_dst, plus);
    
    // 内部执行: adb connect IP:5555
}
```

**执行命令:**
```bash
adb connect 192.168.1.100:5555
```

---

### 操作 3: 推送服务端 JAR 文件

**目的:** 将 scrcpy-server.jar 传输到 Android 设备

**代码位置:** `server.c:push_server()`

```c
static bool push_server(struct sc_intr *intr, const char *serial) {
    // 1. 获取本地服务端文件路径
    char *server_path = get_server_path();
    
    // 2. 检查文件是否存在
    if (!sc_file_is_regular(server_path)) {
        LOGE("'%s' does not exist or is not a regular file\n", server_path);
        return false;
    }
    
    // 3. 推送到设备
    bool ok = sc_adb_push(intr, serial, server_path, 
                          SC_DEVICE_SERVER_PATH, 0);
    
    return ok;
}
```

#### 3.1 服务端文件路径查找

**查找顺序:**

1. **环境变量 `SCRCPY_SERVER_PATH`**
   ```bash
   export SCRCPY_SERVER_PATH=/path/to/custom/scrcpy-server.jar
   ```

2. **系统安装路径** (非 PORTABLE 模式)
   ```
   PREFIX/share/scrcpy/scrcpy-server
   例如: /usr/local/share/scrcpy/scrcpy-server
   ```

3. **可执行文件同目录** (PORTABLE 模式)
   ```
   ./scrcpy-server
   ```

**代码实现:**

```c
static char *get_server_path(void) {
    // 1. 检查环境变量
    char *server_path = sc_get_env("SCRCPY_SERVER_PATH");
    if (server_path) {
        LOGD("Using SCRCPY_SERVER_PATH: %s", server_path);
        return server_path;
    }
    
#ifndef PORTABLE
    // 2. 使用安装路径
    LOGD("Using server: " SC_SERVER_PATH_DEFAULT);
    server_path = strdup(SC_SERVER_PATH_DEFAULT);
#else
    // 3. 使用可执行文件同目录
    server_path = sc_file_get_local_path(SC_SERVER_FILENAME);
    LOGD("Using server (portable): %s", server_path);
#endif
    
    return server_path;
}
```

#### 3.2 执行的命令

```bash
adb -s <serial> push <本地路径> /data/local/tmp/scrcpy-server.jar
```

**实际示例:**
```bash
adb -s R3CN90ABCD push /usr/local/share/scrcpy/scrcpy-server /data/local/tmp/scrcpy-server.jar
```

**推送过程日志:**
```
Pushing scrcpy-server.jar...
/usr/local/share/scrcpy/scrcpy-server: 1 file pushed, 0 skipped. 
52.4 MB/s (66432 bytes in 0.001s)
```

#### 3.3 目标路径

**设备上的固定路径:**
```c
#define SC_DEVICE_SERVER_PATH "/data/local/tmp/scrcpy-server.jar"
```

**特点:**
- `/data/local/tmp/` 目录无需 root 权限
- 对所有应用可访问
- 临时目录，重启后可能清空
- 每次启动都会覆盖旧文件

---

### 操作 4: 生成唯一 Socket 名称

**目的:** 为本次 scrcpy 会话创建唯一标识，支持多实例运行

**代码位置:** `server.c:run_server()`

```c
// 生成 8 位十六进制的 scid (scrcpy instance id)
uint32_t scid = scrcpy_generate_scid();

// 构造 socket 名称
int r = asprintf(&server->device_socket_name, 
                 SC_SOCKET_NAME_PREFIX "%08x", 
                 params->scid);

// 结果: "scrcpy_12ab34cd"
```

**scid 生成算法:**

```c
// scrcpy.c:scrcpy_generate_scid()
static uint32_t scrcpy_generate_scid(void) {
    struct sc_rand rand;
    sc_rand_init(&rand);
    
    // 只使用 31 位避免 Java 端有符号数问题
    return sc_rand_u32(&rand) & 0x7FFFFFFF;
}
```

**Socket 名称格式:**
```
scrcpy_XXXXXXXX
│      └─ 8 位十六进制 scid
└─ 固定前缀
```

**示例:**
- `scrcpy_1a2b3c4d`
- `scrcpy_9f8e7d6c`
- `scrcpy_00112233`

**作用:**
- 允许同时运行多个 scrcpy 实例
- 每个实例使用不同的 socket 名称
- 避免不同会话之间的冲突

---

### 操作 5: 建立 ADB 隧道

**目的:** 在 PC 和设备之间建立网络通道

**代码位置:** `server.c:run_server()`

```c
ok = sc_adb_tunnel_open(&server->tunnel, &server->intr, serial,
                        server->device_socket_name, params->port_range,
                        params->force_adb_forward);
```

#### 5.1 隧道模式选择

**优先级:**
1. **adb reverse** (默认，推荐)
2. **adb forward** (回退)

**代码逻辑:** `adb_tunnel.c:sc_adb_tunnel_open()`

```c
bool sc_adb_tunnel_open(..., bool force_adb_forward) {
    if (!force_adb_forward) {
        // 优先尝试 adb reverse
        if (enable_tunnel_reverse_any_port(...)) {
            return true;  // 成功
        }
        
        LOGW("'adb reverse' failed, fallback to 'adb forward'");
    }
    
    // 回退到 adb forward
    return enable_tunnel_forward_any_port(...);
}
```

#### 5.2 adb reverse 模式 (默认)

**执行命令:**
```bash
adb -s <serial> reverse localabstract:scrcpy_12ab34cd tcp:27183
```

**含义:**
- 设备连接到 `localabstract:scrcpy_12ab34cd` 
- 自动转发到 PC 的 `localhost:27183`

**工作原理:**
```
Android 设备                      adb reverse                     PC
    │                                │                            │
    │ connect localabstract          │                            │
    │ ─────────────────────────>     │ ─────────────────────> accept()
    │   scrcpy_12ab34cd              │     映射到 tcp:27183       │ 监听 27183
```

**代码实现:** `adb_tunnel.c:enable_tunnel_reverse_any_port()`

```c
static bool enable_tunnel_reverse_any_port(...) {
    uint16_t port = port_range.first;  // 默认 27183
    
    for (;;) {
        // 执行 adb reverse
        if (!sc_adb_reverse(intr, serial, device_socket_name, port, ...)) {
            return false;
        }
        
        // PC 监听该端口
        sc_socket server_socket = net_socket();
        bool ok = listen_on_port(intr, server_socket, port);
        
        if (ok) {
            // 成功
            tunnel->server_socket = server_socket;
            tunnel->local_port = port;
            tunnel->enabled = true;
            return true;
        }
        
        // 端口被占用，尝试下一个
        sc_adb_reverse_remove(intr, serial, device_socket_name, ...);
        port++;
    }
}
```

**端口范围:**
- 默认: 27183-27199 (17 个端口)
- 可通过 `--port` 参数自定义
- 如果端口被占用，自动尝试下一个

#### 5.3 adb forward 模式 (回退)

**执行命令:**
```bash
adb -s <serial> forward tcp:27183 localabstract:scrcpy_12ab34cd
```

**含义:**
- PC 连接到 `localhost:27183`
- 自动转发到设备的 `localabstract:scrcpy_12ab34cd`

**工作原理:**
```
Android 设备                      adb forward                     PC
    │                                │                            │
    │ listen localabstract           │                            │
    │   scrcpy_12ab34cd       <─────────────────────── connect()  │
    │                                │     映射 tcp:27183          │ 连接 27183
```

**使用场景:**
- 旧设备不支持 adb reverse (Android < 5.0)
- TCP/IP 连接时 reverse 失败
- 使用 `--force-adb-forward` 参数强制

---

### 操作 6: 启动服务端进程

**目的:** 在 Android 设备上启动 Java 服务端进程

**代码位置:** `server.c:execute_server()`

#### 6.1 构建启动命令

```c
static sc_pid execute_server(struct sc_server *server,
                              const struct sc_server_params *params) {
    const char *cmd[128];
    unsigned count = 0;
    
    // 基础命令
    cmd[count++] = sc_adb_get_executable();  // "adb"
    cmd[count++] = "-s";
    cmd[count++] = serial;                   // 设备序列号
    cmd[count++] = "shell";
    cmd[count++] = "CLASSPATH=" SC_DEVICE_SERVER_PATH;
    cmd[count++] = "app_process";
    cmd[count++] = "/";                      // 工作目录
    cmd[count++] = "com.genymobile.scrcpy.Server";
    cmd[count++] = SCRCPY_VERSION;           // 例如 "2.8"
    
    // 添加所有参数
    ADD_PARAM("scid=%08x", params->scid);
    ADD_PARAM("log_level=%s", log_level_to_server_string(params->log_level));
    
    if (server->tunnel.forward) {
        ADD_PARAM("tunnel_forward=true");
    }
    
    // 视频参数
    if (!params->video) {
        ADD_PARAM("video=false");
    }
    if (params->video_bit_rate) {
        ADD_PARAM("video_bit_rate=%" PRIu32, params->video_bit_rate);
    }
    if (params->video_codec != SC_CODEC_H264) {
        ADD_PARAM("video_codec=%s", sc_server_get_codec_name(params->video_codec));
    }
    if (params->max_size) {
        ADD_PARAM("max_size=%" PRIu16, params->max_size);
    }
    
    // 音频参数
    if (!params->audio) {
        ADD_PARAM("audio=false");
    }
    if (params->audio_bit_rate) {
        ADD_PARAM("audio_bit_rate=%" PRIu32, params->audio_bit_rate);
    }
    if (params->audio_codec != SC_CODEC_OPUS) {
        ADD_PARAM("audio_codec=%s", sc_server_get_codec_name(params->audio_codec));
    }
    
    // 控制参数
    if (!params->control) {
        ADD_PARAM("control=false");
    }
    
    // 显示参数
    if (params->display_id) {
        ADD_PARAM("display_id=%" PRIu32, params->display_id);
    }
    if (params->show_touches) {
        ADD_PARAM("show_touches=true");
    }
    if (params->stay_awake) {
        ADD_PARAM("stay_awake=true");
    }
    
    // ... 更多参数
    
    cmd[count++] = NULL;
    
    // 执行命令
    sc_pid pid = sc_adb_execute(cmd, 0);
    
    return pid;
}
```

#### 6.2 实际执行的命令

**完整命令示例:**

```bash
adb -s R3CN90ABCD shell \
    CLASSPATH=/data/local/tmp/scrcpy-server.jar \
    app_process / com.genymobile.scrcpy.Server \
    2.8 \
    scid=1a2b3c4d \
    log_level=info \
    video_codec=h264 \
    audio_codec=opus \
    video_bit_rate=8000000 \
    audio_bit_rate=128000 \
    max_size=0 \
    max_fps=0 \
    tunnel_forward=false \
    control=true \
    display_id=0 \
    show_touches=false \
    stay_awake=false \
    clipboard_autosync=true \
    power_on=true \
    cleanup=true
```

#### 6.3 服务端参数说明

**关键参数:**

| 参数 | 示例值 | 说明 |
|------|--------|------|
| `scid` | `1a2b3c4d` | Socket 名称的一部分 |
| `log_level` | `info` | 日志级别 (verbose/debug/info/warn/error) |
| `tunnel_forward` | `false` | 是否使用 forward 模式 |
| `video` | `true` | 是否启用视频 |
| `video_codec` | `h264` | 视频编码器 (h264/h265/av1) |
| `video_bit_rate` | `8000000` | 视频比特率 (8Mbps) |
| `max_size` | `1920` | 最大分辨率 |
| `max_fps` | `60` | 最大帧率 |
| `audio` | `true` | 是否启用音频 |
| `audio_codec` | `opus` | 音频编码器 (opus/aac/flac/raw) |
| `audio_bit_rate` | `128000` | 音频比特率 (128Kbps) |
| `audio_source` | `output` | 音频源 |
| `control` | `true` | 是否启用控制 |
| `display_id` | `0` | 显示器 ID |
| `show_touches` | `false` | 是否显示触摸点 |
| `stay_awake` | `false` | 是否保持唤醒 |

**参数传递规则:**
- 所有参数都是 `key=value` 格式
- 布尔值用 `true`/`false`
- 数值直接传递
- 字符串需要验证特殊字符

#### 6.4 app_process 说明

**`app_process` 是什么？**
- Android 的 Java 进程启动器
- 用于在 shell 环境中运行 Java 应用
- 类似于 PC 上的 `java` 命令

**工作原理:**
```bash
CLASSPATH=/path/to/app.jar app_process / com.package.MainClass arg1 arg2
│                           │            │                       │
│                           │            │                       └─ 传递给 main() 的参数
│                           │            └─ Java 主类（包含 main 方法）
│                           └─ 工作目录（这里用 / 表示不重要）
└─ 设置 classpath
```

**对于 scrcpy:**
```bash
CLASSPATH=/data/local/tmp/scrcpy-server.jar \
    app_process / com.genymobile.scrcpy.Server \
    <参数...>
```

等同于 Java 端:
```java
package com.genymobile.scrcpy;

public class Server {
    public static void main(String[] args) {
        // args[0] = "2.8" (版本)
        // args[1] = "scid=1a2b3c4d"
        // args[2] = "log_level=info"
        // ... 解析所有参数
    }
}
```

---

### 操作 7: 建立三路连接

**目的:** 在 PC 和设备之间建立视频、音频、控制三个独立的 Socket 连接

**代码位置:** `server.c:sc_server_connect_to()`

#### 7.1 连接建立逻辑

```c
bool sc_server_connect_to(struct sc_server *server, 
                          struct sc_server_info *info) {
    struct sc_adb_tunnel *tunnel = &server->tunnel;
    
    bool video = server->params.video;
    bool audio = server->params.audio;
    bool control = server->params.control;
    
    if (!tunnel->forward) {
        // === reverse 模式: PC accept 连接 ===
        
        if (video) {
            video_socket = net_accept_intr(&server->intr, 
                                          tunnel->server_socket);
        }
        
        if (audio) {
            audio_socket = net_accept_intr(&server->intr, 
                                          tunnel->server_socket);
        }
        
        if (control) {
            control_socket = net_accept_intr(&server->intr, 
                                            tunnel->server_socket);
        }
        
    } else {
        // === forward 模式: PC connect 连接 ===
        
        uint32_t tunnel_host = IPV4_LOCALHOST;
        uint16_t tunnel_port = tunnel->local_port;
        
        unsigned attempts = 100;
        sc_tick delay = SC_TICK_FROM_MS(100);
        
        if (video) {
            video_socket = connect_to_server(server, attempts, delay,
                                            tunnel_host, tunnel_port);
        }
        
        if (audio) {
            audio_socket = net_socket();
            net_connect_intr(&server->intr, audio_socket, 
                           tunnel_host, tunnel_port);
        }
        
        if (control) {
            control_socket = net_socket();
            net_connect_intr(&server->intr, control_socket,
                           tunnel_host, tunnel_port);
        }
    }
    
    // 关闭 adb 隧道
    sc_adb_tunnel_close(tunnel, &server->intr, serial,
                       server->device_socket_name);
    
    // 读取设备信息
    device_read_info(&server->intr, first_socket, info);
    
    return true;
}
```

#### 7.2 连接顺序

**reverse 模式:**
```
时间  │  Java Server (设备)           │  PC Client
──────┼──────────────────────────────┼─────────────────────
  1   │  connect #1 (video)          │  
  2   │    └─> localabstract socket  │  accept #1 → video_socket
  3   │  connect #2 (audio)          │
  4   │    └─> localabstract socket  │  accept #2 → audio_socket
  5   │  connect #3 (control)        │
  6   │    └─> localabstract socket  │  accept #3 → control_socket
```

**forward 模式:**
```
时间  │  Java Server (设备)           │  PC Client
──────┼──────────────────────────────┼─────────────────────
  1   │  listen localabstract        │
  2   │    (等待连接)                │  connect #1 → video_socket
  3   │  accept #1 ← socket          │
  4   │  accept #2 ← socket          │  connect #2 → audio_socket
  5   │  accept #3 ← socket          │  connect #3 → control_socket
```

#### 7.3 握手验证

**PC 发送第一个字节:**

```c
// server.c:connect_and_read_byte()
static bool connect_and_read_byte(..., sc_socket socket, ...) {
    bool ok = net_connect_intr(intr, socket, host, port);
    if (!ok) {
        return false;
    }
    
    char byte;
    // 读取一个字节确认连接有效
    if (net_recv_intr(intr, socket, &byte, 1) != 1) {
        // 服务端未在监听
        return false;
    }
    
    return true;
}
```

**设备返回设备信息:**

```c
// server.c:device_read_info()
static bool device_read_info(struct sc_intr *intr, sc_socket device_socket,
                            struct sc_server_info *info) {
    uint8_t buf[SC_DEVICE_NAME_FIELD_LENGTH];
    
    // 读取设备名称 (64 字节)
    ssize_t r = net_recv_all_intr(intr, device_socket, buf, sizeof(buf));
    if (r < SC_DEVICE_NAME_FIELD_LENGTH) {
        LOGE("Could not retrieve device information");
        return false;
    }
    
    // 复制设备名称
    buf[SC_DEVICE_NAME_FIELD_LENGTH - 1] = '\0';
    memcpy(info->device_name, (char *) buf, sizeof(info->device_name));
    
    return true;
}
```

**设备名称示例:**
- "Samsung Galaxy S21"
- "Pixel 6"
- "Android SDK built for x86"

#### 7.4 关闭 adb 隧道

**连接建立后立即关闭隧道:**

```c
// 关闭 adb 隧道 (不再需要)
sc_adb_tunnel_close(tunnel, &server->intr, serial,
                   server->device_socket_name);
```

**执行的命令:**

**reverse 模式:**
```bash
adb -s <serial> reverse --remove localabstract:scrcpy_12ab34cd
```

**forward 模式:**
```bash
adb -s <serial> forward --remove tcp:27183
```

**为什么要关闭？**
- 连接已建立，直接使用 Socket 通信
- 不再需要 adb 作为中间层
- 释放 adb 资源
- 允许其他 scrcpy 实例使用相同端口

---

## 特殊模式

### 模式 1: 列表模式 (--list-*)

**目的:** 列出设备信息（编码器、显示器、相机等），不启动镜像

**执行流程:**
```
1. 启动 ADB 服务器
2. 选择设备
3. 推送服务端 JAR
4. 启动服务端 (带 list 参数)
5. 等待服务端输出信息并退出
6. 关闭连接
```

**代码:**
```c
if (params->list) {
    sc_pid pid = execute_server(server, params);
    if (pid == SC_PROCESS_NONE) {
        goto error_connection_failed;
    }
    
    // 等待服务端进程结束
    sc_process_wait(pid, NULL);  // 忽略退出码
    sc_process_close(pid);
    
    // 不建立连接，直接返回
    server->cbs->on_connected(server, server->cbs_userdata);
    return 0;
}
```

**服务端参数:**
```bash
list_encoders=true    # --list-encoders
list_displays=true    # --list-displays
list_cameras=true     # --list-cameras
list_camera_sizes=true  # --list-camera-sizes
list_apps=true        # --list-apps
```

**示例输出:**
```bash
$ scrcpy --list-encoders
--video-codec=h264 --video-encoder='OMX.qcom.video.encoder.avc'
--video-codec=h264 --video-encoder='c2.android.avc.encoder'
--video-codec=h265 --video-encoder='OMX.qcom.video.encoder.hevc'
...
```

### 模式 2: OTG 模式 (--otg)

**目的:** USB 直连模式，不经过 adb，只支持输入控制

**特点:**
- 不推送服务端 JAR
- 不启动 Java 服务端
- 直接使用 USB AOA 协议
- 仅支持键盘、鼠标、游戏手柄输入
- 不支持视频、音频

**执行流程:**
```
1. 初始化 USB 连接
2. 启用 AOA (Android Open Accessory) 模式
3. 创建 HID 设备 (键盘/鼠标/手柄)
4. 直接发送 HID 报告
```

**代码:** `usb/scrcpy_otg.c`

### 模式 3: TCP/IP 自动切换 (--tcpip)

**目的:** 从 USB 自动切换到无线连接

**执行流程:**
```
1. 通过 USB 连接设备
2. 执行 adb tcpip 5555 (启用无线调试)
3. 获取设备 IP 地址
4. 执行 adb connect IP:5555
5. 继续正常流程
```

**代码:** `server.c:sc_server_switch_to_tcpip()`

```c
static char *sc_server_switch_to_tcpip(struct sc_server *server, 
                                       const char *serial) {
    // 1. 获取设备 IP
    char *ip = sc_adb_get_device_ip(intr, serial, 0);
    
    // 2. 启用 TCP/IP 模式
    bool ok = sc_adb_tcpip(intr, serial, SC_ADB_PORT_DEFAULT, ...);
    
    // 3. 等待模式启用
    ok = wait_tcpip_mode_enabled(server, serial, 5555, attempts, delay);
    
    // 4. 构造 IP:端口
    char *ip_port = append_port(ip, 5555);
    
    return ip_port;
}
```

---

## 📊 完整时序图

```
时间  │ PC Client              │ ADB                │ Android Device
──────┼────────────────────────┼────────────────────┼──────────────────────
  0   │ scrcpy                 │                    │
      │                        │                    │
  1   │ adb start-server       │                    │
      │ ──────────────────────>│ 启动 daemon         │
      │                        │                    │
  2   │ adb devices -l         │                    │
      │ ──────────────────────>│ 查询设备列表        │
      │ <──────────────────────│                    │
      │                        │                    │
  3   │ 选择设备               │                    │
      │ serial = "R3CN90ABCD"  │                    │
      │                        │                    │
  4   │ adb push server.jar    │                    │
      │ ──────────────────────>│ 传输文件 ─────────>│ /data/local/tmp/
      │                        │                    │
  5   │ 生成 scid              │                    │
      │ socket = scrcpy_1a2b3c4d                   │
      │                        │                    │
  6   │ 监听 localhost:27183   │                    │
      │                        │                    │
  7   │ adb reverse            │                    │
      │ ──────────────────────>│ 建立隧道 ─────────>│
      │ localabstract ↔ tcp:27183                  │
      │                        │                    │
  8   │ adb shell app_process  │                    │
      │ ──────────────────────>│ 启动服务端 ───────>│ Java Server 启动
      │                        │                    │ 解析参数
      │                        │                    │ 初始化编码器
      │                        │                    │
  9   │                        │ <─ connect #1 ─────│ 连接 video socket
      │ accept(video) <────────┤                    │
      │                        │                    │
 10   │                        │ <─ connect #2 ─────│ 连接 audio socket
      │ accept(audio) <────────┤                    │
      │                        │                    │
 11   │                        │ <─ connect #3 ─────│ 连接 control socket
      │ accept(control) <──────┤                    │
      │                        │                    │
 12   │ 握手验证               │                    │
      │ ──────────────────────────────────────────>│ 发送 1 字节
      │ <──────────────────────────────────────────│ 返回设备信息
      │                        │                    │
 13   │ 关闭 adb 隧道          │                    │
      │ adb reverse --remove   │                    │
      │ ──────────────────────>│                    │
      │                        │                    │
 14   │ 开始接收视频/音频       │                    │ 开始编码和发送
      │ 开始发送控制消息        │                    │ 接收输入控制
      │ <─────────────────────────────────────────>│
      │                        │                    │
```

---

## 🔧 调试技巧

### 1. 查看 adb 命令执行

```bash
# 设置 adb 跟踪
export ADB_TRACE=all

# 运行 scrcpy
scrcpy

# 会看到所有 adb 命令
```

### 2. 查看服务端日志

```bash
# 实时查看设备日志
adb logcat | grep scrcpy

# 或使用 verbose 模式
scrcpy -V verbose
```

### 3. 手动复现流程

```bash
# 1. 启动 adb
adb start-server

# 2. 查看设备
adb devices -l

# 3. 推送服务端
adb push scrcpy-server /data/local/tmp/scrcpy-server.jar

# 4. 建立隧道
adb reverse localabstract:scrcpy_test tcp:27183

# 5. 启动服务端
adb shell CLASSPATH=/data/local/tmp/scrcpy-server.jar \
    app_process / com.genymobile.scrcpy.Server \
    2.8 scid=12345678 log_level=debug

# 6. 在另一个终端连接 (需要自己实现客户端)
nc localhost 27183
```

### 4. 检查服务端文件

```bash
# 查看设备上的服务端文件
adb shell ls -lh /data/local/tmp/scrcpy-server.jar

# 输出:
# -rw-rw-rw- 1 shell shell 64K 2026-01-23 10:30 scrcpy-server.jar

# 查看文件权限
adb shell stat /data/local/tmp/scrcpy-server.jar
```

### 5. 检查隧道状态

```bash
# 查看 forward 隧道
adb forward --list

# 输出:
# R3CN90ABCD tcp:27183 localabstract:scrcpy_1a2b3c4d

# 查看 reverse 隧道 (需要在设备上)
adb shell dumpsys connectivity | grep scrcpy
```

---

## 📚 总结

### 7 个关键操作

1. ✅ **启动 ADB 服务器** - 确保 adb daemon 运行
2. ✅ **选择/连接设备** - 确定目标设备
3. ✅ **推送服务端 JAR** - 传输 scrcpy-server.jar
4. ✅ **生成 Socket 名称** - 创建唯一标识 (scrcpy_XXXXXXXX)
5. ✅ **建立 ADB 隧道** - reverse 或 forward 模式
6. ✅ **启动服务端进程** - app_process 执行 Java 服务端
7. ✅ **建立三路连接** - 视频、音频、控制 Socket

### 关键路径

```
PC 本地文件                  设备文件系统                   设备进程
    │                           │                             │
    ├─ scrcpy-server ──────> /data/local/tmp/scrcpy-server.jar
    │                           │                             │
    │                           └─> app_process 加载执行 ────>│
    │                                                         │
    └─ localhost:27183 <──── adb tunnel <──── localabstract socket <── Java Server
```

### 耗时分析

| 操作 | 平均耗时 | 说明 |
|------|---------|------|
| 启动 ADB | ~100ms | 首次启动较慢 |
| 设备选择 | ~200ms | 执行 adb devices |
| 推送 JAR | ~50ms | 64KB 文件 |
| 建立隧道 | ~100ms | reverse/forward |
| 启动服务端 | ~500ms | Java 进程启动 |
| 建立连接 | ~200ms | 三路握手 |
| **总计** | **~1-2秒** | 正常情况 |

---

**文档版本**: 1.0  
**更新日期**: 2026-01-23  
**基于代码**: scrcpy 2.8

