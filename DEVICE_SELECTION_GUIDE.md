# Scrcpy 设备选择和连接完全指南

## 🎯 快速查找表

| 你的情况 | 使用命令 | 说明 |
|---------|---------|------|
| 只连接了一个设备 | `scrcpy` | 自动选择 |
| 知道设备序列号 | `scrcpy -s <序列号>` | 精确指定 |
| 有USB + 模拟器,想用USB | `scrcpy -d` | 选择USB设备 |
| 有USB + 模拟器,想用模拟器 | `scrcpy -e` | 选择TCP/IP设备 |
| 想无线连接(首次) | `scrcpy --tcpip` | 自动配置无线 |
| 无线连接(已知IP) | `scrcpy --tcpip=192.168.1.100:5555` | 直接连接 |
| 经常用同一设备 | `export ANDROID_SERIAL=xxx` | 设置默认 |
| 多个相同类型设备 | `scrcpy -s <序列号>` | 必须精确指定 |

---

## 📋 完整决策树

```
需要连接 Android 设备
    │
    ▼
是否已知设备IP地址? ────────────────────┐
    │                                  │
    ├─ 是 → 设备支持无线调试?           │
    │       │                          │
    │       ├─ 是 → scrcpy --tcpip=IP:5555
    │       │
    │       └─ 否 → 需先通过USB启用
    │              └─ adb tcpip 5555
    │              └─ adb connect IP:5555
    │              └─ scrcpy -s IP:5555
    │
    ▼
设备是否已通过USB连接? ──────────────────┐
    │                                    │
    ├─ 是 → 继续 ↓                        │
    │                                    │
    └─ 否 → ERROR: 无法连接设备           │
                                         │
    ┌────────────────────────────────────┘
    │
    ▼
执行 adb devices 查看连接的设备
    │
    ▼
有几个设备? ─────────────────────────────┐
    │                                    │
    ├─ 0 个 → ERROR: 没有设备            │
    │                                    │
    ├─ 1 个 → scrcpy (自动选择) ✓         │
    │                                    │
    └─ 多个 → 继续 ↓                      │
              │                          │
              ▼                          │
    有设置 ANDROID_SERIAL 环境变量? ──────┤
              │                          │
              ├─ 是 → scrcpy (使用环境变量) ✓
              │                          │
              └─ 否 → 需要明确指定 ↓       │
                      │                  │
                      ▼                  │
    设备类型情况? ──────────────────────────┤
                      │                  │
                      ├─ 1个USB + N个其他
                      │   └→ scrcpy -d ✓
                      │
                      ├─ N个USB + 1个其他
                      │   └→ scrcpy -e ✓
                      │
                      └─ 其他复杂情况
                          └→ scrcpy -s <序列号> ✓
```

---

## 🔍 设备类型识别速查

### 查看所有设备
```bash
$ adb devices -l
List of devices attached
R3CN90ABCD              device product:... model:... device:...   # USB设备
192.168.1.100:5555      device product:... model:... device:...   # TCP/IP设备
emulator-5554           device product:... model:... device:...   # 模拟器
```

### 设备类型判断规则

```
┌─────────────────────────────────────────────────────┐
│ 序列号格式                → 设备类型                  │
├─────────────────────────────────────────────────────┤
│ R3CN90ABCD              → USB 设备                  │
│ 0123456789ABCDEF        → USB 设备                  │
│                                                     │
│ 192.168.1.100:5555      → TCP/IP 设备               │
│ 10.0.0.50:37845         → TCP/IP 设备               │
│                                                     │
│ emulator-5554           → Android 模拟器            │
│ emulator-5556           → Android 模拟器            │
└─────────────────────────────────────────────────────┘

判断逻辑:
1. 包含 "emulator-" 前缀 → 模拟器
2. 包含 ":"           → TCP/IP 设备
3. 其他               → USB 设备
```

---

## 🚀 常见场景完整示例

### 场景 1: 开发单个应用 (一个设备)

```bash
# 最简单,不需要任何参数
scrcpy
```

---

### 场景 2: 测试多个设备 (自动化脚本)

```bash
#!/bin/bash
# test_multiple_devices.sh

# 设备列表
DEVICES=(
    "R3CN90ABCD"         # 三星 Galaxy
    "0A1B2C3D4E5F"       # 小米 11
    "emulator-5554"      # 模拟器
)

# 对每个设备执行测试
for device in "${DEVICES[@]}"; do
    echo "测试设备: $device"
    
    # 使用 -s 明确指定设备
    scrcpy -s "$device" \
        --max-size=1080 \
        --record="test_${device}.mp4" \
        --time-limit=60 \
        --no-audio
    
    echo "设备 $device 测试完成"
done
```

---

### 场景 3: 办公室环境 (USB + 无线混合)

