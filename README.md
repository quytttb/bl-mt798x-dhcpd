# ATF and u-boot for mt798x with DHCPD

A modified version of hanwckf's U-Boot for MT798x by Yuzhii, with support for DHCPD and a beautiful web UI. (Builds available for versions 2025/SP1/SP2)

Supports GitHub Actions for automatic builds, and can generate both normal and overclocked BL2.

**Warning: Flashing custom bootloaders can brick your device. Proceed with caution and at your own risk.**

## About bl-mt798x

U-Boot 2025 adds more features:

- System info display
- Factory (RF) update
- Backup download
- Flash editor
- Web terminal
- Environment manager
- Theme manager
- I18N support
- Device reboot
- UBI volume management

![Version-2025](document/pictures/uboot-2025.png)

You can configure the features you need.

- [x] MTK_DHCPD
  - [x] MTK_DHCPD_USE_CONFIG_IP
  - MTK_DHCPD_POOL_START_HOST default 100
  - MTK_DHCPD_POOL_SIZE default 101
- [ ] MTK_TELNETD
- Failsafe Web UI style:
  - [x] WEBUI_FAILSAFE_UI_BOOTSTRAP
    - [x] WEBUI_FAILSAFE_I18N
  - [ ] WEBUI_FAILSAFE_UI_GL
  - [ ] WEBUI_FAILSAFE_UI_MTK
- [x] WEBUI_FAILSAFE_ADVANCED - Enable advanced features
  - [ ] WEBUI_FAILSAFE_SIMG - Enable Single Image upgrade
  - [x] WEBUI_FAILSAFE_FACTORY - Enable factory (RF) update
  - [x] WEBUI_FAILSAFE_BACKUP - Enable backup download
  - [x] WEBUI_FAILSAFE_ENV - Enable environment manager
  - [x] WEBUI_FAILSAFE_CONSOLE - Enable web terminal
  - [x] WEBUI_FAILSAFE_FLASH - Enable flash editor
  - [x] WEBUI_FAILSAFE_UBI - Enable UBI volume management

## Prepare

```bash
sudo apt install gcc-aarch64-linux-gnu build-essential flex bison libssl-dev device-tree-compiler qemu-user-static nodejs npm
```

> If you want to build for armv7l devices, you also need to install `gcc-arm-linux-gnueabi`
>
> The failsafe web UI assets are minified at build time. If you build U-Boot manually, run `npm install` once in `uboot-mtk-20250711/failsafe/embedded` so the local minifier dependency is available. It will be installed automatically by the `build.sh` tool.

## Build

Configure once with:

```bash
make menuconfig
```

Then build the current `.config` selection:

```bash
make
```

In `make menuconfig`, you can control whether `make` runs FIP (`build.sh`), ATF (`compile_atf.sh`), and GPT (`generate_gpt.sh`) with:

- `BUILD_FIP`
- `BUILD_ATF`
- `BUILD_GPT`

Build every board with the selected version by using:

```bash
make all
```

For help:

```bash
make help
```

Single model examples:

```bash
# mt7981, emmc device
make BOARD=sn_r1
# mt7981, spi-nand device, nonmbm device, multi-layout support
make BOARD=zbt_z8103ax-c VARIANT=NONMBM
# mt7986, spi-nand device, multi-layout support, single image upgrade support
make BOARD=ruijie_rg-x60-new VERSION=SP1 SIMG=1
```

List available boards for a version:

```bash
make boards VERSION=2025
```

- Version (default: 2025. Optional, for different versions of ATF and U-Boot)

| Version | ATF | UBOOT |
| --- | --- | --- |
| 2025 | 20250711 | 20250711 |
| SP1 | 20241017-bacca82a8 | 20250711 |
| SP2 | 20260123 | 20250711 |

> SP1: For some devices, still use the kernel 5.4 firmware, may cause some issues on version 2025, like hwrng wrong, in this case, you can try SP1.
>
> SP2: With some modifications for better compatibility with new platforms, like mt7987, or newest kernel.

- VARIANT (default: default. Optional, for different firmware variants)

> Normally, `VARIANT` is prepared for MTD devices.

