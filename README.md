# PY32F002AL15S6TU：栅极 DC 启动 2.6 V、停止 2.2 V

本版使用你在 MOS 栅极测到的万用表 DC 门限。
PA1 由 3.3 V LDO 供电的单片机直接驱动，因此用 3.3 V 高电平计算占空比，
不再使用电机的 18650 电源电压，也不采用 RMS 平方换算。

## 默认行为

- 从关闭状态启动：立即跳到栅极平均约 2.6 V，再用 5 秒线性升至约 3.3 V / 100%。
- 关闭：从当前栅极平均电压用 5 秒线性降至约 2.2 V，然后立即将 PWM 写零。
- 起停边界触发 TIM1 UG，使比较值立即装载。
- 渐变中再次按键，从当前值用 5 秒向相反目标渐变。
- 若缓关闭途中已降到 2.6 V 以下，此时重新开启会保留当前值，不跳回 2.6 V。
  只有已经完全关断后，下一次启动才跳到 2.6 V。
- 按下、松开各做 20 ms 消抖，长按只触发一次。
- 上电输出为零；上电时按住不会启动，先松开再按下。
- PA3 是 EXTI3 中断按键，PA1 是 TIM1_CH4 / AF13 硬件 PWM，默认 20 kHz。
- 不使用阻塞延时等待渐变，普通睡眠时 PWM 持续输出，SysTick 每毫秒唤醒。

## 参数与占空比

在 Src/main.c 中修改：

```c
#define GATE_HIGH_MV        3300U  /* PA1 输出 100% 时实测栅极高电平 */
#define GATE_START_MV       2600U  /* 从关闭启动的门限 */
#define GATE_STOP_MV        2200U  /* 缓关闭终点，随后立即关断 */
#define RAMP_TIME_MS        5000U
```

按照“栅极 DC 平均电压 ≈ 栅极高电平 × 占空比”换算。
TIM1 默认周期 400 计数，占空比向上取整至一个计数：

| 状态 | 理想占空比 | CCR4 | 实际 PWM 占空比 | 理想估算栅极 DC |
| --- | --- | --- | --- | --- |
| 启动点 | 2600/3300 ≈ 78.79% | 316 | 79.00% | 2.607 V |
| 停止点 | 2200/3300 ≈ 66.67% | 267 | 66.75% | 2.203 V |
| 全开 | 100% | 400 | 100% | 3.300 V |
| 关闭 | 0% | 0 | 0% | 0 V |

因此启动时 CCR4 直接从 0 跳到 316，随后渐变到 400；
停止时从当前值渐变到约 267，随后直接变为 0。
不会再从 0% 做 5 秒渐变，也不会在 66.75% 停留不关断。

三个电压参数满足 0 < STOP <= START < HIGH。
如果 100% PWM 时栅极 DC 实测不是 3.3 V，请按实测值校准 GATE_HIGH_MV。
这里改变的是 PWM 的时间占比；栅极仍是开关脉冲，并非输出可调的恒定直流栅压。
门限来自你的当前电路实测，本例不保证电机转速与栅极平均电压线性对应。

## 接线与工程

| SOP8 物理脚 | 信号 | 用途 |
| --- | --- | --- |
| 1 | VCC | LDO 输出 3.3 V |
| 3 | PA3 | 常开按键，另一端接 GND |
| 4 | PA14 / SWCLK | 下载器 SWCLK |
| 5 | PA13 / SWDIO | 下载器 SWDIO |
| 7 | PA1 / TIM1_CH4 | MOS 栅极控制 |
| 8 | VSS | 电路共地 |

2、6 脚本例不使用。VCC / VSS 就近接去耦电容。
本版针对你确认的 PA1 直接驱动低端 N 沟道 MOS、高电平导通连接。
原有栅极下拉及电机续流回路仍应保留。

1. 安装支持 PY32F002A 的 Puya.PY32F0xx_DFP 器件包。
2. 解压本次 Gate_2V6_2V2 工程，打开 MDK-ARM/Project.uvprojx。
3. 目标名称为 PY32F002AL15S6TU_Gate_2V6_2V2，器件为 PY32F002Ax5。
4. 默认采用官方 ARM Compiler 5 模板，DFP 原引用 1.2.0；
   若装其他器件包版本，可在 Device 中重新选择 PY32F002Ax5。
5. 使用 Rebuild all target files，新的输出文件名为：
   MDK-ARM/Output/PY32F002_Gate_2V6_2V2.hex。
6. 使用实际下载器、SWD 和适合 PY32F002A 20 KB Flash 的算法下载。
7. 若已有上一版电机工程，可完整替换其实际参与编译的 Src/main.c，然后重新编译下载。
   不要把两份含 main() 的文件同时加入工程。

## 验证范围

- 新 main.c 通过 ARM Cortex-M0+ / C99 / freestanding 编译，
  使用 -Wall -Wextra -Werror。
- 主机模拟检查首个非零值为 316，停止前非零值为约 267，最后直接写零；
  首个非零及最终零写入都触发 UG。
- 验证启动、停止、两种中途反向、低于 2.6 V 时重新开启不跳变、
  不同起停门限、消抖、长按、上电按住、计时回绕、APB 倍频和故障关闭。
- 逐毫秒检查理论栅极 DC 线性曲线及单调性；默认配置通过
  AddressSanitizer / UndefinedBehaviorSanitizer。
- 没有在本机用 Keil5 完整编译链接、下载或实物测量，未附带 HEX。

Tests 目录不参与 Keil 编译。可在工程根目录用 clang 运行模拟测试：

```sh
clang -std=c99 -Wall -Wextra -Werror -ITests -IInc Tests/test_motor.c -lm -o test_motor
./test_motor
```

## 来源和许可

[普冉官方固件库](https://github.com/OpenPuya/PY32F0xx_Firmware)、
[器件包和文档](https://www.puyasemi.com/download.html?keywords=Py32f002)。
驱动、CMSIS、启动文件保留原版权和许可，并附 LICENSE。
本地应用、EXTI 中断入口、故障关闭及 Keil 工程列表已更新。
HAL 定义头文件中的 stdio.h 包含改为 stddef.h，用于 freestanding 编译检查。
