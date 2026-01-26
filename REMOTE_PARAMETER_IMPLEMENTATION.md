# --remote 参数实现总结

## 实现概述

成功为 scrcpy 客户端添加了 `--remote=IP:PORT` 参数，允许直接连接到已启动的服务端，完全绕过 ADB 相关操作。

**实现日期**：2026-01-23  
**修改文件数**：6 个核心文件  
**新增代码行数**：约 200 行（含注释和标记）

---

## 环境变量支持

### SCRCPY_REMOTE

**实现日期**：2026-01-26

从该版本开始，scrcpy 支持通过 `SCRCPY_REMOTE` 环境变量设置远程连接地址，提供更灵活的配置方式。

**格式**：`IP:PORT`（例如：`192.168.1.100:50001`）

**优先级**：
1. 命令行参数 `--remote` 的优先级最高
2. 如果未指定 `--remote`，则检查 `SCRCPY_REMOTE` 环境变量
3. 如果环境变量也未设置，则使用默认的 ADB 模式

**使用场景**：
- 避免在脚本中反复输入 `--remote` 参数
- 在自动化测试环境中统一配置
- 团队协作中共享配置

**限制**：
- 与 `--remote` 参数相同，不能与 ADB 设备选择选项同时使用
- 不能与 `--list-*` 选项同时使用

---

## 文件修改清单

### 1. `app/src/cli.c` - 命令行参数解析

**修改内容**：
- 在 `enum` 中添加 `OPT_REMOTE` 选项 ID（第 117 行）
- 在 `options` 数组中添加 `--remote` 参数定义（第 840 行附近）
- 在 `scrcpy_parse_args()` 函数中添加解析逻辑（第 2600 行附近）
- 添加参数冲突检查（第 3118 行附近）
- **[2026-01-26 新增]** 在 `envvars` 数组中添加 `SCRCPY_REMOTE` 环境变量说明（第 1238 行附近）
- **[2026-01-26 新增]** 在 `parse_args_with_getopt()` 函数结尾添加环境变量解析逻辑（第 3386 行附近）

**关键代码**：

#### 命令行参数解析
```c
// 解析 IP:PORT 格式
case OPT_REMOTE: {
    char *colon = strchr(optarg, ':');
    if (!colon) {
        LOGE("Invalid remote address format, expected IP:PORT");
        return false;
    }
    
    *colon = '\0';
    const char *ip_str = optarg;
    const char *port_str = colon + 1;
    
    uint32_t remote_host;
    if (!net_parse_ipv4(ip_str, &remote_host)) {
        LOGE("Invalid IP address: %s", ip_str);
        return false;
    }
    
    long port_long;
    if (!sc_str_parse_integer(port_str, &port_long) || 
        port_long <= 0 || port_long > 0xFFFF) {
        LOGE("Invalid port: %s", port_str);
        return false;
    }
    
    opts->remote_host = remote_host;
    opts->remote_port = (uint16_t)port_long;
    break;
}

// 参数冲突检查
if (opts->remote_host && (opts->serial || opts->tcpip || 
                           opts->select_usb || opts->select_tcpip)) {
    LOGE("--remote cannot be used with device selection options");
    return false;
}

if (opts->remote_host && opts->list) {
    LOGE("--remote cannot be used with --list-* options");
    return false;
}
```

#### 环境变量解析（2026-01-26 新增）
```c
// 在 parse_args_with_getopt() 函数结尾
if (opts->remote_host == 0) {
    const char *env_remote = getenv("SCRCPY_REMOTE");
    if (env_remote) {
        LOGI("Using SCRCPY_REMOTE: %s", env_remote);
        
        // 解析格式：IP:PORT（与命令行参数相同的逻辑）
        char *env_remote_copy = strdup(env_remote);
        if (!env_remote_copy) {
            LOG_OOM();
            return false;
        }
        
        char *colon = strchr(env_remote_copy, ':');
        if (!colon) {
            LOGE("Invalid SCRCPY_REMOTE format, expected IP:PORT");
            free(env_remote_copy);
            return false;
        }
        
        *colon = '\0';
        const char *ip_str = env_remote_copy;
        const char *port_str = colon + 1;
        
        uint32_t remote_host;
        if (!net_parse_ipv4(ip_str, &remote_host)) {
            LOGE("Invalid IP address in SCRCPY_REMOTE: %s", ip_str);
            free(env_remote_copy);
            return false;
        }
        
        long port_long;
        if (!sc_str_parse_integer(port_str, &port_long) || 
            port_long <= 0 || port_long > 0xFFFF) {
            LOGE("Invalid port in SCRCPY_REMOTE: %s", port_str);
            free(env_remote_copy);
            return false;
        }
        
        opts->remote_host = remote_host;
        opts->remote_port = (uint16_t)port_long;
        
        free(env_remote_copy);
        
        // 检查与其他参数的冲突
        if (opts->serial || opts->tcpip || 
            opts->select_usb || opts->select_tcpip) {
            LOGE("SCRCPY_REMOTE cannot be used with device selection options");
            return false;
        }
        
        if (opts->list) {
            LOGE("SCRCPY_REMOTE cannot be used with --list-* options");
            return false;
        }
    }
}
```