| Variant | Description | Adapted Firmware |
| --- | --- | --- |
| default | Recommend for devices with stock/custom partition layout, enable MTK-NMBM, suitable for most users | stock/custom layout firmware |
| nonmbm | Recommend for devices with stock/custom partition layout, with MTK-NMBM disabled | stock/custom layout firmware without MTK-NMBM |
| ubootmod | With some modifications for better compatibility with OpenWrt/ImmortalWrt firmware | ubootmod layout firmware |
| ubi | Designed for UBI layout(such as: `spi-nand0:1024k(bl2),-(ubi)`) | ubi layout firmware |
| openwrt | From the official OpenWrt repository, it currently has no failsafe web UI | OpenWrt official firmware |

---

Other options:

| Option | type | required | default | description |
| --- | --- | --- | --- | --- |
| SOC | string | false | null | Auto detected, you can set SOC=mt7981, SOC=mt7986 or other mt798x platforms |
| MULTI_LAYOUT | boolean | false | 1 | You can set MULTI_LAYOUT=0 to disable multi-layout support(Only for nand devices) |
| FIXED_MTDPARTS | boolean | false | 1 | You can set FIXED_MTDPARTS=0 to make mtdparts editable, but it may cause some issues if you don't know what you are doing, so it's default to 1 to use fixed mtdparts.(Only for nand devices) |
| FSTHEME | string | false | bootstrap | You can set FSTHEME=bootstrap/gl/mtk to change the failsafe web UI theme, bootstrap/gl/mtk |
| SIMG | boolean | false | null | SIMG=1 means enable single image upgrade support in the failsafe web UI, but it may cause some issues if you don't know what you are doing, so it's default to 0 to disable it. |
| UBIMNG | boolean | false | 0 | UBIMNG=1 enables UBI volume management in the failsafe web UI. Requires MTD device with UBI support. |
| TELNETD | boolean | false | 0 | TELNETD=1 enables the RFC 854 compliant telnet server in failsafe mode. Provides U-Boot CLI access over TCP port 23. |
| CLEAN | boolean | false | null | Pass `--clean` to clean the build environment before build |

> CAN'T ENABLE MULTI_LAYOUT=1 and FIXED_MTDPARTS=0 at the same time

Generated files will be in the `output`

For direct `*.sh` usage details, please see [`doc/tools.md`](./document/tools.md).

## Use Actions to build

You need to fork this repository to your own account, and then you can use Actions to build the binaries. The generated files will be in the `artifacts` or `releases` page.

- [x] Build FIP
  - [x] single-board/all/all-mt798x
  - [x] Version 2025/SP1/SP2/all
  - [ ] VARIANT
  - [ ] Extra Options
  > VERSION:all only for single-board
- [x] Build GPT
  - [x] Official layout
  - [ ] Custom layout
- [x] Build BL2
  - [x] RAMBOOT
  - [ ] OC profiles

> if you want to build old versions(<2025), you can checkout the "old-version" branch
>
> This branch only keeps 2025/SP1/SP2 support.

## Generate GPT with python2.7

> install dependencies

```bash
sudo apt-get install python2 python2-dev
```

> run

```bash
make gpt
```

Generated files will be in the `output_gpt`

> You need to add your device's partition info JSON file in the "mt798x_gpt" directory, e.g. "atf-dir/tools/dev/gpt_editor/example/gpt.json".

When you enable `SDMMC=1` (e.g. `make gpt SDMMC=1`), the generated GPT image will support MTK SDMMC.

### Show GPT info

Create a directory named `mt798x_gpt_bin` in the repository root directory, and put your GPT bin files in it.

Then run:

```bash
make gpt SHOW=1
```

Then it will display the GPT partition info of all GPT bin files in `mt798x_gpt_bin` directory, and output the results to `gpt_info.txt` in the `output_gpt` directory.

### Draw GPT layout

Install `Pillow` library:

```bash
pip3 install Pillow
```

Then run:

```bash
make gpt DRAW=1
```

## Compile ATF

```bash
make atf
```

Then it will generate BL2 in the `output` directory. Normally, it will generate a ramboot BL2.

### Overclocking profiles

Adjusting ARMPLL frequency is a **very dangerous** operation.

**It may cause some issues if you don't know what you are doing, and may cause your device to be bricked!**

So it's default to the stock frequency for safety, but you can enable the OC profiles to adjust the ARMPLL frequency, but please be careful when using it.

