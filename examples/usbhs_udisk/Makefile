EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(EXAMPLE_DIR)/../..)

# Point this at the EVT directory inside the official openwch/ch32v307 tree.
WCH_EVT_ROOT ?= $(abspath $(REPO_ROOT)/../ch32v307/EVT)
WCH_SRC := $(WCH_EVT_ROOT)/EXAM/SRC
WCH_USBHS_MSC_USER := $(WCH_EVT_ROOT)/EXAM/USB/USBHS/DEVICE/MSC_U-Disk/User

MRS_TOOLCHAIN_BIN := /Applications/MounRiver Studio 2.app/Contents/Resources/app/resources/darwin/components/WCH/Toolchain/RISC-V Embedded GCC/bin
ifneq ($(shell test -x "$(MRS_TOOLCHAIN_BIN)/riscv-none-embed-gcc" && echo yes),)
TOOLCHAIN_BIN ?= $(MRS_TOOLCHAIN_BIN)
endif

ifneq ($(strip $(TOOLCHAIN_BIN)),)
CROSS_COMPILE := $(TOOLCHAIN_BIN)/riscv-none-embed-
else
CROSS_COMPILE ?= riscv-none-embed-
endif

CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE    := $(CROSS_COMPILE)size

BUILD_DIR := $(EXAMPLE_DIR)/build
TARGET := $(BUILD_DIR)/usbhs_udisk

INCLUDES := \
	-I$(EXAMPLE_DIR) \
	-I$(REPO_ROOT)/CherryUSB \
	-I$(REPO_ROOT)/CherryUSB/common \
	-I$(REPO_ROOT)/CherryUSB/core \
	-I$(REPO_ROOT)/CherryUSB/class/msc \
	-I$(REPO_ROOT)/CherryUSB/port/ch32 \
	-I$(WCH_SRC)/Core \
	-I$(WCH_SRC)/Peripheral/inc \
	-I$(WCH_USBHS_MSC_USER)

SOURCES := \
	$(EXAMPLE_DIR)/main.c \
	$(EXAMPLE_DIR)/disk_image.c \
	$(REPO_ROOT)/CherryUSB/core/usbd_core.c \
	$(REPO_ROOT)/CherryUSB/class/msc/usbd_msc.c \
	$(REPO_ROOT)/CherryUSB/port/ch32/usb_dc_usbhs.c \
	$(EXAMPLE_DIR)/flash_disk.c \
	$(WCH_SRC)/Core/core_riscv.c \
	$(WCH_SRC)/Peripheral/src/ch32v30x_flash.c \
	$(WCH_SRC)/Peripheral/src/ch32v30x_gpio.c \
	$(WCH_SRC)/Peripheral/src/ch32v30x_rcc.c \
	$(WCH_SRC)/Peripheral/src/ch32v30x_tim.c \
	$(EXAMPLE_DIR)/system_ch32v30x_hsi.c \
	$(WCH_SRC)/Startup/startup_ch32v30x_D8C.S

ARCH_FLAGS := -march=rv32imacxw -mabi=ilp32 -msave-restore
CFLAGS := $(ARCH_FLAGS) -std=gnu99 -Os -g3 -ffunction-sections -fdata-sections \
	-fno-common -fsigned-char -Wall -Wextra -DCH32V30x_D8C \
	-DCONFIG_USB_HS $(INCLUDES)
LDFLAGS := $(ARCH_FLAGS) -nostartfiles --specs=nano.specs --specs=nosys.specs \
	-Wl,--gc-sections -Wl,-Map,$(TARGET).map -T$(EXAMPLE_DIR)/link.ld

.PHONY: all clean check-sdk

all: $(TARGET).bin

check-sdk:
	@test -f "$(WCH_SRC)/Peripheral/inc/ch32v30x.h" || { \
		echo "WCH EVT not found: set WCH_EVT_ROOT=/path/to/ch32v307/EVT"; exit 1; }
	@test -f "$(WCH_USBHS_MSC_USER)/system_ch32v30x.h" || { \
		echo "WCH USBHS MSC headers not found under WCH_EVT_ROOT"; exit 1; }

$(BUILD_DIR):
	@mkdir -p "$@"

$(TARGET).elf: check-sdk | $(BUILD_DIR)
	"$(CC)" $(CFLAGS) $(SOURCES) $(LDFLAGS) -o "$@"
	"$(SIZE)" "$@"

$(TARGET).bin: $(TARGET).elf
	"$(OBJCOPY)" -O binary "$<" "$@"

clean:
	rm -rf "$(BUILD_DIR)"
