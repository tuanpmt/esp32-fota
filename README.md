
# ESP32 OTA demo

## BUILD STEP
1. Register http://fota.vn account & clone this app `git clone --recursive https://github.com/tuanpmt/esp32-fota.git`
2. Copy your API Key at: http://fota.vn/me 
3. Build newest firmware (call it is `2.0`)
4. `make menuconfig` and set your WiFi ssid, pass, and Apikey above in `FOTA Configuration`, set `APP_VERSION = 2.0` in `main/fota.c`, then `make`
5. Upload firmware 2.0 `build\fota-app.bin` to http://fota.vn/firmware with `Version will update to = 1.0`, `This file version=2.0`
6. Set `APP_VERSION = 1.0` in `main/fota.c`, then `make flash monitor` to see ESP32 connect to the internet and download 2.0 firmware to replace 1.0 

## REQUIRE
- [esp-request](https://github.com/tuanpmt/esp-request)

## License

This projects are released under the MIT license. Short version: this code is copyrighted to me [@TuanPM](https://twitter.com/tuanpmt), I give you permission to do wantever you want with it except remove my name from the credits. See the LICENSE file or http://opensource.org/licenses/MIT for specific terms.

