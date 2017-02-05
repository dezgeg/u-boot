/*
 * (C) Copyright 2012-2016 Stephen Warren
 *
 * SPDX-License-Identifier:	GPL-2.0
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include <linux/sizes.h>
#include <asm/arch/timer.h>

#if defined(CONFIG_TARGET_RPI_2) || defined(CONFIG_TARGET_RPI_3_32B)
#define CONFIG_SKIP_LOWLEVEL_INIT
#endif

/* Architecture, CPU, etc.*/
#define CONFIG_ARCH_CPU_INIT

/* Use SoC timer for AArch32, but architected timer for AArch64 */
#ifndef CONFIG_ARM64
#define CONFIG_SYS_TIMER_RATE		1000000
#define CONFIG_SYS_TIMER_COUNTER	\
	(&((struct bcm2835_timer_regs *)BCM2835_TIMER_PHYSADDR)->clo)
#endif

/*
 * 2835 is a SKU in a series for which the 2708 is the first or primary SoC,
 * so 2708 has historically been used rather than a dedicated 2835 ID.
 *
 * We don't define a machine type for bcm2709/bcm2836 since the RPi Foundation
 * chose to use someone else's previously registered machine ID (3139, MX51_GGC)
 * rather than obtaining a valid ID:-/
 *
 * For the bcm2837, hopefully a machine type is not needed, since everything
 * is DT.
 */
#ifdef CONFIG_BCM2835
#define CONFIG_MACH_TYPE		MACH_TYPE_BCM2708
#endif

/* Memory layout */
#define CONFIG_NR_DRAM_BANKS		1
#define CONFIG_SYS_SDRAM_BASE		0x00000000
#ifdef CONFIG_ARM64
#define CONFIG_SYS_TEXT_BASE		0x00080000
#else
#define CONFIG_SYS_TEXT_BASE		0x00008000
#endif
#define CONFIG_SYS_UBOOT_BASE		CONFIG_SYS_TEXT_BASE
/*
 * The board really has 256M. However, the VC (VideoCore co-processor) shares
 * the RAM, and uses a configurable portion at the top. We tell U-Boot that a
 * smaller amount of RAM is present in order to avoid stomping on the area
 * the VC uses.
 */
#define CONFIG_SYS_SDRAM_SIZE		SZ_128M
#define CONFIG_SYS_INIT_SP_ADDR		(CONFIG_SYS_SDRAM_BASE + \
					 CONFIG_SYS_SDRAM_SIZE - \
					 GENERATED_GBL_DATA_SIZE)
#define CONFIG_SYS_MALLOC_LEN		SZ_4M
#define CONFIG_SYS_MEMTEST_START	0x00100000
#define CONFIG_SYS_MEMTEST_END		0x00200000
#define CONFIG_LOADADDR			0x00200000

/* Devices */
/* GPIO */
#define CONFIG_BCM2835_GPIO
/* LCD */
#define CONFIG_LCD_DT_SIMPLEFB
#define CONFIG_VIDEO_BCM2835

#ifdef CONFIG_CMD_USB
#define CONFIG_TFTP_TSIZE
#define CONFIG_MISC_INIT_R
#endif

/* Console UART */
#ifdef CONFIG_BCM2837
#define CONFIG_BCM283X_MU_SERIAL
#else
#define CONFIG_PL01X_SERIAL
#endif

/* Console configuration */
#define CONFIG_SYS_CBSIZE		1024

/* Environment */
#define CONFIG_ENV_SIZE			SZ_16K
#define CONFIG_ENV_VARS_UBOOT_CONFIG
#define CONFIG_SYS_LOAD_ADDR		0x1000000
#define CONFIG_PREBOOT			"usb start"

/* Shell */
#define CONFIG_CMDLINE_EDITING

/* ATAGs support for bootm/bootz */
#define CONFIG_SETUP_MEMORY_TAGS
#define CONFIG_CMDLINE_TAG
#define CONFIG_INITRD_TAG