---

### 2. `app/src/options.h` - 选项结构体定义

**修改内容**：
- 在 `struct scrcpy_options` 中添加 `remote_host` 和 `remote_port` 字段（第 330 行附近）

**关键代码**：
```c
struct scrcpy_options {
    // ... existing fields ...
    
    // Remote connection mode (直接连接模式)
    uint32_t remote_host;  // IPv4 地址（网络字节序）
    uint16_t remote_port;  // 起始端口号（主机字节序）
};
```

---

### 3. `app/src/options.c` - 选项默认值

**修改内容**：
- 在 `scrcpy_options_default` 中添加默认值（第 115 行附近）

**关键代码**：
```c
const struct scrcpy_options scrcpy_options_default = {
    // ... existing fields ...
    .remote_host = 0,  // 0 表示未设置
    .remote_port = 0,
};
```

---

### 4. `app/src/server.h` - 服务器参数结构体

**修改内容**：
- 在 `struct sc_server_params` 中添加 `remote_host` 和 `remote_port` 字段（第 73 行附近）

**关键代码**：
```c
struct sc_server_params {
    // ... existing fields ...
    
    // Remote connection mode
    uint32_t remote_host;  // IPv4 地址
    uint16_t remote_port;  // 起始端口号
};
```

---

### 5. `app/src/server.c` - 服务器连接逻辑（核心实现）

**修改内容**：

#### 5.1 新增 `sc_server_connect_to_remote()` 函数（第 742 行）

专门用于 remote 模式的连接函数，负责：
- 按顺序连接 3 次到**同一个端口**（video、audio、control）
- 服务端通过连接顺序区分不同的流
- 设置 TCP_NODELAY 选项
- 发送握手字节
- 读取设备信息
- 错误处理和资源清理

**关键代码**：
```c
static bool
sc_server_connect_to_remote(struct sc_server *server, struct sc_server_info *info) {
    const struct sc_server_params *params = &server->params;
    
    uint32_t remote_host = params->remote_host;
    uint16_t remote_port = params->remote_port;
    
    // 连接重试参数
    unsigned attempts = 50;
    sc_tick delay = SC_TICK_FROM_MS(200);
    
    // 第一个连接（可能是 video、audio 或 control，取决于哪个被启用）
    sc_socket first_socket = connect_to_server(server, attempts, delay,
                                               remote_host, remote_port);
    if (first_socket == SC_SOCKET_NONE) {
        return false;
    }
    
    // 根据流的启用情况分配第一个 socket
    if (video) {
        video_socket = first_socket;
    } else if (audio) {
        audio_socket = first_socket;
    } else if (control) {
        control_socket = first_socket;
    }
    
    // 如果需要第二个连接（例如 video+audio 或 audio+control）
    if (audio && video) {
        audio_socket = net_socket();
        net_connect_intr(&server->intr, audio_socket,
                        remote_host, remote_port);  // 同一个端口！
    }
    
    // 如果需要第三个连接（video+audio+control）
    if (control && (video || audio)) {
        control_socket = net_socket();
        net_connect_intr(&server->intr, control_socket,
                        remote_host, remote_port);  // 同一个端口！
    }
    
    // ... 错误处理和资源清理 ...
}
```

#### 5.2 修改 `run_server()` 函数（第 1060 行）

在函数开始处添加 remote 模式检测和处理逻辑：

