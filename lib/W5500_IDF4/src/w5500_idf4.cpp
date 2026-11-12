#include "w5500_idf4.h"
#include "sdkconfig.h"
#include "esp_idf_version.h"

#if defined(CONFIG_ETH_SPI_ETHERNET_W5500) && (ESP_IDF_VERSION_MAJOR == 4)

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_netif_glue.h"
#include "esp_interface.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"

// arduino-esp32 2.0.x, WiFiGeneric.cpp: what ETHClass::begin() calls too
extern bool tcpipInit();
extern void add_esp_interface_netif(esp_interface_t interface, esp_netif_t* esp_netif);

static const char* TAG = "w5500";
static esp_eth_handle_t s_eth = nullptr;

namespace {
struct W5500Parts {
  spi_host_device_t host;
  bool bus = false;
  spi_device_handle_t spi = nullptr;
  esp_eth_mac_t* mac = nullptr;
  esp_eth_phy_t* phy = nullptr;
  esp_eth_handle_t eth = nullptr;
  esp_eth_netif_glue_handle_t glue = nullptr;
  esp_netif_t* netif = nullptr;
};

// Reverse order of setup; each step only if it was done.
void release(W5500Parts& p) {
  if (p.glue)  esp_eth_del_netif_glue(p.glue);   // drops its reference on the driver
  if (p.eth)   esp_eth_driver_uninstall(p.eth);  // deinits MAC and PHY
  if (p.phy)   p.phy->del(p.phy);
  if (p.mac)   p.mac->del(p.mac);
  if (p.netif) esp_netif_destroy(p.netif);
  if (p.spi)   spi_bus_remove_device(p.spi);
  if (p.bus)   spi_bus_free(p.host);
}

bool fail(W5500Parts& p, const char* step, esp_err_t err) {
  ESP_LOGE(TAG, "%s failed: %s", step, esp_err_to_name(err));
  release(p);
  return false;
}
} // namespace

bool w5500EthBegin(spi_host_device_t host, int sck, int miso, int mosi, int cs, int irq, int rst, uint8_t spiMHz)
{
  if (s_eth) return true;
  if (!tcpipInit()) {
    ESP_LOGE(TAG, "tcpipInit failed");
    return false;
  }

  W5500Parts p;
  p.host = host;

  // the MAC driver adds its interrupt handler to the shared GPIO ISR service (Arduino's attachInterrupt() uses it too)
  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return fail(p, "gpio_install_isr_service", err);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = mosi;
  bus.miso_io_num = miso;
  bus.sclk_io_num = sck;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  if ((err = spi_bus_initialize(host, &bus, SPI_DMA_CH_AUTO)) != ESP_OK) return fail(p, "spi_bus_initialize", err);
  p.bus = true;

  spi_device_interface_config_t dev = {};
  dev.command_bits = 16;  // W5500 frame: address phase
  dev.address_bits = 8;   // W5500 frame: control phase
  dev.mode = 0;
  dev.clock_speed_hz = spiMHz * 1000 * 1000;
  dev.spics_io_num = cs;
  dev.queue_size = 20;
  if ((err = spi_bus_add_device(host, &dev, &p.spi)) != ESP_OK) return fail(p, "spi_bus_add_device", err);

  eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(p.spi);
  w5500.int_gpio_num = irq;
  eth_mac_config_t macCfg = ETH_MAC_DEFAULT_CONFIG();
  macCfg.rx_task_stack_size = 4096;  // 2048 by default; the RX task hands frames to lwIP
  p.mac = esp_eth_mac_new_w5500(&w5500, &macCfg);
  if (!p.mac) return fail(p, "esp_eth_mac_new_w5500", ESP_FAIL);

  eth_phy_config_t phyCfg = ETH_PHY_DEFAULT_CONFIG();
  phyCfg.reset_gpio_num = rst;
  p.phy = esp_eth_phy_new_w5500(&phyCfg);
  if (!p.phy) return fail(p, "esp_eth_phy_new_w5500", ESP_FAIL);

  esp_eth_config_t ethCfg = ETH_DEFAULT_CONFIG(p.mac, p.phy);
  if ((err = esp_eth_driver_install(&ethCfg, &p.eth)) != ESP_OK) return fail(p, "esp_eth_driver_install", err);

  // the W5500 has no MAC of its own; set it before the netif glue copies it at attach
  uint8_t mac[6];
  if ((err = esp_read_mac(mac, ESP_MAC_ETH)) != ESP_OK) return fail(p, "esp_read_mac", err);
  if ((err = esp_eth_ioctl(p.eth, ETH_CMD_S_MAC_ADDR, mac)) != ESP_OK) return fail(p, "set MAC", err);

  esp_netif_config_t netifCfg = ESP_NETIF_DEFAULT_ETH();  // key "ETH_DEF": what ETH / tcpip_adapter look up
  p.netif = esp_netif_new(&netifCfg);
  if (!p.netif) return fail(p, "esp_netif_new", ESP_FAIL);
  p.glue = esp_eth_new_netif_glue(p.eth);
  if (!p.glue) return fail(p, "esp_eth_new_netif_glue", ESP_FAIL);
  if ((err = esp_netif_attach(p.netif, p.glue)) != ESP_OK) return fail(p, "esp_netif_attach", err);

  add_esp_interface_netif(ESP_IF_ETH, p.netif);  // WiFiGeneric: hostname, events (before the START event, as ETH.cpp)
  if ((err = esp_eth_start(p.eth)) != ESP_OK) {
    add_esp_interface_netif(ESP_IF_ETH, nullptr);
    return fail(p, "esp_eth_start", err);
  }
  s_eth = p.eth;
  ESP_LOGI(TAG, "started, MAC %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return true;
}

bool w5500EthMacAddress(uint8_t mac[6])
{
  return s_eth && esp_eth_ioctl(s_eth, ETH_CMD_G_MAC_ADDR, mac) == ESP_OK;
}

#else

bool w5500EthBegin(spi_host_device_t, int, int, int, int, int, int, uint8_t) { return false; }
bool w5500EthMacAddress(uint8_t[6]) { return false; }

#endif
