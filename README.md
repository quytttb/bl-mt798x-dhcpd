# ATF và U-Boot MT798x có DHCPD

Phiên bản U-Boot MT798x của hanwckf được Yuzhii chỉnh sửa, có DHCPD và giao diện web. Repo hỗ trợ các phiên bản `2025`/`SP1`/`SP2`, GitHub Actions để build tự động, BL2 thường và BL2 ép xung.

> **Cảnh báo:** Nạp bootloader tùy biến có thể làm hỏng thiết bị. Hãy tự chịu trách nhiệm và thực hiện thận trọng.

## Giới thiệu

U-Boot 2025 bổ sung các tính năng:

- Hiển thị thông tin hệ thống, cập nhật Factory (RF) và tải bản sao lưu.
- Flash editor, web terminal và trình quản lý môi trường.
- Quản lý theme, i18n, khởi động lại thiết bị và quản lý volume UBI.

![Version-2025](document/pictures/uboot-2025.png)

Có thể bật/tắt các tính năng sau trong cấu hình:

- [x] `MTK_DHCPD`
  - [x] `MTK_DHCPD_USE_CONFIG_IP`
  - `MTK_DHCPD_POOL_START_HOST` mặc định là `100`
  - `MTK_DHCPD_POOL_SIZE` mặc định là `101`
- [ ] `MTK_TELNETD`
- Giao diện web failsafe:
  - [x] `WEBUI_FAILSAFE_UI_BOOTSTRAP`
    - [x] `WEBUI_FAILSAFE_I18N`
  - [ ] `WEBUI_FAILSAFE_UI_GL`
  - [ ] `WEBUI_FAILSAFE_UI_MTK`
- [x] `WEBUI_FAILSAFE_ADVANCED` — bật tính năng nâng cao
  - [ ] `WEBUI_FAILSAFE_SIMG` — nâng cấp bằng single image
  - [x] `WEBUI_FAILSAFE_FACTORY` — cập nhật Factory (RF)
  - [x] `WEBUI_FAILSAFE_BACKUP` — tải bản sao lưu
  - [x] `WEBUI_FAILSAFE_ENV` — quản lý environment
  - [x] `WEBUI_FAILSAFE_CONSOLE` — web terminal
  - [x] `WEBUI_FAILSAFE_FLASH` — flash editor
  - [x] `WEBUI_FAILSAFE_UBI` — quản lý volume UBI

## Chuẩn bị môi trường

```bash
sudo apt install gcc-aarch64-linux-gnu build-essential flex bison libssl-dev device-tree-compiler qemu-user-static nodejs npm
```

> Để build thiết bị `armv7l`, cài thêm `gcc-arm-linux-gnueabi`.
>
> Tài nguyên giao diện web failsafe được minify khi build. Nếu build U-Boot thủ công, chạy `npm install` một lần trong `uboot-mtk-20250711/failsafe/embedded`; công cụ `build.sh` sẽ tự thực hiện khi cần.

## Build

Thiết lập cấu hình một lần:

```bash
make menuconfig
```

Sau đó build cấu hình `.config` hiện tại:

```bash
make
```

Trong `make menuconfig`, chọn các thành phần mà `make` cần chạy:

- `BUILD_FIP` (`build.sh`)
- `BUILD_ATF` (`compile_atf.sh`)
- `BUILD_GPT` (`generate_gpt.sh`)

Build mọi board của phiên bản đã chọn:

```bash
make all
```

Xem trợ giúp:

```bash
make help
```

Ví dụ build một model:

```bash
# mt7981, thiết bị eMMC
make BOARD=sn_r1
# mt7981, SPI-NAND, không dùng NMBM, hỗ trợ nhiều layout
make BOARD=zbt_z8103ax-c VARIANT=NONMBM
# mt7986, SPI-NAND, nhiều layout, hỗ trợ nâng cấp single image
make BOARD=ruijie_rg-x60-new VERSION=SP1 SIMG=1
```

### Board Viettel MT7981

Fork này hỗ trợ các board sau cho luồng boot ImmortalWrt:

```bash
# Viettel VHT-32X6V1
make BOARD=viettel_32x6 VERSION=SP2

# Viettel SDMC NR3053
make BOARD=viettel_nr3053 VERSION=SP2
```

