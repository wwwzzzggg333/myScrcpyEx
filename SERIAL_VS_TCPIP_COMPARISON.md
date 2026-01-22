# scrcpy -s IP:5555 vs scrcpy --tcpip=IP:5555 对比分析

## 🎯 快速回答

**不一样！** 虽然都能连接到 TCP/IP 设备，但内部执行流程有本质区别：

| 特性 | `-s IP:5555` | `--tcpip=IP:5555` |
|------|--------------|-------------------|
| **执行 adb connect** | ❌ 不执行 | ✅ 自动执行 |
| **前提条件** | 设备必须已连接 | 设备可以未连接 |
| **使用场景** | 选择已连接设备 | 主动连接设备 |
| **设备列表检查** | ✅ 需要 | ❌ 不需要 |
| **失败情况** | 设备未连接时报错 | 连接失败时报错 |

---

## 📊 详细对比

### 命令 1: `scrcpy -s 192.168.1.100:5555`

**含义:** 从已连接的设备中**选择**序列号为 `192.168.1.100:5555` 的设备

#### 执行流程

```
1. adb start-server
2. adb devices -l  ← 获取已连接设备列表
3. 从列表中查找 192.168.1.100:5555
   ├─ 找到 → 使用该设备
   └─ 未找到 → ERROR: Device not found
4. adb push scrcpy-server.jar
5. 建立 adb tunnel
6. 启动服务端
```

#### 代码路径

```c
// server.c:run_server()

bool need_initial_serial = !params->tcpip_dst;  // true

if (need_initial_serial) {
    // 使用 -s 参数
    if (params->req_serial) {
        selector.type = SC_ADB_DEVICE_SELECT_SERIAL;
        selector.serial = params->req_serial;  // "192.168.1.100:5555"
    }
    
    // ⚠️ 关键：从已连接设备中选择
    ok = sc_adb_select_device(&server->intr, &selector, 0, &device);
    
    if (!ok) {
        // ERROR: 设备未在列表中
        goto error_connection_failed;
    }
    
    // 直接使用该设备
    server->serial = device.serial;
}
```

#### 实际执行的命令

```bash
# 1. 启动 adb 服务
adb start-server

# 2. 列出设备 (内部解析输出)
adb devices -l

# 3. 检查 192.168.1.100:5555 是否在列表中
# 如果不在，报错退出

# 4. 继续后续操作
adb -s 192.168.1.100:5555 push scrcpy-server.jar /data/local/tmp/
adb -s 192.168.1.100:5555 reverse ...
adb -s 192.168.1.100:5555 shell ...
```

#### 前提条件

**设备必须已经通过 adb connect 连接！**

```bash
# 必须先手动连接
$ adb connect 192.168.1.100:5555
connected to 192.168.1.100:5555

# 验证连接
$ adb devices
List of devices attached
192.168.1.100:5555      device

# 然后才能使用 -s
$ scrcpy -s 192.168.1.100:5555
```

#### 使用场景

- ✅ 设备已经通过 `adb connect` 连接
- ✅ 多个 TCP/IP 设备，需要指定其中一个
- ✅ 脚本中明确指定设备
- ✅ 避免重复执行 `adb connect`

---

### 命令 2: `scrcpy --tcpip=192.168.1.100:5555`

**含义:** 主动连接到指定 IP 地址，如果未连接则**先执行 adb connect**

#### 执行流程

```
1. adb start-server
2. adb connect 192.168.1.100:5555  ← 主动连接
   ├─ 已连接 → 无影响
   └─ 未连接 → 建立连接
3. 不检查设备列表，直接使用该地址
4. adb push scrcpy-server.jar
5. 建立 adb tunnel
6. 启动服务端
```

#### 代码路径