**关键代码**：
```c
static int
run_server(void *data) {
    struct sc_server *server = data;
    const struct sc_server_params *params = &server->params;

    // Remote 模式：直接连接，跳过所有 ADB 操作
    if (params->remote_host != 0) {
        LOGI("Remote connection mode: connecting to %u.%u.%u.%u:%u", ...);
        
        // 设置为 forward 模式（客户端主动连接）
        server->tunnel.forward = true;
        server->tunnel.enabled = true;
        server->tunnel.local_port = params->remote_port;
        
        // 设置设备名称为 "Remote Device"
        strcpy(server->info.device_name, "Remote Device");
        
        // 直接建立连接
        bool ok = sc_server_connect_to_remote(server, &server->info);
        if (!ok) {
            goto error_connection_failed;
        }
        
        // 通知连接成功
        server->cbs->on_connected(server, server->cbs_userdata);
        
        // 等待停止信号
        sc_mutex_lock(&server->mutex);
        while (!server->stopped) {
            sc_cond_wait(&server->cond_stopped, &server->mutex);
        }
        sc_mutex_unlock(&server->mutex);
        
        return 0;
    }

    // 以下是原有的 ADB 模式代码...
}
```

#### 5.3 修改 `sc_server_init()` 函数（第 518 行）

在 remote 模式下跳过 ADB 初始化：

**关键代码**：
```c
bool
sc_server_init(struct sc_server *server, const struct sc_server_params *params,
              const struct sc_server_callbacks *cbs, void *cbs_userdata) {
    server->params = *params;

    // Remote 模式不需要 ADB
    if (params->remote_host == 0) {
        bool ok = sc_adb_init();
        if (!ok) {
            return false;
        }
    }
    
    // ... 其余初始化代码 ...
}
```

#### 5.4 修改 `sc_server_destroy()` 函数（第 1364 行）

在 remote 模式下跳过 ADB 销毁：

**关键代码**：
```c
void
sc_server_destroy(struct sc_server *server) {
    // ... socket 关闭代码 ...
    
    // Remote 模式不需要销毁 ADB
    if (server->params.remote_host == 0) {
        sc_adb_destroy();
    }
}
```

---

### 6. `app/src/scrcpy.c` - 参数传递

**修改内容**：
- 在 `scrcpy()` 函数中将 remote 参数传递到服务器模块（第 472 行附近）

**关键代码**：
```c
struct sc_server_params params = {
    .scid = scid,
    .req_serial = options->serial,
    // ... existing parameters ...
    .remote_host = options->remote_host,
    .remote_port = options->remote_port,
    // ... remaining parameters ...
};
```

---

## 技术实现细节

### 1. 参数优先级

remote 连接地址的获取遵循以下优先级（从高到低）：

1. **命令行参数 `--remote`**：如果指定，直接使用
2. **环境变量 `SCRCPY_REMOTE`**：如果命令行未指定，则检查环境变量
3. **默认 ADB 模式**：如果都未设置，使用传统的 ADB 连接方式

这种设计允许用户：
- 在环境变量中设置默认地址，但可以通过命令行参数临时覆盖
- 在自动化脚本中灵活配置

### 2. IP 地址解析

使用 `net_parse_ipv4()` 函数将字符串格式的 IP 地址转换为 32 位整数（网络字节序）。

### 2. 连接顺序策略

客户端会**连接 3 次到同一个端口**，服务端通过**连接顺序**区分不同的流：

- **第 1 个连接**：视频流
- **第 2 个连接**：音频流
- **第 3 个连接**：控制流

如果某个流被禁用（如 `--no-audio`），对应的连接会被跳过，后续流会提前建立连接。

**重要说明**：
- ✅ 客户端多次连接到**同一个端口**（例如都是 50001）
- ✅ 服务端在**同一个 socket 上按顺序 accept** 3 个连接
- ❌ **不是**连接到 3 个不同的连续端口（50001, 50002, 50003）

### 3. 连接重试机制

- **最大尝试次数**：50 次
- **重试间隔**：200ms
- **总超时时间**：约 10 秒

### 4. 错误处理

- **参数验证**：在命令行解析阶段进行
- **连接失败**：记录详细日志并清理资源
- **资源泄漏防护**：使用 `goto fail` 模式确保所有 socket 被正确关闭

---

## 代码标记规范

所有新增和修改的代码都使用以下标记格式：

```c
// 新增或修改的代码
```

---

## 兼容性保证

