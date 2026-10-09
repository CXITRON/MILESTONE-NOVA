#pragma once
#include "SPI.h"
constexpr int SPI2_HOST=2, SPI_DMA_CH_AUTO=0, ESP_OK=0, SPI_TRANS_USE_TXDATA=1;
using spi_device_handle_t=void*;
struct spi_bus_config_t { int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num, max_transfer_sz; };
struct spi_device_interface_config_t { int clock_speed_hz, mode, spics_io_num, queue_size; };
struct spi_transaction_t { unsigned flags=0; size_t length=0; uint8_t tx_data[4]{}; const void *tx_buffer=nullptr; };
inline bool nativeFail=false, nativeWindow=true;
inline int spi_bus_initialize(int, const spi_bus_config_t*, int) { return 0; }
inline int spi_bus_free(int) { return 0; }
inline int spi_bus_add_device(int, const spi_device_interface_config_t*, spi_device_handle_t *out) { *out=reinterpret_cast<void*>(1); return 0; }
inline int spi_bus_remove_device(spi_device_handle_t) { return 0; }
inline int spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t *t) {
 if (nativeFail) return -1;
 if ((t->flags & SPI_TRANS_USE_TXDATA) && dcLevel == LOW) { currentCommand=t->tx_data[0]; if(currentCommand==0x2C) nativeWindow=true; return 0; }
 const auto *p=(t->flags & SPI_TRANS_USE_TXDATA) ? t->tx_data : static_cast<const uint8_t*>(t->tx_buffer);
 if(currentCommand==0x2B) { startRows.push_back(p[0]*256+p[1]); return 0; }
 if(currentCommand!=0x2C) return 0;
 if(nativeWindow) { transfers.emplace_back(); nativeWindow=false; }
 for(size_t i=0;i<t->length/8;i+=2) transfers.back().push_back(uint16_t(p[i]*256+p[i+1]));
 testMicros+=pixelWriteUs;
 return 0;
}

inline int spi_device_transmit(spi_device_handle_t h, spi_transaction_t *t) { return spi_device_polling_transmit(h,t); }