Cả hai board dùng SPI-NAND và giữ lại phân vùng `Factory` 2 MiB. Các bản build Viettel dùng `SP2`, tương ứng TF-A `20260123` và U-Boot `20250711`. Trước khi dùng bootloader tùy biến, hãy sao lưu toàn bộ flash và phân vùng Factory, kiểm tra checksum ảnh tạo ra và chỉ dùng ảnh đúng model. Vị trí FIP tùy theo board: VHT-32X6V1 dùng `0x380000`, NR3053 dùng `0x400000`.

Liệt kê board khả dụng của một phiên bản:

```bash
make boards VERSION=2025
```

### Phiên bản và biến thể

`VERSION` mặc định là `2025`; dùng để chọn phiên bản TF-A và U-Boot.

| Phiên bản | TF-A | U-Boot |
| --- | --- | --- |
| `2025` | `20250711` | `20250711` |
| `SP1` | `20241017-bacca82a8` | `20250711` |
| `SP2` | `20260123` | `20250711` |

> `SP1` phù hợp với một số thiết bị vẫn dùng firmware kernel 5.4; nếu bản 2025 gặp lỗi như hwrng, hãy thử `SP1`.
>
> `SP2` có thay đổi tăng khả năng tương thích với nền tảng mới hơn, như mt7987 hoặc kernel mới nhất.

`VARIANT` mặc định là `default`; biến thể thường áp dụng cho thiết bị MTD.

| Biến thể | Mô tả | Firmware tương thích |
| --- | --- | --- |
| `default` | Khuyến nghị cho layout gốc/tùy biến; bật MTK-NMBM, phù hợp đa số người dùng | Layout gốc/tùy biến |
| `nonmbm` | Layout gốc/tùy biến nhưng tắt MTK-NMBM | Firmware không dùng MTK-NMBM |
| `ubootmod` | Điều chỉnh để tương thích tốt hơn với OpenWrt/ImmortalWrt | Layout ubootmod |
| `ubi` | Dành cho layout UBI, ví dụ `spi-nand0:1024k(bl2),-(ubi)` | Layout UBI |
| `openwrt` | Từ repo OpenWrt chính thức, hiện không có web UI failsafe | Firmware OpenWrt chính thức |

Các tùy chọn khác:

| Tùy chọn | Kiểu | Bắt buộc | Mặc định | Mô tả |
| --- | --- | --- | --- | --- |
| `SOC` | chuỗi | không | null | Tự nhận diện; có thể đặt `mt7981`, `mt7986` hoặc nền tảng MT798x khác |
| `MULTI_LAYOUT` | boolean | không | 1 | Đặt `0` để tắt hỗ trợ nhiều layout (chỉ NAND) |
| `FIXED_MTDPARTS` | boolean | không | 1 | Đặt `0` để cho phép sửa `mtdparts`; chỉ dùng khi hiểu rõ tác động (chỉ NAND) |
| `FSTHEME` | chuỗi | không | `bootstrap` | Theme failsafe: `bootstrap`, `gl` hoặc `mtk` |
| `SIMG` | boolean | không | null | `1` bật đóng gói single image; mặc định tắt vì có thể phát sinh rủi ro |
| `UBIMNG` | boolean | không | 0 | `1` bật quản lý volume UBI; yêu cầu thiết bị MTD có UBI |
| `TELNETD` | boolean | không | 0 | `1` bật telnet server RFC 854 ở failsafe, cổng 23 |
| `CLEAN` | boolean | không | null | Truyền `--clean` để dọn môi trường build |

> Không thể đồng thời dùng `MULTI_LAYOUT=1` và `FIXED_MTDPARTS=0`.

File tạo ra nằm trong thư mục `output`. Tên file được rút gọn theo dạng
`fip-<soc>-<board>-<version>[-variant][-fixed|-multi][-xz].bin` và
`bl2-<soc>-<board>-<version>[-variant].img`; không chứa tên tác giả hoặc MD5.
Kiểm tra tính toàn vẹn bằng file `sha256sums` do workflow tạo ra, hoặc tự tạo
bằng `cd output && sha256sum * > sha256sums`. Xem chi tiết cách dùng trực tiếp
các script tại [document/tools.md](./document/tools.md).

## Build bằng GitHub Actions

Cần fork repo về tài khoản của bạn trước khi dùng GitHub Actions.

