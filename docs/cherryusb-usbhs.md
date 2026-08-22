# CherryUSB 与 USBHS 工程配置

## 原始实现引用

CH32V307 版 CherryUSB 实现最初来自 `crl6/cherryusb_ch32v307`：

- 原始仓库：<https://github.com/crl6/cherryusb_ch32v307>
- 原始 CherryUSB 目录：<https://github.com/crl6/cherryusb_ch32v307/tree/a017bee289f4fdfcdbba8bf59a76fdfc973875b5/CherryUSB>
- 对应提交：[`a017bee289f4fdfcdbba8bf59a76fdfc973875b5`](https://github.com/crl6/cherryusb_ch32v307/commit/a017bee289f4fdfcdbba8bf59a76fdfc973875b5)

本项目修复了该实现的一些BUG

## MounRiver Studio 中启用 USBHS

下图是原项目的关键配置：选中 `usb_dc_usbhs.c`，并在 C Compiler 的
Preprocessor 设置中定义 `CONFIG_USB_HS`。

![MounRiver Studio USBHS 配置](images/mounriver-usbhs-project-config.png)

按以下步骤配置工程：

1. 将仓库根目录的 `CherryUSB` 加入 MounRiver 工程。可以直接放入工程目录，或像
   ProShock 4 一样使用 Eclipse linked resource 指向该目录。
2. 在 `Project Properties -> C/C++ Build -> Settings -> GNU RISC-V Cross C Compiler
   -> Preprocessor` 的 `Defined symbols (-D)` 中加入 `CONFIG_USB_HS`。等价的命令行
   选项是 `-DCONFIG_USB_HS`。
3. 至少加入这些头文件搜索路径：

   ```text
   CherryUSB
   CherryUSB/common
   CherryUSB/core
   CherryUSB/class/<实际使用的 class>
   CherryUSB/port/ch32
   ```

4. Device 工程至少编译 `CherryUSB/core/usbd_core.c`、实际使用的 class 源文件，以及
   `CherryUSB/port/ch32/usb_dc_usbhs.c`。同一工程不要同时编译
   `usb_dc_usbfs.c`、`usb_dc_ch58x.c` 或其他 device-controller port；否则会出现重复
   API/IRQ 实现或使用错误控制器。
5. `usb_dc_usbhs.c` 提供弱定义的 `usb_dc_low_level_init()` 和
   `usb_dc_low_level_deinit()`。板级代码应提供强定义，完成 USBHS 时钟、PHY 和 IRQ
   初始化。以 ProShock 4 的外部 HSE 配置为例：

   ```c
   void usb_dc_low_level_init(void)
   {
       RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
       RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
       RCC_USBHSConfig(RCC_USBPLL_Div2);
       RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);
       RCC_USBHSPHYPLLALIVEcmd(ENABLE);
       RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
       NVIC_EnableIRQ(USBHS_IRQn);
   }
   ```

   上述 PLL source、分频和 reference frequency 必须与本板实际 HSE/系统时钟方案一致，
   不应在时钟源不同的板上原样照搬。
6. USBHS 使用芯片的 HS PHY/USBHS 引脚，PCB 必须把对应 D+/D- 引脚接到 USB 接口；
   不能仅靠 `CONFIG_USB_HS` 将接在 USBFS/OTG_FS 引脚上的硬件变成 USBHS。
7. 调用 CherryUSB 的 descriptor/class 注册与 `usbd_initialize()` 前，先完成板级时钟
   初始化。高速 descriptor 的 endpoint max packet size 和 interval 也要按实际 class
   与目标轮询率设置，不能只修改编译宏。

## 快速自检

- 编译命令中能看到 `-DCONFIG_USB_HS`。
- 最终只链接一个 CH32 device-controller port：`usb_dc_usbhs.c`。
- map 文件中存在 `USBHS_IRQHandler`，且没有重复定义。
- `usb_dc_low_level_init()` 使用的是本板时钟配置，并启用了 `USBHS_IRQn`。
- 主机枚举结果显示目标 speed 和 descriptor；若只能枚举为 Full Speed，优先检查 PHY
  时钟、D+/D- 引脚/布线、线缆与 descriptor，而不是继续叠加宏。