#include <config_distro_defaults.h>

/* Environment */
#define CONFIG_ENV_VARS_UBOOT_RUNTIME_CONFIG
#define ENV_DEVICE_SETTINGS \
	"stdin=serial,usbkbd\0" \
	"stdout=serial,vidconsole\0" \
	"stderr=serial,vidconsole\0"

#ifdef CONFIG_ARM64
#define FDT_HIGH "ffffffffffffffff"
#define INITRD_HIGH "ffffffffffffffff"
#else
#define FDT_HIGH "ffffffff"
#define INITRD_HIGH "ffffffff"
#endif

/*
 * Memory layout for where various images get loaded by boot scripts:
 *
 * I suspect address 0 is used as the SMP pen on the RPi2, so avoid this.
 *
 * fdt_addr_r simply shouldn't overlap anything else. However, the RPi's
 *   binary firmware loads a DT to address 0x100, so we choose this address to
 *   match it. This allows custom boot scripts to pass this DT on to Linux
 *   simply by not over-writing the data at this address. When using U-Boot,
 *   U-Boot (and scripts it executes) typicaly ignore the DT loaded by the FW
 *   and loads its own DT from disk (triggered by boot.scr or extlinux.conf).
 *
 * kernel_addr_r has different constraints on ARM and Aarch64.
 *   For Aarch64, the kernel image is uncompressed and must be loaded at
 *   text_offset bytes (specified in the header of the Image) into a
 *   2MB boundary. As Linux uses a default text_offset of 0x80000, load
 *   the kernel at 0x80000 so that the 'booti' command does not need
 *   to perform any relocation of the Image in the typical case.
 *
 *   For 32-bit ARM, it must be within the first 128M of RAM in order for the
 *   kernel's CONFIG_AUTO_ZRELADDR option to work. Since the kernel will
 *   decompress itself to 0x8000 after the start of RAM, kernel_addr_r
 *   should not overlap that area, or the kernel will have to copy itself
 *   somewhere else before decompression. Similarly, the address of any other
 *   data passed to the kernel shouldn't overlap the start of RAM. Pushing
 *   this up to 48M allows for a sizable kernel to be decompressed below the
 *   compressed load address.
 *
 * scriptaddr can be pretty much anywhere that doesn't conflict with something
 *   else. Choosing 64M allows for the compressed kernel to be up to 16M on
 *   32-bit ARM and roughly 64M for the uncompressed kernel on Aarch64.
 *
 * pxefile_addr_r can be pretty much anywhere that doesn't conflict with
 *   something else. Choosing 65M allows for any boot script to be up to 1M,
 *   which is hopefully plenty.
 *
 * ramdisk_addr_r simply shouldn't overlap anything else. Choosing 66M allows
 *   for any PXE configuration file to be up to 1M, which is hopefully plenty.
 */
#ifdef CONFIG_ARM64
# define KERNEL_ADDR_R "0x00080000"
#else
# define KERNEL_ADDR_R "0x03000000"
#endif
#define ENV_MEM_LAYOUT_SETTINGS \
	"fdt_high=" FDT_HIGH "\0" \
	"initrd_high=" INITRD_HIGH "\0" \
	"fdt_addr_r=0x00000100\0" \
	"kernel_addr_r=" KERNEL_ADDR_R "\0" \
	"scriptaddr=0x04000000\0" \
	"pxefile_addr_r=0x04100000\0" \
	"ramdisk_addr_r=0x04200000\0" \

#define BOOT_TARGET_DEVICES(func) \
	func(MMC, mmc, 0) \
	func(USB, usb, 0) \
	func(PXE, pxe, na) \
	func(DHCP, dhcp, na)
#include <config_distro_bootcmd.h>

#define CONFIG_EXTRA_ENV_SETTINGS \
	"dhcpuboot=usb start; dhcp u-boot.uimg; bootm\0" \
	ENV_DEVICE_SETTINGS \
	ENV_MEM_LAYOUT_SETTINGS \
	BOOTENV


#endif
