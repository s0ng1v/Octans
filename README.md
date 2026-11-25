<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/octans-wordmark-dark@2x.png">
    <source media="(prefers-color-scheme: light)" srcset="assets/octans-wordmark-light@2x.png">
    <img alt="Octans" src="assets/octans-wordmark-dark@2x.png" width="420">
  </picture>
</p>

<p align="center">
  <b>v0.5.0</b> · 轻量原生 Windows 无线外设电量托盘工具 · C11 + Win32 + Direct2D / DirectComposition
</p>

---

## 核心特性

- **极致轻量化**：纯 C11 后端内核 + 原生 Win32 / Direct2D / DirectComposition 界面，无 Electron / Chromium / WebView2 / .NET / `libstdc++` 运行时依赖；静默驻留托盘时 **0% CPU** 占用，闲置状态零后台网络请求。
- **三大宇宙显示样式（`Octans` / `Nebula` / `Pulsar`）**：
  - **`Octans`（默认）**：经典圆环电量 + 极简线性外设图标，面板与托盘风格统一。
  - **`Nebula`**：设备实心轮廓内液位填充 + 液面高光，托盘无外环满幅渲染，高辨识度。
  - **`Pulsar`**：12 刻度精密环形仪表盘（面板）+ 线性外设图标配底部彩色电量数字（托盘）。
  - 三套样式统一以 Octans 电量色阶为准（`>40%` 绿、`21%–40%` 橙、`≤20%` 红、充电/充满绿）。
- **关于与一键安全升级**：
  - 菜单内置「关于 Octans」独立弹窗，支持手动触发检查更新（优先走免登录国内 AtomGit 镜像，自动回退 GitHub Releases）。
  - 支持应用内直接下载新版安装程序、CNG SHA-256 完整性校验与一键静默升级，失败时自动提供发布页下载链接；平时不预加载网络库、零后台联网。
- **即开即显与南极星光波扫描**：
  - 首次启动自动全盘扫描 2.4G / 蓝牙 / 标准 HID / XInput 设备，空状态展示专属南极星光波探测动效，扫描完成后卡片交错浮现。
  - 零固定设备时任务栏仅显示透明 **Octans Logo**；点击任意设备卡片即可固定到任务栏（Logo 自动切换为设备电量图标），取消最后一个固定设备时自动恢复为 Octans Logo。
- **开机自启动与即插即用**：首次运行默认开启开机自启动（以 `--silent` 静默驻留托盘），自动监听 USB 接收器热插拔与系统休眠唤醒事件。

## 支持的外设范围

内置 15+ 类无线协议族、Windows 蓝牙电量（`DEVPKEY_Bluetooth_Battery`）、XInput 手柄电量以及标准 HID 电池协议，全面覆盖：

- **罗技 Logitech**：完整 HID++ 2.0（`0x1004` SoC+Capabilities / `0x1000` / `0x1001` / `0x1F20` / `0x0104` CentPP）+ HID++ 1.0（`0x0D` / `0x07` / `0x0F`）+ 蓝牙 `slot 0x00` + Busy/休眠自动唤醒重试，覆盖 G PRO / GPW / G502 / G903 / G304 / MX 全系鼠键与 G435 / G533 / G535 / G733 / G935 / G PRO X / PRO X 2 等无线耳机。
- **迈从 MCHOSE**：完整 v2 9 通道协议族（HID4D `0x4D`、MC Feature `0x11`、BY 键盘、G 系列 / G7 MAX、V9 / V9 Turbo / V9 Pro 2 / S9 / R9 等键鼠耳机全系）。
- **更多主流品牌**：雷蛇 Razer、华硕 ROG、海盗船 Corsair、赛睿 SteelSeries、HyperX、Keychron、ATK/VXE/VGN、雷柏 Rapoo、黑爵 AJAZZ、前行者 EWEADN、索尼 DualSense/DS4、任天堂 Switch Pro/Joy-Con 等。

详细支持列表与实机验证状态见 [docs/06-当前已支持外设清单.md](docs/06-当前已支持外设清单.md)。

## 构建与运行

需要 Windows 10/11 与 LLVM-MinGW (UCRT) 工具链：

```powershell
# 一键编译后端 CLI、单元测试与完整桌面托盘程序 Octans.exe
powershell -ExecutionPolicy Bypass -File .\build.ps1

# 打包为标准 Windows 安装程序（输出 dist\Octans-Setup-v0.5.0.exe）
powershell -ExecutionPolicy Bypass -File .\installer\package.ps1
```

构建产物位于 `build/`（安装程序位于 `dist/`）：

| 文件 | 说明 |
|---|---|
| `dist\Octans-Setup-v0.5.0.exe` | 标准 Windows 安装程序（支持开机自启动配置、一键静默升级与系统标准卸载） |
| `build\Octans.exe` | 完整桌面托盘主程序（直接运行自动打开面板；支持 `--silent` 静默启动到托盘） |
| `build\octans-cli.exe` | 独立无头命令行工具（支持 `list --json`、`discover`、`test-rule`、`probe --dump`） |
| `build\test_engine.exe` | 协议解析、规则引擎与 ABI 单元测试 |

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
cli/       无头命令行诊断与 JSON 输出入口
include/   Octans 后端与 UI 稳定 C ABI 契约头文件与版本定义
assets/    Octans 品牌图标、矢量字标与多分辨率应用图标
installer/ Inno Setup 安装脚本与一键打包脚本
```

## 开源协议与品牌声明

- **源代码许可**：本项目源代码遵循 [GNU General Public License v3.0 (GPL-3.0-only)](LICENSE) 协议开源。任何基于本项目的修改或再分发均须以同等 GPL-3.0 协议公开全部源代码。
- **品牌与美术资产保留**：`Octans` 名称、Logo、Wordmark、应用图标（`assets/` 目录下的全部品牌视觉资产）**不适用 GPL-3.0 协议，保留所有权利**。详见 [TRADEMARK.md](TRADEMARK.md)。任何二次分发或衍生作品须移除并替换为独立名称与图标。
- **商业双授权**：版权归原作者 `s0ng1v` 所有，如需非 GPL 商业授权请联系作者。