```c
// server.c:run_server()

bool need_initial_serial = !params->tcpip_dst;  // false!

if (need_initial_serial) {
    // 跳过这个分支
} else {
    // ✅ 进入这里
    const char *tcpip_dst = params->tcpip_dst;  // "192.168.1.100:5555"
    
    // ⚠️ 关键：直接连接，不检查设备列表
    ok = sc_server_configure_tcpip_known_address(server, tcpip_dst, false);
    
    // 内部执行 adb connect
}
```

**`sc_server_configure_tcpip_known_address()` 函数:**

```c
static bool
sc_server_configure_tcpip_known_address(struct sc_server *server,
                                        const char *addr, bool disconnect) {
    // 补全端口（如果没有）
    char *ip_port = contains_port ? strdup(addr)
                                  : append_port(addr, 5555);
    
    server->serial = ip_port;
    
    // ⚠️ 关键：执行 adb connect
    return sc_server_connect_to_tcpip(server, ip_port, disconnect);
}

static bool
sc_server_connect_to_tcpip(struct sc_server *server, const char *ip_port,
                           bool disconnect) {
    LOGI("Connecting to %s...", ip_port);
    
    // ⚠️ 执行 adb connect
    bool ok = sc_adb_connect(intr, ip_port, 0);
    
    if (!ok) {
        LOGE("Could not connect to %s", ip_port);
        return false;
    }
    
    LOGI("Connected to %s", ip_port);
    return true;
}
```

#### 实际执行的命令

```bash
# 1. 启动 adb 服务
adb start-server

# 2. ⚠️ 主动连接设备
adb connect 192.168.1.100:5555
# 输出: connected to 192.168.1.100:5555
# 或者: already connected to 192.168.1.100:5555

# 3. 不执行 adb devices，直接继续
adb -s 192.168.1.100:5555 push scrcpy-server.jar /data/local/tmp/
adb -s 192.168.1.100:5555 reverse ...
adb -s 192.168.1.100:5555 shell ...
```

#### 前提条件

**设备无需预先连接！** scrcpy 会自动执行 `adb connect`

```bash
# 设备可以未连接
$ adb devices
List of devices attached
# (空列表)

# 直接使用 --tcpip 即可
$ scrcpy --tcpip=192.168.1.100:5555
# scrcpy 内部自动执行:
# adb connect 192.168.1.100:5555
```

#### 使用场景

- ✅ 设备未连接，需要自动连接
- ✅ 一次性命令，无需手动 `adb connect`
- ✅ 临时连接设备
- ✅ 脚本自动化（不依赖预先连接）

---

## 🔍 核心区别详解

### 区别 1: 是否执行 adb connect

```bash
# -s 参数：假设已连接
scrcpy -s 192.168.1.100:5555
# 内部不执行 adb connect
# 如果设备未连接，直接失败

# --tcpip 参数：主动连接
scrcpy --tcpip=192.168.1.100:5555
# 内部执行: adb connect 192.168.1.100:5555
# 建立连接后继续
```

### 区别 2: 设备列表检查

```c
// -s 参数的处理
if (params->req_serial) {
    // 1. 执行 adb devices -l
    // 2. 解析输出获取设备列表
    // 3. 查找匹配的序列号
    // 4. 如果找不到，报错
    ok = sc_adb_select_device(&server->intr, &selector, 0, &device);
}

// --tcpip 参数的处理
if (params->tcpip_dst) {
    // 直接使用指定的地址
    // 不检查设备列表
    server->serial = ip_port;
    sc_adb_connect(intr, ip_port, 0);
}
```

### 区别 3: 错误处理

**使用 `-s` 时的错误:**

```bash
$ scrcpy -s 192.168.1.100:5555
ERROR: Device '192.168.1.100:5555' not found

# 需要先手动连接
$ adb connect 192.168.1.100:5555
$ scrcpy -s 192.168.1.100:5555  # 成功
```

**使用 `--tcpip` 时的错误:**

```bash
$ scrcpy --tcpip=192.168.1.100:5555
INFO: Connecting to 192.168.1.100:5555...
ERROR: Could not connect to 192.168.1.100:5555

# 可能的原因:
# - IP 地址错误
# - 设备未在同一网络
# - 设备未开启无线调试
# - 端口不是 5555
```