- For mt7981, now support OC to 1.4GHz~1.8GHz, and the OC profiles are in the `mt798x_atf/mt7981` directory.

  e.g. to build the 1.6GHz OC BL2 you need configure:

  ```makefile
  MT7981_ARMPLL_FREQ_1600=y
  ```

- For mt7986, now support OC to 2.5GHz, or underclock to 1.6GHz, and the OC profiles are in the `mt798x_atf/mt7986` directory.

  e.g. to build the 2.3GHz OC BL2 you need configure:

  ```makefile
  MT7986_ARMPLL_FREQ_2300=y
  ```

> Limit each adjustment to 100MHz for mt798x, and limit each adjustment to 50MHz for mt762x. It is recommended to adjust the frequency step by step, e.g. from 1.6GHz to 1.7GHz, then to 1.8GHz.

ARMPLL frequency range adjustment support for different platforms:

| Version | mt7622 | mt7629 | mt7981 | mt7986 | mt7987 | mt7988 |
| --- | --- | --- | --- | --- | --- | --- |
| TF-A 2024 | No | No | 1.3GHz~1.8GHz | 1.6GHz~2.5GHz | N/A | No |
| TF-A 2025 | 1.35GHz~1.7GHz | 1.2GHz~1.5GHz | 1.3GHz~1.8GHz | 1.6GHz~2.5GHz | No | No |
| TF-A 2026 | No | No | No | No | No | No |

### Other Options

These options only work for the `normal` directory.

| Option | type | required | default | description |
| --- | --- | --- | --- | --- |
| VARIANT | string | false | null | You can set VARIANT=NONMBM/UBOOTMOD to build different BL2 variants, NONMBM means build BL2 with MTK-NMBM disabled, UBOOTMOD means build BL2 with some modifications for better compatibility with OpenWrt/ImmortalWrt firmware, but it may cause some issues if you don't know what you are doing, so it's default to null to use the default BL2 variant. |
| OC7981 | int | false | null | You can set OC7981=13-18 to build BL2 with different OC profiles for mt7981, FREQ=OC7981*100MHz, e.g. OC7981=16 means 1.6GHz, but it may cause some issues if you don't know what you are doing, so it's default to null to use the default OC profile. |
| OC7986 | int | false | null | You can set OC7986=16-25 to build BL2 with different OC profiles for mt7986, FREQ=OC7986*100MHz, e.g. OC7986=23 means 2.3GHz, but it may cause some issues if you don't know what you are doing, so it's default to null to use the default OC profile. |

---

## FIT support

**You MUST test it yourself, and there is a risk of BRICKING your device!**

There are two ways to build:

- Local Build

  ```bash
  make BOARD=your_board VERSION=2025 VARIANT=ubootmod
  ```

- Use Action to build

How to flash:

