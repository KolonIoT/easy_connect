/*
 * Copyright (c) 2016-2016 ARM Limited. All rights reserved.
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
#include "at24mac.h"

/* Device addressing */
#define AT24MAC_EEPROM_ADDRESS		(0x0A<<4)
#define AT24MAC_RW_PROTECT_ADDRESS	(0x06<<4)
#define AT24MAC_SERIAL_ADDRESS		(0x0B<<4)

/* Known memory blocks */
#define AT24MAC_SERIAL_OFFSET	(0x80)
#define AT24MAC_EUI64_OFFSET	(0x98)
#define AT24MAC_EUI48_OFFSET	(0x9A)

#define SERIAL_LEN 16
#define EUI64_LEN 8
#define EUI48_LEN 6

AT24Mac::I2CReset::I2CReset(PinName sda, PinName scl)
{
    mbed::DigitalInOut SDA(sda, PIN_OUTPUT, PullUp, 1);
    mbed::DigitalInOut SCL(scl, PIN_OUTPUT, PullUp, 0);
    //generate 9 clocks for worst-case scenario
    for (int i = 0; i < 10; ++i) {
        SCL = 1;
        wait_us(5);
        SCL = 0;
        wait_us(5);
    }
    //generate a STOP condition
    SDA = 0;
    wait_us(5);
    SCL = 1;
    wait_us(5);
    SDA = 1;
    wait_us(5);
}

/*I2C needs to be reset before constructing the I2C object (in case I2C is stuck)
  because they use the same pins, therefore i2c_reset has to be before _i2c
  in the initializer list*/
AT24Mac::AT24Mac(PinName sda, PinName scl) : i2c_reset(sda, scl), _i2c(sda, scl)
{
    // Do nothing
}

int AT24Mac::read_serial(void *buf)
{
    char offset = AT24MAC_SERIAL_OFFSET;
    if (_i2c.write(AT24MAC_SERIAL_ADDRESS, &offset, 1, true))
        return -1; //No ACK
    return _i2c.read(AT24MAC_SERIAL_ADDRESS, (char*)buf, SERIAL_LEN);
}

int AT24Mac::read_eui64(void *buf)
{
    char offset = AT24MAC_EUI64_OFFSET;
    if (_i2c.write(AT24MAC_SERIAL_ADDRESS, &offset, 1, true))
        return -1; //No ACK
    return _i2c.read(AT24MAC_SERIAL_ADDRESS, (char*)buf, EUI64_LEN);
}

int AT24Mac::read_eui48(void *buf)
{
    char offset = AT24MAC_EUI48_OFFSET;
    if (_i2c.write(AT24MAC_SERIAL_ADDRESS, &offset, 1, true))
        return -1; //No ACK
    return _i2c.read(AT24MAC_SERIAL_ADDRESS, (char*)buf, EUI48_LEN);
}