---

## 💡 实际示例对比

### 场景 1: 设备已连接

```bash
# 预先连接
$ adb connect 192.168.1.100:5555
connected to 192.168.1.100:5555

# 方式 1: 使用 -s (快速)
$ scrcpy -s 192.168.1.100:5555
# ✅ 直接工作，不执行 connect

# 方式 2: 使用 --tcpip (稍慢)
$ scrcpy --tcpip=192.168.1.100:5555
# ✅ 也能工作，但会重新执行 connect
# 输出: "already connected to 192.168.1.100:5555"
```

**结论:** 设备已连接时，两者都能用，但 `-s` 稍快（省略 connect 步骤）

---

### 场景 2: 设备未连接

```bash
# 确认未连接
$ adb devices
List of devices attached
# (空)

# 方式 1: 使用 -s (失败)
$ scrcpy -s 192.168.1.100:5555
ERROR: Device '192.168.1.100:5555' not found
# ❌ 失败

# 方式 2: 使用 --tcpip (成功)
$ scrcpy --tcpip=192.168.1.100:5555
INFO: Connecting to 192.168.1.100:5555...
INFO: Connected to 192.168.1.100:5555
# ✅ 成功
```

**结论:** 设备未连接时，只有 `--tcpip` 能用

---

### 场景 3: 多个 TCP/IP 设备

```bash
# 预先连接多个设备
$ adb connect 192.168.1.100:5555
$ adb connect 192.168.1.101:5555

$ adb devices
List of devices attached
192.168.1.100:5555      device
192.168.1.101:5555      device

# 方式 1: 使用 -s (精确选择)
$ scrcpy -s 192.168.1.100:5555
# ✅ 选择第一个设备

$ scrcpy -s 192.168.1.101:5555
# ✅ 选择第二个设备

# 方式 2: 使用 --tcpip (也能工作)
$ scrcpy --tcpip=192.168.1.100:5555
# ✅ 连接第一个设备 (already connected)

$ scrcpy --tcpip=192.168.1.101:5555
# ✅ 连接第二个设备 (already connected)
```

**结论:** 两者都能精确选择设备

---

### 场景 4: 脚本自动化

**使用 `-s` 的脚本 (需要预先连接):**

```bash
#!/bin/bash
# bad_script.sh - 假设设备已连接

DEVICE_IP="192.168.1.100:5555"

# ❌ 问题：如果设备未连接，脚本失败
scrcpy -s "$DEVICE_IP" --max-size=720 --record=test.mp4
```

**使用 `--tcpip` 的脚本 (更健壮):**

```bash
#!/bin/bash
# good_script.sh - 自动连接设备

DEVICE_IP="192.168.1.100:5555"

# ✅ 更好：自动连接，无需预先准备
scrcpy --tcpip="$DEVICE_IP" --max-size=720 --record=test.mp4
```

**最佳实践 (显式检查):**

```bash
#!/bin/bash
# best_script.sh - 显式处理连接

DEVICE_IP="192.168.1.100:5555"

# 1. 确保连接
echo "连接到设备..."
adb connect "$DEVICE_IP"

# 2. 等待设备就绪
adb -s "$DEVICE_IP" wait-for-device

# 3. 使用 -s (已经连接)
scrcpy -s "$DEVICE_IP" --max-size=720 --record=test.mp4
```

---

## 🎯 选择建议

### 使用 `-s IP:5555` 的情况

✅ **适合:**
- 设备已经通过 `adb connect` 连接
- 多设备环境，需要明确选择
- 性能敏感场景（省略 connect 步骤）
- 你确信设备已连接

❌ **不适合:**
- 设备未预先连接
- 一次性使用，嫌麻烦
- 自动化脚本（需要额外处理连接）

### 使用 `--tcpip=IP:5555` 的情况

