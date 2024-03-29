/*
 * Copyright (c) 2014-2015 ARM Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NANOSTACK_PHY_MCR20A_H_
#define NANOSTACK_PHY_MCR20A_H_

#include "mbed.h"

#ifdef MBED_CONF_NANOSTACK_CONFIGURATION

#include "NanostackRfPhy.h"

// Arduino pin defaults for convenience
#if !defined(MCR20A_SPI_MOSI)
#define MCR20A_SPI_MOSI   D11
#endif
#if !defined(MCR20A_SPI_MISO)
#define MCR20A_SPI_MISO   D12
#endif
#if !defined(MCR20A_SPI_SCLK)
#define MCR20A_SPI_SCLK   D13
#endif
#if !defined(MCR20A_SPI_CS)
#define MCR20A_SPI_CS     D10
#endif
#if !defined(MCR20A_SPI_RST)
#define MCR20A_SPI_RST    D5
#endif
#if !defined(MCR20A_SPI_IRQ)
#define MCR20A_SPI_IRQ    D2
#endif

class NanostackRfPhyMcr20a : public NanostackRfPhy {
public:
    NanostackRfPhyMcr20a(PinName spi_mosi, PinName spi_miso,
                         PinName spi_sclk, PinName spi_cs,  PinName spi_rst,
                         PinName spi_irq);
    virtual ~NanostackRfPhyMcr20a();
    virtual int8_t rf_register();
    virtual void rf_unregister();
    virtual void get_mac_address(uint8_t *mac);
    virtual void set_mac_address(uint8_t *mac);

private:
    SPI _spi;
    DigitalOut _rf_cs;
    DigitalOut _rf_rst;
    InterruptIn _rf_irq;
    DigitalIn _rf_irq_pin;

    void _pins_set();
    void _pins_clear();
};

#endif /* MBED_CONF_NANOSTACK_CONFIGURATION */
#endif /* NANOSTACK_PHY_MCR20A_H_ */
