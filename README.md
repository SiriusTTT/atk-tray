# ATK Tray

系统托盘工具，监控 ATK 无线鼠标的电量、DPI 和回报率。纯 Win32 C++，无外部依赖。

基于 [rapoo-tray](https://github.com/Iris-0109/rapoo-tray) 改编，但 ATK 用的是完全不同的
COMPX 协议，实现是重新逆向的。

## 功能

- 托盘图标显示电池电量（4x 超采样抗锯齿）
- 托盘提示显示：电量百分比、电压、充电状态、当前 DPI 和档位、回报率
- 按鼠标 DPI 键切换档位时弹出 OSD 通知
- 自动区分「接收器未插」和「鼠标休眠」两种状态
- 支持开机自启动
- 调试日志写入可执行文件同目录的 `atk-tray.log`

## 编译

```cmd
build.bat
```

需要 MinGW g++ 或 MSVC cl.exe。产物在 `bin\atk-tray.exe`。

手动编译（MinGW）：

```bash
g++ -o bin/atk-tray.exe src/main.cpp -static -mwindows -municode \
    -lsetupapi -lhid -luser32 -lgdi32 -lshell32 -ladvapi32 -O2
```

## 使用方法

1. 运行 `atk-tray.exe`
2. 托盘出现电池图标
3. 左键点击图标：显示 DPI/电量 OSD
4. 右键点击图标：菜单（电量、DPI、回报率、自启动、重连、退出）

## 支持的设备

| 设备 | VID | PID | 状态 |
| :--- | :--- | :--- | :---: |
| ATK Mouse 8K Dongle（2.4G 接收器） | `0x373B` | `0x101B` | ✅ 已实测 |
| ATK 有线鼠标 | `0x373B` | `0x104A` | 未实测 |

## 协议

逆向自 ATK HUB 的 COMPX 实现，并在一台 ATK Mouse 8K Dongle 上逐字节验证。
协议层在 `src/atk_protocol.h`。

### 通道

走 **col05** vendor collection（usage page `0xFF02`，usage `0x0002`），
17 字节双向报文，**report ID `0x08`**。

> 注意 col06（8 字节 feature）和 col07（49 字节）都不是数据通道。
> col06 里那三个恒定的 `0x64` 是静态常量，**不是电量**。
> col07 是固件升级通道。

### 报文格式

```
wire:  08 | b0 b1 b2 b3 b4 b5 ... b14 | b15
             ↑  ↑  ↑------↑  ↑  ↑------↑  ↑
             │  │  │         │  │         └ 校验和
             │  │  │         │  └ 数据 (10 字节)
             │  │  │         └ 数据有效长度
             │  │  └ EEPROM 地址 (大端 u16)
             │  └ 状态 (请求填 0；响应 0=成功, 1=失败)
             └ 命令 ID (同时用作响应匹配字节)
```

校验和（`0x08` 是 report ID，不是笔误）：

```
checksum = (0x55 - ((0x08 + Σ b0..b14) & 0xFF)) & 0xFF
```

### 命令

| ID | 名称 | 说明 |
| :--- | :--- | :--- |
| `0x03` | GetWirelessMouseOnline | `b5`: 1=在线 0=休眠。**接收器自己应答** |
| `0x04` | GetBatteryLevel | `b5`=电量% `b6`=1 充电/0 放电 `b7..8`=电压 mV（大端） |
| `0x08` | GetEEPROM | `b4` = **要读的字节数**，见下 |
| `0x10` | GetMouseCIDMID | `b5`=cid `b6`=mid |

### EEPROM 地址

| 地址 | 长度 | 内容 |
| :--- | :--- | :--- |
| `0` | 10 | 回报率块：`b5`=回报率 `b7`=档位总数 `b9`=当前档位 `b11`=bhop `b13`=按键模式 |
| `12` / `20` / `28` / `36` | 8 | DPI 值块，每块 2 个槽位 |

单字节设置项都跟一个 `0x55 - value` 的校验字节。

回报率编码：`1`=1000Hz `2`=500 `4`=250 `8`=125 `16`=2000 `32`=4000 `64`=8000

### DPI 槽位

每个槽位 4 字节：`{xDpi, yDpi, dpiEx, crc}`，其中 `crc = (0x55 - (x+y+ex)) & 0xFF`。
槽位序号 = 块内偏移 ×2 + i。

`dpiEx` 是位域，按 PAW3950Ultra 解码（也是 ATK HUB 对未知 COMPX 鼠标的回退方案）：

```c
int x = xRaw | (((dpiEx & 0x0C) >> 2) << 8);
int y = yRaw | (((dpiEx & 0xC0) >> 6) << 8);
int rx = (dpiEx & 0x02) ? (50 * x + 10050) : (10 * (x + 1));
int ry = (dpiEx & 0x20) ? (50 * y + 10050) : (10 * (y + 1));
if (dpiEx & 0x01) rx *= 2;
if (dpiEx & 0x10) ry *= 2;
```

实测某鼠标：`400 / 800 / 1600 / 3200`，四个槽位的 CRC 全部自洽。

### 两个容易踩的坑

1. **`GetEEPROM` 的 `b4`（数据长度）传 0 会被拒绝**（返回 status=1）。
   它是「要读几个字节」，不是「有没有数据」。
2. **鼠标休眠时，转发给鼠标的命令一律无响应**（battery / EEPROM / CID-MID），
   而接收器自己处理的命令（`0x03` 在线状态、`0x19` 接收器灯效）照常应答。
   所以「无响应」= 鼠标睡了，**不是协议错误**。程序据此区分两种状态。

另外：ATK HUB 的后台服务 `atk-hub-service.exe` 会占用同一 collection，
`CreateFile` 可能瞬时失败，需要重试。

## 调试工具

`tools/` 下是实测用的探针，排查协议问题时很有用：

| 工具 | 用途 |
| :--- | :--- |
| `atk_probe.exe` | 基础查询（电量、在线、版本、CID/MID） |
| `atk_sweep.exe` | 扫描全部命令 ID 和 EEPROM 地址 |
| `atk_listen.exe` | 监听全部 vendor collection 的输入报文 |
| `atk_eeprom.exe` | 按地址范围读取 EEPROM |

## 致谢

- [rapoo-tray](https://github.com/Iris-0109/rapoo-tray) —— 项目结构参考
- [libatk-rs](https://github.com/cyberphantom52/libatk-rs) —— 协议起点（报文格式与校验和）