✅ **适合:**
- 设备可能未连接，需要自动连接
- 一次性命令，图方便
- 自动化脚本（自动处理连接）
- 不确定设备是否已连接

❌ **不适合:**
- 频繁切换设备（每次都执行 connect）
- 网络不稳定（connect 可能超时）

---

## 📝 等价命令组合

```bash
# 这两组命令是等价的:

# 组合 1 (显式)
adb connect 192.168.1.100:5555
scrcpy -s 192.168.1.100:5555

# 组合 2 (隐式)
scrcpy --tcpip=192.168.1.100:5555
```

---

## 🔧 高级用法

### `--tcpip` 的特殊前缀

```bash
# 强制重连 (先断开再连接)
scrcpy --tcpip=+192.168.1.100:5555

# 代码实现:
if (tcpip_dst[0] == '+') {
    sc_adb_disconnect(intr, addr, ...);  // 先断开
    sc_adb_connect(intr, addr, ...);     // 再连接
}
```

**使用场景:**
- 连接出现问题，需要重新建立
- 切换网络后重连
- 清除旧连接状态

### 自动查找 IP 地址

```bash
# 不带参数的 --tcpip (从 USB 切换到无线)
scrcpy --tcpip

# 内部流程:
# 1. 从 USB 设备获取 IP
# 2. 执行 adb tcpip 5555
# 3. 执行 adb connect IP:5555
# 4. 开始镜像
```

---

## 📊 性能对比

| 操作 | `-s IP:5555` | `--tcpip=IP:5555` |
|------|--------------|-------------------|
| **adb start-server** | ~100ms | ~100ms |
| **adb devices** | ~200ms | **0ms (跳过)** |
| **adb connect** | **0ms (跳过)** | ~300ms |
| **总耗时** | ~300ms | ~400ms |

**结论:** `-s` 在设备已连接时稍快 (~100ms)

---

## ⚠️ 常见错误

### 错误 1: 混用参数

```bash
# ❌ 错误：不能同时使用
scrcpy -s 192.168.1.100:5555 --tcpip=192.168.1.100:5555

# 代码中的检查:
assert(!params->req_serial || !params->tcpip_dst);
```

### 错误 2: `-s` 找不到设备

```bash
$ scrcpy -s 192.168.1.100:5555
ERROR: Device '192.168.1.100:5555' not found

# 解决方案 1: 先连接
$ adb connect 192.168.1.100:5555
$ scrcpy -s 192.168.1.100:5555

# 解决方案 2: 改用 --tcpip
$ scrcpy --tcpip=192.168.1.100:5555
```

### 错误 3: `--tcpip` 连接失败

```bash
$ scrcpy --tcpip=192.168.1.100:5555
ERROR: Could not connect to 192.168.1.100:5555

# 可能原因和解决方案:
# 1. 检查 IP 是否正确
$ ping 192.168.1.100

# 2. 检查设备是否开启无线调试
# 设备: 设置 -> 开发者选项 -> 无线调试

# 3. 检查端口是否正确
$ adb connect 192.168.1.100:5555
# 或尝试其他端口
$ adb connect 192.168.1.100:37845
```

---

## 📚 总结表

| 特性 | `-s IP:5555` | `--tcpip=IP:5555` |
|------|--------------|-------------------|
| **执行 adb connect** | ❌ | ✅ |
| **检查设备列表** | ✅ | ❌ |
| **需要预先连接** | ✅ 是 | ❌ 否 |
| **自动化友好** | ⚠️ 需额外处理 | ✅ 是 |
| **性能** | 🚀 稍快 | ⚡ 稍慢 |
| **容错性** | ⚠️ 低 | ✅ 高 |
| **适用场景** | 明确选择已连接设备 | 自动连接指定设备 |
| **推荐度** | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

---

**文档版本**: 1.0  
**更新日期**: 2026-01-23  
**基于代码**: scrcpy 2.8

