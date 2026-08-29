# CH32V305 USBHS 内置 Flash U 盘

这是一个面向 **CH32V305RBT6（128 KiB Flash / 32 KiB SRAM）** 的 CherryUSB MSC
设备例程，用来验证板载 USB2.0 PHY 和本仓库的 CH32 USBHS device port。固件使用
PB6/PB7 USBHS 控制器，以 480 Mbps High Speed 枚举为可读写 U 盘。
例程参考 WCH 官方
[`CH32V307EVT/EVT/EXAM/USB/USBHS/DEVICE/MSC_U-Disk`](https://github.com/openwch/ch32v307/tree/main/EVT/EXAM/USB/USBHS/DEVICE/MSC_U-Disk)，
但针对 CH32V305 的 Flash 容量重新划分，并使用本仓库的 CherryUSB MSC 实现。

## Flash 布局

| 地址范围 | 大小 | 用途 |
| --- | ---: | --- |
| `0x00000000..0x0001DFFF` | 120 KiB | 固件（链接脚本限制） |
| `0x0001E000..0x0001FFFF` | 8 KiB | 16 × 512 B MSC 扇区 |

固件把已格式化的 FAT12 镜像直接链接到末尾 8 KiB，并通过 MSC 报告为可写介质。
当前验证版沿用 WCH 官方 USBHS MSC U-Disk 的实现方式：每次收到完整 512 B 扇区后，
直接擦写两个 256 B Flash fast page。安全弹出后，新建的小文件可跨复位保留。
文件系统元数据占用 2 KiB，实际可用于文件数据的空间约为 6 KiB。

CherryUSB 的 CH32 USBHS port 默认按 16 个双向 endpoint 各保留 `512 + 512` B，约占
15 KiB SRAM，即使 descriptor 没有使用这些端点也照样分配。本例只使用 EP0、EP2 IN
和 EP3 OUT，因此通过 `USB_NUM_BIDIR_ENDPOINTS=4` 把 DMA buffer 缩到 3 KiB；端点编号
仍全部落在已分配范围内，不改变 USB 描述符或端点包长。

TIM2 只用于驱动 PA8 心跳灯，不参与 USB 传输。CH32 USBHS port 不会因为 EP0 SETUP
请求暂停 MSC Bulk IN，也不需要应用层周期性调用 USB 控制器 service。
MSC 写入是同步完成的，并接受主机安全弹出时常用的 `SYNCHRONIZE CACHE(10/16)`。

## 硬件

- USBHS DM：PB6（封装 58 脚）
- USBHS DP：PB7（封装 59 脚）
- 必须接到 USBHS PHY 对应的 D+/D-，不能使用 USBFS 引脚代替。
- 使用可传数据的高速 USB 线，并保持差分线短、等长、无支路。
- 默认使用板载 8 MHz HSE：系统时钟为 HSE × 9 = 72 MHz，USBHS PHY PLL 参考时钟为
  HSE ÷ 2 = 4 MHz。配置参考时钟后等待约 10 ms，再启用 PHY PLL。
- 也可构建 48 MHz HSI 诊断版；其 USBHS PHY PLL 使用内部 8 MHz HSI ÷ 2 的 4 MHz
  参考时钟，完全不依赖外部晶振。

## 编译

需要 WCH 版 `riscv-none-embed-gcc`（MounRiver Studio 2 的 macOS 默认路径会自动识别）
和 WCH 官方 `openwch/ch32v307` EVT：

```sh
make -C examples/usbhs_udisk \
  WCH_EVT_ROOT=/path/to/ch32v307/EVT
```

输出文件：

```text
examples/usbhs_udisk/build/usbhs_udisk.elf
examples/usbhs_udisk/build/usbhs_udisk.bin
examples/usbhs_udisk/build/usbhs_udisk.map
```

默认产物使用 HSE。若要构建不依赖外部晶振的 HSI 诊断固件：

```sh
make -C examples/usbhs_udisk CLOCK_SOURCE=hsi \
  WCH_EVT_ROOT=/path/to/ch32v307/EVT
```

HSI 诊断版输出为 `usbhs_udisk_hsi.elf/.bin/.map`。如果 HSI 版能够稳定以 480 Mbps
枚举、而默认 HSE 版不能，应优先检查板载 8 MHz 晶振、OSC_IN/OSC_OUT 焊点、负载电容
以及周边走线。HSI 版成功只能说明 USBHS 控制器、PHY 和数据线基本正常，不能证明外部
晶振已经起振。

BIN 将 8 KiB FAT12 镜像放在末尾，因此文件大小固定为完整的 128 KiB。PA8 持续
闪烁表示主循环正常，Flash 擦写校验失败时 PA8 会转为常亮。

如果工具链不在 `PATH`，可指定其 `bin` 目录：

```sh
make -C examples/usbhs_udisk \
  WCH_EVT_ROOT=/path/to/ch32v307/EVT \
  TOOLCHAIN_BIN=/path/to/toolchain/bin
```

## 烧录与验证

1. 烧录 `.elf` 或 `.bin`，二进制起始地址为 `0x00000000`。
2. 复位后，将 USBHS 接口连接电脑；设备应枚举为 `1a86:fe13`。
3. 系统应挂载名为 `CH32V305HS` 的 8 KiB FAT12 卷，可新建总计约 6 KiB 的小文件。
4. 用系统信息或 `lsusb -t` 确认设备速度为 `480M`，不要仅凭“能识别 U 盘”判断 HS。
5. 新建一个小文件，安全弹出并复位，再次挂载后确认文件仍存在。重新烧录完整 128 KiB
   BIN 会把末尾磁盘区恢复成编译时镜像。

注意：格式化、操作系统索引文件和日志都会产生较多擦写。测试完成后应安全弹出；Flash
写入期间掉电可能损坏这个微型文件系统。

本例已于 2026-08-29 在 CH32V305RBT6 实机上验证：默认 HSE 版可成功以 480 Mbps
枚举；HSI 版可完成 480 Mbps 枚举和文件写入。