1. Use failsafe WEB UI to back up [1*](#endnote) **all your flash and partitions**; this is very **important**!

2. Update BL2 in the WEB UI to flash the preloader provided by OpenWrt/ImmortalWrt ubootmod firmware.

3. Update U-Boot in the WEB UI to flash the **FIT version FIP**.

4. Use Flash Editor in the WEB UI to erase the UBI partition (or use the command line: `mtd erase ubi`); this step is only for NAND devices.

5. Try upgrade in firmware upgrade page with the OpenWrt/ImmortalWrt ubootmod firmware[2*](#endnote) [3*](#endnote), if not work, try next step.

6. Use failsafe WEB UI Initramfs to boot the OpenWrt/ImmortalWrt ubootmod Initramfs image.

7. If the device can boot into OpenWrt/ImmortalWrt successfully, then you can try upgrade in firmware upgrade page with the OpenWrt/ImmortalWrt ubootmod firmware again.

---

## The best practices

1. Use TTL tools to connect to the serial port, and use [MTK UARTBOOT](https://github.com/981213/mtk_uartboot/releases)/[MTK-LAUNCHPAD](https://github.com/Yuzhii0718/mtk-launchpad) to ramboot

2. In the Web UI, back up all your flash and partitions [1*](#endnote); this is very important!

3. Update U-Boot in the WEB UI and upgrade firmware

4. Restore the backup if something goes wrong

### Change failsafe WEB UI start key

Default set `glbtn_key=reset,wps` and `glbtn_gpio=gpio 1,gpio 0`
(Reset GPIO1 + WPS GPIO0, ACTIVE_LOW). The glbtn command checks those
GPIOs / button labels; hold any configured key for 4 seconds to start httpd.

The following priorities are now supported:

- `glbtn_gpio=<gpio>`
  → Directly read the GPIO.
- `glbtn_key=<label>`
  → Still search by label.

e.g.

- Specify only GPIO:
  `setenv glbtn_gpio 0`
- With the `gpio:` prefix:
  `setenv glbtn_gpio gpio:0`
  > 0, gpio 0, pio 0, gpio:0, pio0.
- Flip the signal:
  `setenv glbtn_gpio !0`
  > !gpio 0, !pio 0, !gpio:0, !pio0.
- Scan gpio-keys:
  `setenv glbtn_key wps`
  > wps, reset, mesh...

> Then you need saveenv and reboot to apply.

### Change MTD partition layout manually

Only for multi-layout devices

Set mtdparts environment variable to the partition layout you want to use, and reboot to apply.

```bash
# Current method
setenv mtd_layout <label>
# legacy method
setenv mtd_layout_label <label>
```

> Then you need saveenv and reboot to apply.

### Disable auto-reboot after upgrade

Set failsafe_auto_reboot environment variable to 1/true/yes/on to enable auto reboot after upgrade(New WEB UI).

### Some commands in firmware

```bash
fw_setenv env_invalid 1 # Reset environment to default values in next boot
fw_setenv failsafe 1 # Reboot to failsafe mode in next boot
```

> You need to install `uboot-envtools` and configure `package/boot/uboot-envtools/files/mediatek_filogic` correctly for your device before compiling firmware; otherwise, the environment variables will not work.

### Telnet support

You can connect to the device with telnet, default port is 23, and you can set the `telnet_port` environment variable to change the port.

TelnetD is enabled by default, but you can set the `telnetd_enable` environment variable to 0/false/no/off to disable it.

### Unified env-controlled NMBM enablement(Only for MTD devices)

You can set `nmbm_enable` environment variable to 0/false/no/off to disable MTK-NMBM.

> Only for MTD devices which enable MTK-NMBM configs before compile.

More information about the NMBM enablement can be found in the [unified env-controlled NMBM enablement](./document/unified-env-controlled-NMBM-enablement.md) documentation.

---

## Endnote

1*: If your device is a MMC device, back up all flash is not feasible. It depends on the size of the firmware, which is usually 200MB to 300MB.

2*: If your device is a MMC device, you need upgrade GPT table which has production partition, then you needn't use ubootmod firmware, you can use the OpenWrt official firmware directly.

3*: The OpenWrt/ImmortalWrt ubootmod firmware is a special firmware with FIT support, in this firmware, devicetree is loaded from the FIT image(bootargs = "root=/dev/fit0 rootwait"), and loaded from ubi_rootdisk. You'd better use a version after OpenWrt/ImmortalWrt 24.10.

---

## Old Version ( < U-Boot 2025 )

Current branch only supports **2025/SP1/SP2**.

**You can find old versions (such as 2022/2023/2024) in the "old-version" branch, but they may have some issues, so it is recommended to use the current branch for a better experience.**

- <https://cmi.hanwckf.top/p/mt798x-uboot-usage>

---

## MTMIPS

**It only for development and testing, not recommended for production use.**

```bash
chmod +x mtmips.sh
SOC=<mt7620|mt7621|mt7628|mt7688> BOARD=<board_name> ./mtmips.sh
```

but it not preferred, because the mt7621 u-boot has some issues on uboot-mtk-20250711.

It may cause some issues if you don't know what you are doing, so it's recommended to use the [uboot-mt7621-dhcpd](https://github.com/Yuzhii0718/uboot-mt7621-dhcpd) project for mt7621 devices, which is more stable and has better support for mt7621 devices.

---

## Acknowledgement

- [u-boot](https://github.com/u-boot/u-boot)
- [mtk-openwrt](https://github.com/mtk-openwrt)
- [hanwckf](https://github.com/hanwckf/bl-mt798x)
- [Tianling](https://blog.imouto.in/)