```bash
# 查看当前连接的设备
$ adb devices
List of devices attached
R3CN90ABCD             device    # 我的USB测试机
192.168.1.100:5555     device    # 同事的无线设备
192.168.1.101:5555     device    # 另一个无线设备
emulator-5554          device    # 本地模拟器

# 选择策略
scrcpy -d                    # 选择唯一的USB设备 (R3CN90ABCD)
scrcpy -s 192.168.1.100      # 选择特定无线设备(可省略端口)
scrcpy -s emulator-5554      # 选择模拟器

# 或者设置默认设备
export ANDROID_SERIAL=R3CN90ABCD
scrcpy                       # 自动使用我的USB测试机
```

---

### 场景 4: 从USB切换到无线 (完整流程)

```bash
# ==========================================
# 方法 1: 自动模式 (推荐)
# ==========================================
# 确保设备通过USB连接
adb devices

# 自动切换到无线
scrcpy --tcpip
# scrcpy会自动:
# 1. 执行 adb tcpip 5555
# 2. 获取设备IP地址
# 3. 执行 adb connect <IP>:5555
# 4. 开始无线镜像

# 现在可以拔掉USB线了!
# 后续连接使用:
scrcpy -s 192.168.1.100:5555


# ==========================================
# 方法 2: 手动模式 (更多控制)
# ==========================================
# 1. 启用设备的TCP/IP模式
adb tcpip 5555

# 2. 查找设备IP地址
# 方法A: 从adb获取
adb shell ip -f inet addr show wlan0 | grep inet
# 方法B: 设备设置 → 关于手机 → 状态信息 → IP地址
# 假设IP是: 192.168.1.100

# 3. 连接到设备
adb connect 192.168.1.100:5555

# 4. 验证连接
adb devices
# 应该看到: 192.168.1.100:5555    device

# 5. 现在可以拔掉USB线
# 6. 使用无线连接
scrcpy -s 192.168.1.100:5555

# 或者更简洁:
scrcpy --tcpip=192.168.1.100:5555


# ==========================================
# 方法 3: 使用 Android 11+ 无线调试
# ==========================================
# 前提: Android 11+ 设备

# 1. 设备上启用 "开发者选项" → "无线调试"
# 2. 点击 "使用配对码配对设备"
# 3. 记下显示的 IP:端口 和配对码
#    例如: 192.168.1.100:37845, 配对码: 123456

# 4. PC上配对 (首次)
adb pair 192.168.1.100:37845
# 输入配对码: 123456

# 5. 连接 (使用不同的端口,在无线调试页面显示)
adb connect 192.168.1.100:5555

# 6. 使用 scrcpy
scrcpy -s 192.168.1.100:5555
```

---

### 场景 5: 团队协作 (避免冲突)

```bash
# 团队成员A: 使用默认端口
scrcpy -s device1 --port=27183:27199

# 团队成员B: 使用不同端口范围
scrcpy -s device2 --port=27200:27216

# 团队成员C: 使用更高端口
scrcpy -s device3 --port=27217:27233

# 或使用环境变量
# ~/.bashrc 或 ~/.zshrc
export ANDROID_SERIAL=my_device_serial
export SCRCPY_SERVER_PATH=/path/to/custom/scrcpy-server.jar

# 这样每个人的 scrcpy 配置不会互相干扰
```

---

### 场景 6: CI/CD 自动化测试

```bash
#!/bin/bash
# ci_test.sh - CI/CD 环境中的自动化测试

set -e  # 遇到错误立即退出

# 1. 等待设备连接
echo "等待设备连接..."
adb wait-for-device

# 2. 获取设备序列号
DEVICE_SERIAL=$(adb devices | grep -w device | head -n1 | awk '{print $1}')

if [ -z "$DEVICE_SERIAL" ]; then
    echo "ERROR: 未找到设备"
    exit 1
fi

echo "找到设备: $DEVICE_SERIAL"

# 3. 检查设备类型
if [[ $DEVICE_SERIAL == *":"* ]]; then
    echo "设备类型: TCP/IP"
elif [[ $DEVICE_SERIAL == emulator-* ]]; then
    echo "设备类型: 模拟器"
else
    echo "设备类型: USB"
fi

# 4. 安装测试APK
adb -s "$DEVICE_SERIAL" install -r app-debug.apk

# 5. 启动scrcpy录制
scrcpy -s "$DEVICE_SERIAL" \
    --max-size=720 \
    --video-bit-rate=2M \
    --record="test_recording_$(date +%Y%m%d_%H%M%S).mp4" \
    --time-limit=300 \
    --no-control \
    --no-display &

SCRCPY_PID=$!

# 6. 运行UI测试
adb -s "$DEVICE_SERIAL" shell am instrument -w \
    com.example.app.test/androidx.test.runner.AndroidJUnitRunner

# 7. 停止scrcpy
kill $SCRCPY_PID

echo "测试完成!"
```

---

### 场景 7: 远程设备访问 (SSH隧道)

