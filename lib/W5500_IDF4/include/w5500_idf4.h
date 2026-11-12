#pragma once
// W5500 SPI Ethernet for arduino-esp32 2.0.x (ESP-IDF 4.4).
//
// The Arduino ETH class of core 2.0.x only knows the internal EMAC (classic ESP32), and the
// prebuilt IDF libraries contain no SPI Ethernet driver. w5500EthBegin() brings the W5500 up
// with the IDF 4.4.8 driver compiled in this library and attaches it as the default Ethernet
// netif ("ETH_DEF"), the one ETH reads: ETH.localIP() / config() / setHostname() / linkUp()
// work unchanged and the events arrive as ARDUINO_EVENT_ETH_*. ETH.macAddress() does not (it
// asks a driver handle ETH never had): use w5500EthMacAddress().
//
// Compiled only with -D CONFIG_ETH_SPI_ETHERNET_W5500=1 on IDF 4; otherwise both calls return false.

#include <stdint.h>
#include "driver/spi_common.h"

// Starts the W5500 on its own SPI bus (spi_bus_initialize(): the bus must not be in use) and
// gives it the chip's Ethernet MAC from eFuse (ESP_MAC_ETH). Once: a second call after a success
// returns true; a failure releases everything it set up, so it can be called again.
bool w5500EthBegin(spi_host_device_t host, int sck, int miso, int mosi, int cs, int irq, int rst, uint8_t spiMHz = 20);

// MAC of the running interface; false when w5500EthBegin() has not succeeded.
bool w5500EthMacAddress(uint8_t mac[6]);
