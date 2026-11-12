# W5500_IDF4

W5500 SPI Ethernet for WLED 16.x builds on arduino-esp32 2.0.18 (Tasmota platform 2024.06.00, ESP-IDF 4.4.8), used by `env:slwf-09u` (A1-SLWF-09U, ESP32-S3).

Why: the `ETH` class of arduino-esp32 2.0.x knows only the internal EMAC of the classic ESP32, and the prebuilt IDF libraries of the core contain no SPI Ethernet driver (`libesp_eth.a` has no `esp_eth_mac_w5500`). Arduino 3.x / IDF 5 (WLED 17+) has W5500 in `ETH.begin()`; this library is not needed there.

- `src/esp_eth_mac_w5500.c`, `src/esp_eth_phy_w5500.c`, `src/w5500.h`: ESP-IDF v4.4.8 `components/esp_eth/src/`, unchanged (Apache-2.0), wrapped in `#if CONFIG_ETH_SPI_ETHERNET_W5500 && ESP_IDF_VERSION_MAJOR == 4`. The `esp_eth` headers of the core are identical to v4.4.8, so the driver matches the prebuilt `esp_eth` library.
- `w5500EthBegin()` (`include/w5500_idf4.h`): SPI bus + device, hardware reset of its own (the IDF 4.4 driver releases RST and writes the MAC setup before the W5500 is up: VERSIONR reads 0, the setup is lost, no frame goes out or in; here RST is held 1 ms and VERSIONR polled for 0x04 as IDF 5 does), W5500 MAC / PHY, MAC address = `ESP_MAC_ETH` from eFuse, netif `ETH_DEF` attached and registered with WiFiGeneric like `ETHClass::begin()` does. `ETH.localIP()`, `config()`, `setHostname()`, `linkUp()` and the `ARDUINO_EVENT_ETH_*` events work as with the EMAC; `ETH.macAddress()` does not (`w5500EthMacAddress()`).

Enable with `-D CONFIG_ETH_SPI_ETHERNET_W5500=1` (declares the W5500 parts of `esp_eth_mac.h` / `esp_eth_phy.h` and compiles the driver); without it the library is empty.
