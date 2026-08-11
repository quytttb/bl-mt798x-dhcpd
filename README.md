# U-Boot DHCPD cho Keenetic NR3053

Nhánh `keenetic-nr3053-mod` chứa bootloader cho **Keenetic NR3053** (MediaTek MT7981), dựa trên `bl-mt798x-dhcpd` và có các điều chỉnh dành riêng cho luồng boot của KeeneticOS.

Đây không phải tree ImmortalWrt. Các board Viettel `viettel_*` và boot flow ImmortalWrt được duy trì ở nhánh `main` của fork.

> **Cảnh báo:** Nạp BL2, FIP hoặc ghi trực tiếp NAND sai địa chỉ có thể brick thiết bị. Luôn sao lưu đầy đủ flash, đặc biệt là phân vùng `Factory`, trước khi thay đổi bootloader.

## Phạm vi hỗ trợ

| Hạng mục | Giá trị |
| --- | --- |
| Board | Keenetic NR3053 |
| SoC | MediaTek MT7981 |
| Phiên bản build | `SP1` — TF-A `20241017-bacca82a8`, U-Boot `20250711` |
| Storage | SPI-NAND với NMBM |
| Giao diện failsafe | Bootstrap, i18n, web terminal, backup, flash editor và U-Config |

Các thay đổi riêng của nhánh gồm DTS/defconfig NR3053, provision U-Config, giao diện Keenetic, logo, i18n và kiểu dáng web UI.

## Layout flash và cách boot

Defconfig của board sử dụng layout NMBM sau:

```text
nmbm0:1024k(bl2),1024k(u-boot-env),2048k(Factory),2048k(fip),-(ubi)
```

Boot command mặc định đọc ảnh FIT của KeeneticOS trực tiếp từ đầu phân vùng `ubi`:

```text
mtd read ubi 0x46000000 0x0 0x400000; bootm 0x46000000#config@1
```

Không xóa hoặc ghi đè `Factory`; phân vùng này chứa dữ liệu hiệu chuẩn quan trọng. Luồng nâng cấp firmware Keenetic dùng ảnh FIT thô trong phân vùng `ubi`, không phải UBI volume thông thường.

## Chuẩn bị môi trường build

Trên Ubuntu/Debian:

```bash
sudo apt update
sudo apt install $(cat depends/ubuntu-22.04)
```

Hoặc cài trực tiếp các phụ thuộc chính:

```bash
sudo apt install gcc-aarch64-linux-gnu build-essential flex bison libssl-dev device-tree-compiler qemu-user-static nodejs npm
```

Tài nguyên của web UI được minify khi build. `build.sh` tự cài dependency Node.js khi cần; khi làm thủ công có thể chạy `npm install` trong `uboot-mtk-20250711/failsafe/embedded`.

## Build NR3053

Build FIP và BL2 mặc định:

```bash
make BOARD=keenetic_nr3053 VERSION=SP1 VARIANT=default BUILD_LOG=y
```

Kết quả nằm trong `output/`:

- `fip-mt7981_keenetic_nr3053_*.bin`: FIP chứa BL31 và U-Boot.
- `bl2-mt7981_keenetic_nr3053_*.img`: BL2/preloader, nếu board cho phép xuất BL2.
- `build-keenetic_nr3053-SP1-default.log`: log build khi dùng `BUILD_LOG=y`.

Tên file có MD5 để nhận diện build. Nên tự tạo SHA-256 trước khi nạp:

```bash
(cd output && sha256sum * > sha256sums)
```

Kiểm tra board khả dụng:

```bash
make boards VERSION=SP1
```

Xem các tùy chọn build:

```bash
make help
```

> Không đổi sang `VERSION=2025` hoặc `SP2` cho NR3053 nếu chưa bổ sung và kiểm tra đầy đủ defconfig TF-A tương ứng. Board này hiện được duy trì với `SP1`.

## Giao diện failsafe và U-Config

Web UI failsafe của NR3053 cung cấp:

- Sao lưu flash, flash editor và web terminal.
- Quản lý trạng thái/cấp phát U-Config của Keenetic qua trang riêng.
- Nút failsafe, DHCP/DNS và các thiết lập mạng cơ bản.
- Giao diện, logo và bản dịch riêng cho Keenetic.

Trình sửa environment chung bị tắt có chủ đích: biến environment U-Boot không cấu hình KeeneticOS/U-Config. Tương tự, upload Factory qua web UI bị tắt để tránh ghi nhầm dữ liệu hiệu chuẩn; chỉ khôi phục Factory bằng quy trình đã xác minh qua Flash Editor hoặc UART.

Không chia sẻ, commit hoặc công bố dữ liệu U-Config, MAC address, serial hay nội dung Factory từ thiết bị thật.

## Nạp và khôi phục

1. Kết nối UART và xác minh khả năng boot/recovery trước khi ghi flash.
2. Sao lưu toàn bộ flash và `Factory`; lưu bản sao ở nơi an toàn.
3. Chỉ nạp file FIP/BL2 dành cho `keenetic_nr3053` và kiểm tra checksum.
4. Sau khi nạp, boot thử qua UART và kiểm tra KeeneticOS, mạng, nút failsafe cùng trang U-Config.
5. Nếu có lỗi, khôi phục từ backup hoặc dùng quy trình UART recovery; không dùng ảnh của board khác.

## Đồng bộ upstream

Nhánh này được đồng bộ từ `upstream/master` theo merge, không rebase public history. Khi cập nhật upstream, giữ các thành phần riêng của Keenetic:

- `mt7981-keenetic_nr3053.dts` và các defconfig `keenetic_nr3053`.
- `nr3053_uconfig.c`, module U-Config và Kconfig liên quan.
- Asset/UI Keenetic: i18n, logo, `style.css`, `main.js`, `uconfig.html` và `uconfig_js.js`.
- Các thay đổi bootmenu, raw FIT và provisioning của NR3053.

Các file thường cần review thủ công khi merge gồm `build.sh`, `failsafe/Kconfig`, `failsafe_core.c`, `settings.html` và `i18n.js`, vì upstream cũng thay đổi chúng. Ưu tiên ghép cả tính năng upstream lẫn mod/UI Keenetic, không chọn một phía để ghi đè toàn bộ.

## Ghi công

- [u-boot](https://github.com/u-boot/u-boot)
- [mtk-openwrt](https://github.com/mtk-openwrt)
- [hanwckf](https://github.com/hanwckf/bl-mt798x)
- [Yuzhii0718/bl-mt798x-dhcpd](https://github.com/Yuzhii0718/bl-mt798x-dhcpd)