1. **向后兼容**：所有现有功能保持不变
2. **参数互斥**：`--remote` 与 ADB 相关参数互斥，避免混淆
3. **功能限制**：remote 模式下不支持需要 ADB 的功能（如 `--list-*`）

---

## 使用示例

### 基本用法

#### 使用命令行参数

```bash
scrcpy --remote=192.168.1.100:50001
```

#### 使用环境变量

```bash
# Windows (PowerShell)
$env:SCRCPY_REMOTE="192.168.1.100:50001"
scrcpy

# Windows (CMD)
set SCRCPY_REMOTE=192.168.1.100:50001
scrcpy

# Linux / macOS
export SCRCPY_REMOTE=192.168.1.100:50001
scrcpy
```

**注意**：命令行参数 `--remote` 的优先级高于环境变量 `SCRCPY_REMOTE`。

### 与其他参数组合

```bash
# 禁用音频
scrcpy --remote=192.168.1.100:50001 --no-audio

# 仅音频模式
scrcpy --remote=192.168.1.100:50001 --no-video --no-control

# 无窗口模式
scrcpy --remote=192.168.1.100:50001 --no-window

# 调整视频质量
scrcpy --remote=192.168.1.100:50001 -b 16M --max-fps=60
```

---

## 测试状态

✅ **已完成**：
- 代码实现
- 参数解析
- 环境变量支持（2026-01-26）
- 错误处理
- 资源管理
- 文档编写
- Bug 修复（握手协议错误）

⏳ **待测试**：
- 实际连接测试
- 环境变量功能测试
- 性能测试
- 稳定性测试
- 边界条件测试

详细测试指南请参考：[REMOTE_PARAMETER_TESTING.md](REMOTE_PARAMETER_TESTING.md)

## 已修复问题

### Bug #1: 连接立即断开（2026-01-24 修复）

**问题**：使用 `--remote` 连接时，虽然能建立连接但立即断开。

**原因**：`sc_server_connect_to_remote()` 函数错误地发送了握手字节，而 forward 模式的协议不需要客户端发送握手数据。

**修复**：删除发送握手字节的代码（原第 836-843 行），直接读取设备信息。

详细信息请参考：[REMOTE_MODE_BUG_FIX.md](REMOTE_MODE_BUG_FIX.md)

### Bug #2: 窗口关闭后进程不退出（2026-01-24 修复）

**问题**：关闭投屏窗口后，客户端进程不退出，持续打印音频缓冲区警告信息。

**原因**：`run_server()` 函数的 remote 模式分支在退出前没有中断 socket 连接，导致 demuxer 持续运行。

**修复**：在收到停止信号后，添加 `net_interrupt()` 调用中断所有 socket（第 1113-1126 行），使 demuxer 能够检测到错误并停止。

详细信息请参考：[REMOTE_EXIT_BUG_FIX.md](REMOTE_EXIT_BUG_FIX.md)

---

## 已知限制

1. **仅支持 IPv4**：暂不支持 IPv6 和域名解析
2. **端口必须连续**：三个流必须使用连续的端口
3. **不支持设备自动发现**：必须手动指定 IP 和端口
4. **不支持服务端自动启动**：服务端必须预先启动
5. **不支持 --list-* 选项**：这些选项需要 ADB 支持

---

## 未来改进方向

1. **支持 IPv6**：扩展到 IPv6 网络
2. **支持域名解析**：允许使用域名而非 IP
3. **自定义端口映射**：允许为每个流指定独立端口
4. **连接超时配置**：允许用户自定义超时时间
5. **连接状态监控**：提供更详细的连接状态信息

---

## 总结

`--remote` 参数和 `SCRCPY_REMOTE` 环境变量的成功实现为 scrcpy 提供了更灵活的连接方式，特别适用于：
- 远程服务器场景
- 复杂网络环境（如 SSH 隧道）
- 需要跳过 ADB 的场景
- 多客户端连接同一服务端
- 自动化测试和脚本环境

**环境变量支持的优势**：
- 避免在脚本中反复输入长参数
- 在 CI/CD 环境中便于统一配置
- 保持命令行简洁的同时提供灵活性
- 命令行参数可以覆盖环境变量，兼顾灵活性和便利性

所有代码修改都遵循了项目的编码规范，并添加了清晰的标记和注释，便于后续维护和审查。

