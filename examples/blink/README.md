# PA8 呼吸灯

这个最小示例使用 CH32V305RBT6 的 `TIM1_CH1` 在 `PA8` 输出约 1 kHz PWM，
驱动板载用户 LED 做约 4 秒一个周期的呼吸效果。亮度使用平方曲线校正，低亮度段的
变化比线性占空比更自然。

默认按这块板的 LED **高电平点亮** 配置。若其他板卡上的 LED 为低电平点亮，编译时
传入 `CFLAGS_EXTRA=-DLED_ACTIVE_LOW=1` 即可反转极性。

## 编译

需要 WCH 版 `riscv-none-embed-gcc`（MounRiver Studio 2 的 macOS 默认路径会自动识别）
和 WCH 官方 `openwch/ch32v307` EVT：

```sh
make -C examples/blink WCH_EVT_ROOT=/path/to/ch32v307/EVT
```

输出文件为：

```text
examples/blink/build/blink.elf
examples/blink/build/blink.bin
examples/blink/build/blink.map
```

如果工具链不在 `PATH`，可额外指定 `TOOLCHAIN_BIN=/path/to/toolchain/bin`。

## 烧录

烧录 `blink.elf`，或将 `blink.bin` 写入地址 `0x00000000`，复位后板载 PA8 LED
应持续渐亮、渐暗。若 LED 常灭或明暗方向不对，按上面的 `LED_ACTIVE_LOW=1`
方式重新编译。
