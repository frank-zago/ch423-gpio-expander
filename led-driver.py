#!/usr/bin/env python3

# Naive clock implementation using a 5641AS 4 * 7-segments digits
# driven by a CH423.
#
# Copyright 2022, Frank Zago
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# OC-0 to OC-3 are connected to digits 1 to 4 respectively.
# segments a, b, c, d, e, f, g, dp are connected to S0 to S7 respectively.

import smbus
import time

# CH423 commands and config bits
CMD_SET_OC_L = 0x22
CMD_SET_OC_H = 0x23
CMD_CFG = 0x24
CMD_CFG_IO_OE = 0x01
CMD_CFG_DEC_L = 0x02
CMD_CFG_DEC_H = 0x04
CMD_CFG_X_INT = 0x08
CMD_CFG_OD_EN = 0x10
CMD_CFG_INTENS_MASK = 0x60
CMD_CFG_SLEEP = 0x80
CMD_READ_IO = 0x26
CMD_SET_IO = 0x30

# The CH423 is on bus 11, i.e. device i2c-11 as reported by
# i2cdetect. Change if needed.
ch = smbus.SMBus()
ch.open(11)

# Converts an hexadecimal number to a 7-segment LED digit.
# Bit 0 = a, bit 1 = b, ..., bit 7 = dp
# This can be fed directly to the CH423.
hex_to_led = [0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07,
              0x7f, 0x6f, 0x77, 0x7c, 0x58, 0x5e, 0x79, 0x71]


def display_num(digit, value):
    # Display an hex number at a given digit (0 to 15)
    ch.write_byte(CMD_SET_IO | digit, hex_to_led[value])


def display_nothing(digit):
    # Empty the digit
    ch.write_byte(CMD_SET_IO | digit, 0)


# Initialize the chip. CMD_CFG_DEC_H would be needed if digits 8 to 15
# were used.
ch.write_byte(CMD_CFG, CMD_CFG_IO_OE | CMD_CFG_DEC_L)

while True:
    tm = time.localtime(time.time())

    if tm.tm_hour > 10:
        display_num(0, tm.tm_hour // 10)
    else:
        display_nothing(0)

    display_num(1, tm.tm_hour % 10)

    display_num(2, tm.tm_min // 10)
    display_num(3, tm.tm_min % 10)

    time.sleep(1)