```bash
# ==========================================
# 场景: 设备连接到远程服务器,本地PC访问
# ==========================================

# 拓扑结构:
# [本地PC] ←─ SSH隧道 ─→ [远程服务器] ←─ USB ─→ [Android设备]

# === 在远程服务器上 (remote-server) ===
ssh user@remote-server

# 1. 确认设备已连接
adb devices

# 2. 保持adb服务运行
# (退出SSH会话前不要关闭)


# === 在本地PC上 ===

# 3. 建立SSH端口转发隧道
# 转发本地 27183-27185 到远程服务器
ssh -L 27183:localhost:27183 \
    -L 27184:localhost:27184 \
    -L 27185:localhost:27185 \
    user@remote-server -N

# 或者使用动态端口转发(更灵活)
ssh -L 27183-27199:localhost:27183-27199 user@remote-server -N

# 4. 另开一个终端,运行 scrcpy
scrcpy --tunnel-host=localhost --tunnel-port=27183

# 或者使用 adb 直接转发
ssh -L 5037:localhost:5037 user@remote-server -N
# 然后本地 adb 命令会自动通过隧道访问远程 adb
adb devices  # 看到远程设备
scrcpy      # 正常使用
```

---

### 场景 8: 多显示器/虚拟显示器

```bash
# 列出设备上的所有显示器
scrcpy --list-displays

# 输出示例:
# --display-id=0    (default)
# --display-id=1    (external)
# --display-id=2    (virtual)

# 选择特定显示器
scrcpy --display-id=1

# 创建虚拟显示器并镜像
scrcpy --new-display=1920x1080/240 --start-app=com.example.game

# 同时镜像多个显示器(需要多个scrcpy实例)
scrcpy -s device1 --display-id=0 --port=27183:27199 &
scrcpy -s device1 --display-id=1 --port=27200:27216 &
```

---

## ⚠️ 常见问题和解决方案

### 问题 1: "Multiple devices" 错误

```bash
$ scrcpy
ERROR: Multiple devices:
       (usb)  R3CN90ABCD         device
      (tcpip) 192.168.1.100:5555 device

# 解决方案1: 使用 -s 指定设备
scrcpy -s R3CN90ABCD

# 解决方案2: 使用 -d 或 -e
scrcpy -d  # 选择USB
scrcpy -e  # 选择TCP/IP

# 解决方案3: 设置环境变量
export ANDROID_SERIAL=R3CN90ABCD
scrcpy
```

---

### 问题 2: "Device unauthorized" 错误

```bash
$ scrcpy
ERROR: Device is unauthorized:
       R3CN90ABCD    unauthorized

# 原因: 设备未授权USB调试

# 解决方案:
# 1. 查看设备屏幕,应该有授权弹窗
# 2. 勾选 "总是允许这台计算机调试"
# 3. 点击 "允许"
# 4. 重新运行 scrcpy
```

---

### 问题 3: "adb: device offline" 错误

```bash
$ scrcpy
ERROR: Device is offline:
       R3CN90ABCD    offline

# 解决方案:
# 1. 重启 adb 服务
adb kill-server
adb start-server

# 2. 重新插拔USB线

# 3. 重启设备

# 4. 如果是无线连接,重新连接
adb disconnect
adb connect 192.168.1.100:5555
```

---

### 问题 4: 无线连接后无法找到设备

```bash
# 症状: 已执行 adb connect,但 scrcpy 找不到设备

# 检查步骤:
# 1. 确认设备已连接
adb devices
# 应该看到: 192.168.1.100:5555    device

# 2. 如果显示 offline,重新连接
adb disconnect 192.168.1.100:5555
adb connect 192.168.1.100:5555

# 3. 检查防火墙是否阻止端口 5555
# Windows: 
netsh advfirewall firewall add rule name="ADB" dir=in action=allow protocol=TCP localport=5555

# Linux:
sudo ufw allow 5555/tcp

# 4. 检查设备和PC是否在同一网络
ping 192.168.1.100

# 5. 确保设备的无线调试仍然启用
# 某些设备会在息屏后关闭无线调试
```

---

### 问题 5: 端口被占用

```bash
$ scrcpy
ERROR: Could not bind to port 27183

# 解决方案1: 使用不同端口范围
scrcpy --port=30000:30016

# 解决方案2: 查找并关闭占用端口的进程
# Windows:
netstat -ano | findstr :27183
taskkill /PID <进程ID> /F

# Linux/macOS:
lsof -i :27183
kill <进程ID>

# 解决方案3: 关闭其他 scrcpy 实例
pkill scrcpy
```

---

### 问题 6: IP地址不匹配

```bash
# 症状: scrcpy --tcpip 获取的IP不对

# 原因: 设备可能有多个网络接口 (WiFi, 热点, VPN等)

# 解决方案: 手动指定正确的IP
# 1. 在设备上查看IP地址
#    设置 → 关于手机 → 状态信息 → IP地址

# 2. 使用该IP直接连接
scrcpy --tcpip=<正确的IP>:5555

# 或者通过adb获取
adb shell ip addr show wlan0 | grep "inet " | awk '{print $2}' | cut -d/ -f1
```

---

## 📚 参考资料

- [官方文档 - 连接](https://github.com/Genymobile/scrcpy/blob/master/doc/connection.md)
- [官方文档 - 隧道](https://github.com/Genymobile/scrcpy/blob/master/doc/tunnels.md)
- [ADB 官方文档](https://developer.android.com/studio/command-line/adb)

---

**文档版本**: 1.0  
**更新日期**: 2026-01-23