- **Dev Build** tự chạy sau khi push thay đổi liên quan bootloader lên `main`, hoặc có thể chạy thủ công trong tab Actions. Workflow build FIP và BL2 `SP2` (TF-A 20260123) cho Viettel VHT-32X6V1 và SDMC NR3053, lưu artifact trong bảy ngày và không tạo GitHub Release.
- **Build Release** chạy khi push tag dạng `vMAJOR.MINOR.PATCH`, hoặc chạy thủ công cho tag đã tồn tại. Workflow checkout chính xác tag đó, build hai board, tạo `sha256sums` và đính kèm toàn bộ file vào GitHub Release tương ứng.

Ví dụ tạo release từ commit hiện tại:

```bash
git tag -a v1.0.0 -m "MT7981 Viettel 1.0.0"
git push origin v1.0.0
```

Các workflow **FIP Build**, **GPT Build** và **BL2 Build** vẫn dành cho build thủ công có thể tùy biến với các board khác. File tạo ra có ở artifact hoặc release của Actions.

### Đồng bộ upstream

Workflow **Sync Upstream** fetch từ
[`Yuzhii0718/bl-mt798x-dhcpd:master`](https://github.com/Yuzhii0718/bl-mt798x-dhcpd/tree/master)
mỗi thứ Hai lúc 10:17 giờ Việt Nam, hoặc có thể chạy thủ công từ tab
**Actions**. Khi có commit mới, workflow merge vào nhánh
`automation/sync-upstream` và tạo/cập nhật pull request về `main`; workflow
không tự merge.

Workflow build/release, README và các thay đổi failsafe/build riêng được khai
báo `merge=ours` trong `.gitattributes`, nên luôn giữ phiên bản của fork nếu
upstream thay đổi cùng file. Các DTS và defconfig Viettel đã có ở upstream nên
không bị khóa, để fork tự nhận các cập nhật board mới. Vẫn cần review pull
request, đặc biệt với thay đổi `build.sh`, cấu hình chung hoặc mã failsafe có
liên quan. Nếu merge conflict ở file không được bảo vệ, hãy giải quyết trên
nhánh sync rồi push lại.

- [x] Build FIP
  - [x] Một board / tất cả board / tất cả board MT798x
  - [x] Phiên bản `2025`/`SP1`/`SP2`/`all`
  - [ ] `VARIANT`
  - [ ] Tùy chọn bổ sung
  > `VERSION=all` chỉ dùng cho build một board.
- [x] Build GPT
  - [x] Layout chính thức
  - [ ] Layout tùy biến
- [x] Build BL2
  - [x] RAMBOOT
  - [ ] Profile ép xung

> Muốn build phiên bản cũ hơn 2025, hãy checkout nhánh `old-version`. Nhánh hiện tại chỉ giữ hỗ trợ `2025`/`SP1`/`SP2`.

## Tạo GPT bằng Python 2.7

Cài phụ thuộc:

```bash
sudo apt-get install python2 python2-dev
```

Chạy:

```bash
make gpt
```

Kết quả nằm trong `output_gpt`. Cần thêm JSON thông tin phân vùng của thiết bị vào `mt798x_gpt`, ví dụ `atf-dir/tools/dev/gpt_editor/example/gpt.json`.

Khi bật `SDMMC=1` (ví dụ `make gpt SDMMC=1`), ảnh GPT tạo ra hỗ trợ MTK SDMMC.

### Xem thông tin GPT

Tạo thư mục `mt798x_gpt_bin` ở gốc repo và chép các file GPT bin vào đó, sau đó chạy:

```bash
make gpt SHOW=1
```

Thông tin tất cả GPT bin sẽ được hiển thị và ghi vào `output_gpt/gpt_info.txt`.

### Vẽ layout GPT

Cài Pillow:

```bash
pip3 install Pillow
```

Sau đó chạy:

```bash
make gpt DRAW=1
```

## Biên dịch ATF

```bash
make atf
```

Lệnh tạo BL2 trong thư mục `output`; thông thường đây là BL2 ramboot.

### Profile ép xung

Điều chỉnh tần số ARMPLL **rất nguy hiểm** và có thể làm thiết bị hoạt động không ổn định hoặc hỏng. Mặc định dùng tần số gốc; hãy chỉ bật profile ép xung khi hiểu rõ rủi ro.

- mt7981 hỗ trợ 1.4–1.8 GHz; profile ở `mt798x_atf/mt7981`. Ví dụ build BL2 1.6 GHz:

  ```makefile
  MT7981_ARMPLL_FREQ_1600=y
  ```

- mt7986 hỗ trợ ép xung tới 2.5 GHz hoặc hạ xuống 1.6 GHz; profile ở `mt798x_atf/mt7986`. Ví dụ build BL2 2.3 GHz:

  ```makefile
  MT7986_ARMPLL_FREQ_2300=y
  ```

> Mỗi lần chỉ nên điều chỉnh 100 MHz cho mt798x và 50 MHz cho mt762x; tăng từng bước, ví dụ 1.6 → 1.7 → 1.8 GHz.

| Phiên bản TF-A | mt7622 | mt7629 | mt7981 | mt7986 | mt7987 | mt7988 |
| --- | --- | --- | --- | --- | --- |
| 2024 | Không | Không | 1.3–1.8 GHz | 1.6–2.5 GHz | Không áp dụng | Không |
| 2025 | 1.35–1.7 GHz | 1.2–1.5 GHz | 1.3–1.8 GHz | 1.6–2.5 GHz | Không | Không |
| 2026 | Không | Không | Không | Không | Không | Không |

### Tùy chọn khác cho BL2

Các tùy chọn này chỉ hoạt động với thư mục `normal`.

| Tùy chọn | Kiểu | Bắt buộc | Mặc định | Mô tả |
| --- | --- | --- | --- | --- |
| `VARIANT` | chuỗi | không | null | `NONMBM` tắt MTK-NMBM; `UBOOTMOD` tăng tương thích OpenWrt/ImmortalWrt. Chỉ dùng khi hiểu rõ rủi ro. |
| `OC7981` | số nguyên | không | null | Đặt `13`–`18` để chọn profile mt7981; tần số = giá trị × 100 MHz, ví dụ `16` là 1.6 GHz. |
| `OC7986` | số nguyên | không | null | Đặt `16`–`25` để chọn profile mt7986; ví dụ `23` là 2.3 GHz. |

---

## Hỗ trợ FIT

> **Bắt buộc tự kiểm tra trước khi dùng: có nguy cơ brick thiết bị.**

Có hai cách build:

- Build cục bộ:

  ```bash
  make BOARD=your_board VERSION=2025 VARIANT=ubootmod
  ```

- Chạy workflow Action.

Cách nạp:

1. Trong web UI failsafe, sao lưu [toàn bộ flash và phân vùng](#ghi-chú) — bước này rất quan trọng.
2. Cập nhật BL2 trong web UI bằng preloader từ firmware OpenWrt/ImmortalWrt ubootmod.
3. Cập nhật U-Boot trong web UI bằng FIP bản FIT.
4. Với NAND, xóa phân vùng UBI bằng Flash Editor hoặc `mtd erase ubi`.
5. Thử nâng cấp bằng firmware OpenWrt/ImmortalWrt ubootmod. Nếu không được, chuyển sang bước kế tiếp.
6. Dùng web UI failsafe để boot ảnh initramfs OpenWrt/ImmortalWrt ubootmod.
7. Khi thiết bị boot được OpenWrt/ImmortalWrt, thử nâng cấp firmware ubootmod lần nữa.

## Thực hành an toàn

1. Dùng TTL để kết nối serial và [MTK UARTBOOT](https://github.com/981213/mtk_uartboot/releases) hoặc [MTK-LAUNCHPAD](https://github.com/Yuzhii0718/mtk-launchpad) để ramboot.
2. Trong web UI tại `http://failsafe.lan`, sao lưu toàn bộ flash và phân vùng.
3. Cập nhật U-Boot rồi nâng cấp firmware.
4. Khôi phục backup nếu có sự cố.

### Đổi phím khởi động web UI failsafe

Mặc định `glbtn_key=reset,wps,mesh`: lệnh `glbtn` tìm GPIO có label theo thứ tự `reset`, `wps`, `mesh` và dùng GPIO đầu tiên tìm được.

Ưu tiên hỗ trợ:

- `glbtn_gpio=<gpio>` — đọc trực tiếp GPIO.
- `glbtn_key=<label>` — tìm theo label.

Ví dụ:

- Chỉ định GPIO: `setenv glbtn_gpio 0`
- Có tiền tố `gpio:`: `setenv glbtn_gpio gpio:0` (`0`, `gpio 0`, `pio 0`, `gpio:0`, `pio0` đều hợp lệ)
- Đảo tín hiệu: `setenv glbtn_gpio !0` (`!gpio 0`, `!pio 0`, `!gpio:0`, `!pio0` cũng hợp lệ)
- Quét `gpio-keys`: `setenv glbtn_key wps` (các label như `wps`, `reset`, `mesh`)

> Chạy `saveenv` rồi khởi động lại để áp dụng.

### Đổi layout phân vùng MTD thủ công

Chỉ áp dụng cho thiết bị có nhiều layout. Đặt biến môi trường `mtdparts` thành layout cần dùng rồi khởi động lại:

```bash
# Cách hiện tại
setenv mtd_layout <label>
# Cách cũ
setenv mtd_layout_label <label>
```

> Chạy `saveenv` rồi khởi động lại để áp dụng.

### Tắt tự khởi động lại sau nâng cấp

Đặt biến `failsafe_auto_reboot` thành `1`, `true`, `yes` hoặc `on` để bật tự khởi động lại sau nâng cấp (web UI mới).

### Một số lệnh trong firmware

```bash
fw_setenv env_invalid 1 # Đặt lại environment về mặc định ở lần boot sau
fw_setenv failsafe 1    # Boot vào chế độ failsafe ở lần khởi động sau
```

> Cần cài `uboot-envtools` và cấu hình đúng `package/boot/uboot-envtools/files/mediatek_filogic` cho thiết bị trước khi biên dịch firmware; nếu không, các biến environment sẽ không hoạt động.

### Hỗ trợ Telnet

Kết nối telnet tới thiết bị qua cổng mặc định `23`; có thể đặt biến `telnet_port` để đổi cổng. TelnetD được bật mặc định, có thể đặt `telnetd_enable` thành `0`, `false`, `no` hoặc `off` để tắt.

### Bật/tắt NMBM bằng environment (chỉ thiết bị MTD)

Đặt biến `nmbm_enable` thành `0`, `false`, `no` hoặc `off` để tắt MTK-NMBM.

> Chỉ áp dụng với thiết bị MTD đã bật cấu hình MTK-NMBM từ trước khi build.

Xem thêm tại [hướng dẫn unified env-controlled NMBM enablement](./document/unified-env-controlled-NMBM-enablement.md).

## Ghi chú

1. Với thiết bị MMC, việc sao lưu toàn bộ flash có thể không khả thi vì firmware thường có dung lượng 200–300 MB.
2. Với MMC, cần nâng cấp bảng GPT có phân vùng production; sau đó không cần firmware ubootmod mà có thể dùng trực tiếp firmware OpenWrt chính thức.
3. Firmware OpenWrt/ImmortalWrt ubootmod là firmware đặc biệt hỗ trợ FIT: devicetree được nạp từ FIT (`bootargs = "root=/dev/fit0 rootwait"`) và từ `ubi_rootdisk`. Nên dùng OpenWrt/ImmortalWrt từ bản 24.10 trở lên.

## Phiên bản cũ (trước U-Boot 2025)

Nhánh hiện tại chỉ hỗ trợ **2025/SP1/SP2**. Phiên bản cũ như 2022/2023/2024 nằm ở nhánh `old-version`, nhưng có thể có lỗi; nên dùng nhánh hiện tại để có trải nghiệm tốt hơn.

- <https://cmi.hanwckf.top/p/mt798x-uboot-usage>

## MTMIPS

> Chỉ dùng cho phát triển và thử nghiệm, không khuyến nghị dùng trong môi trường production.

```bash
chmod +x mtmips.sh
SOC=<mt7620|mt7621|mt7628|mt7688> BOARD=<board_name> ./mtmips.sh
```

Không ưu tiên cách này vì U-Boot mt7621 trên `uboot-mtk-20250711` còn một số vấn đề. Khi dùng mt7621, nên chọn dự án [uboot-mt7621-dhcpd](https://github.com/Yuzhii0718/uboot-mt7621-dhcpd) ổn định và hỗ trợ tốt hơn.

## Ghi công

- [u-boot](https://github.com/u-boot/u-boot)
- [mtk-openwrt](https://github.com/mtk-openwrt)
- [hanwckf](https://github.com/hanwckf/bl-mt798x)
- [Tianling](https://blog.imouto.in/)
