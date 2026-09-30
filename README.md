# SSVEP范式课程设计参考项目

![三位同学进行 SSVEP 脑电实验的简笔插画](assets/readme-cover.png)

该项目基于SSVEP范式实现了基础的视觉诱发脑机接口，不含眼动、字符输入。仅**离线训练**和**在线测试**两个实验入口，以及左侧的电极阻抗图。40 个闪烁方块没有绑定文字或指令，识别结果可以自行赋予用途。

## 简单介绍

已根据当前实现生成应用程序 `dist/ssvep_stu.exe`，运行时请保留整个 `dist` 文件夹。从 GitHub 下载/ fork 源码后，后续更新优化可按下文自行构建。

### *没有脑电设备时

可以打开**刺激预览**，此模式不采集数据。

### *有脑电设备时

本项目使用 eego 放大器、1000 Hz 采样和九个后顶枕通道。

1. 先点击**开始阻抗检测**检查电极，确保阻抗质量后结束检测。
2. 输入被试编号、训练轮数，点击**离线训练**，按照指示进行实验即可。完成后将会自动保存脑电数据并训练 TRCA 模型。
3. 点击**在线测试**，窗口每次提示一个目标，识别出的方块显示绿色，正确率会累计计算。重新启动程序时，可从顶部菜单载入已有模型。

数据和模型分别写入项目根目录中的 `data/`、`model/`。

## 改动空间

| 内容 | 主要文件 |
| --- | --- |
| 40 目标的频率、相位和布局 | `src/SSVEPOfflineThread.cpp`、`src/StimulusWindow.cpp` |
| 提示、频闪、打标、反馈的流程 | `src/SSVEPOfflineThread.cpp`、`src/SSVEPOnlineThread.cpp`（流程），`src/Session.cpp`（界面衔接） |
| 阻抗图、阻抗和脑电采集 | `ui/Impedance.ui`、`images/`、`src/ImpedancePanel.cpp`、`src/EEGThread.cpp` |
| 预处理、训练和在线识别 | `src/SSVEPPreprocess.cpp`、`src/SSVEPModelTrainer.cpp`、`src/SSVEPOnlineThread.cpp` |

可以尝试改进**刺激布局与帧同步**、**识别模型和抗干扰能力**，或者把 40 个独立选项设计成不同的**智能设备控制**新交互。

## 自行构建

需要 Windows、CMake、MSVC、Qt 5.12+ 和 Eigen 3。若只想编译界面及刺激预览：

```bat
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/5.12.12/msvc2017_64 -DEIGEN3_INCLUDE_DIR=C:/path/to/eigen
cmake --build build --config Release
```

如需连接 eego 设备，还需安装对应驱动和 SDK，配置时使用：

```bat
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/5.12.12/msvc2017_64 -DEIGEN3_INCLUDE_DIR=C:/path/to/eigen -DSSVEP_WITH_EEGO=ON -DEEGO_SDK_ROOT=C:/path/to/sdk/include -DEEGO_SDK_LIB=C:/path/to/sdk/lib/eego-SDK.lib
cmake --build build --config Release
```

运行时还需要 Qt 运行库；使用 eego 时需要 `eego-SDK.dll`。