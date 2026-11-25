<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/octans-wordmark-dark@2x.png">
    <source media="(prefers-color-scheme: light)" srcset="assets/octans-wordmark-light@2x.png">
    <img alt="Octans" src="assets/octans-wordmark-dark@2x.png" width="420">
  </picture>
</p>

<p align="center">
  <b>v0.3.0</b> · 轻量原生 Windows 无线外设电量托盘工具 · C11 + Win32 + Direct2D / DirectComposition
</p>

---

## 核心特性

- **极致轻量化**：纯 C11 后端内核 + 原生 Win32 / Direct2D / DirectComposition 界面，无 Electron / Chromium / WebView2 / .NET / `libstdc++` 运行时依赖；静默驻留托盘时 **0% CPU** 占用，完全离线、零遥测、零网络请求。
- **即开即显的苹果风电量面板**：
  - 适配 Windows 11 Acrylic 毛玻璃与浅色/深色系统主题，打开面板触发同步书写式品牌 Wordmark 入场动画。
  - 支持鼠标、键盘、耳机、TWS 耳机、手柄、触控笔、音箱等全品类矢量图标与圆环电量动效。
  - 未检测到无线外设时，托盘与面板空状态均展示纯透明无背景 **Octans Logo**。
- **双模高敏系统托盘**：支持「圆环图标」与「图标 + 数字」两种托盘样式实时无级缩放（默认 `50%` 标准尺寸），支持任意多台设备同时固定到任务栏托盘，支持设备自定义重命名与持久化。
- **开机自启动与即插即用**：首次运行默认开启开机自启动（以 `--silent` 静默驻留托盘），自动监听 USB 接收器热插拔与系统休眠唤醒事件。

## 支持的外设范围

内置 15+ 类无线协议族、Windows 蓝牙电量（`DEVPKEY_Bluetooth_Battery`）、XInput 手柄电量以及标准 HID 电池协议，覆盖罗技 HID++、雷蛇、华硕 ROG、海盗船、赛睿、HyperX、Keychron、ATK/VXE/VGN、雷柏、迈从、黑爵、前行者、索尼 DualSense/DS4、任天堂 Switch Pro/Joy-Con 等主流型号。

详细支持列表与实机验证状态见 [docs/06-当前已支持外设清单.md](docs/06-当前已支持外设清单.md)。

## 添加未识别设备指南

对于暂未直接收录的新款/私有协议 2.4G 无线外设，Octans 采用「**开局全面自动扫描 + 三步傻瓜式【跟着官方驱动学一次】向导**」方案，**桌面驱动 / 网页驱动通杀、离线可用、零第三方驱动、零常驻膨胀**：

1. **优先全面初始化扫描（零操作）**：每次启动或打开向导时自动扫描内置协议族 + 外部规则 + 公模广谱指纹，识别成功直接固化进 `%APPDATA%\Octans\devices.ini`，未识别设备写入负缓存，托盘日常轮询 0 额外开销。
2. **三步傻瓜式学习（桌面驱动 / 网页驱动通用）**：右键托盘或面板菜单「**添加 / 发现设备...**」（启动独立 `octans-add.exe`，与 Octans 统一 Direct2D 毛玻璃风格）：
   - **① 选设备**：选中未识别的 2.4G 设备卡片，点击「**跟着官方驱动学一次**」（仅学习期间临时授权 Windows 原生 `Microsoft-Windows-USB-UCX` 诊断监听，常驻 `Octans.exe` 永远无需管理员权限）；
   - **② 看一眼官方驱动并填一个数字**：保持 2.4G 无线连接，打开该设备的官方桌面驱动（或网页驱动）看到电量后关闭官方驱动，在向导里填入 `1–100` 电量整数；
   - **③ 脱离官方驱动真机验证并启用**：向导自动推断协议并独立向 2.4G 接收器重放验证，读出准确电量后一键保存并热重载到 `Octans.exe` 立即显示。

## 构建与运行

需要 Windows 10/11 与 LLVM-MinGW (UCRT) 工具链：

```powershell
# 一键编译后端 CLI、单元测试与完整桌面托盘程序 Octans.exe
powershell -ExecutionPolicy Bypass -File .\build.ps1

# 打包为标准 Windows 安装程序（输出 dist\Octans-Setup-v0.3.0.exe）
powershell -ExecutionPolicy Bypass -File .\installer\package.ps1
```

构建产物位于 `build/`（安装程序位于 `dist/`）：

| 文件 | 说明 |
|---|---|
| `dist\Octans-Setup-v0.3.0.exe` | 标准 Windows 安装程序（支持开机自启动配置与系统标准卸载） |
| `build\Octans.exe` | 完整桌面托盘主程序（直接运行自动打开面板；支持 `--silent` 静默启动到托盘） |
| `build\octans-add.exe` | 独立按需 3 步设备学习向导（Direct2D 统一风格 + Windows 原生 USB ETW，零常驻内存） |
| `build\octans-cli.exe` | 独立无头命令行工具（支持 `list --json`、`discover`、`test-rule`、`probe --dump`） |
| `build\test_engine.exe` | 协议解析、规则引擎、USB ETW 协议推断与 ABI 单元测试 |

单独编译并预览界面 Demo（无需连接真实硬件）：

```powershell
powershell -ExecutionPolicy Bypass -File .\ui\build_ui.ps1
.\build\octans_ui_demo.exe
```

## 目录结构

```text
core/      底层 HID / SetupDi 蓝牙 / XInput / cfgmgr32 热插拔（纯 C11）
engine/    多协议解析引擎、广谱试探、外部规则引擎与持久化（纯 C11）
ui/        原生 Direct2D / DirectWrite / DirectComposition 界面与托盘组件
app/       桌面程序入口（单实例互斥、热插拔/热重载监听、异步后端工作线程）
tools/     独立按需运行设备添加向导与 WebHID 抓包工具（octans-add.exe）
cli/       无头命令行诊断与 JSON 输出入口
include/   Octans 后端与 UI 稳定 C ABI 契约头文件与版本定义
assets/    Octans 品牌图标、矢量字标与多分辨率应用图标
installer/ Inno Setup 安装脚本与一键打包脚本
```

## 开源协议与品牌声明

- **源代码许可**：本项目源代码遵循 [GNU General Public License v3.0 (GPL-3.0-only)](LICENSE) 协议开源。任何基于本项目的修改或再分发均须以同等 GPL-3.0 协议公开全部源代码。
- **品牌与美术资产保留**：`Octans` 名称、Logo、Wordmark、应用图标（`assets/` 目录下的全部品牌视觉资产）**不适用 GPL-3.0 协议，保留所有权利**。详见 [TRADEMARK.md](TRADEMARK.md)。任何二次分发或衍生作品须移除并替换为独立名称与图标。
- **商业双授权**：版权归原作者 `s0ng1v` 所有，如需非 GPL 商业授权请联系作者。
