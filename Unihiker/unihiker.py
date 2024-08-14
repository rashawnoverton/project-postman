# -*- coding: utf-8 -*-
import time
from pinpong.board import Board, UART
def ascii_to_char(ascii_values):
    return ''.join(chr(value) for value in ascii_values)
Board("UNIHIKER").begin()  # Initialize the UNIHIKER board. Select the board type, if not specified, it will be automatically detected.
uart1 = UART(bus_num=0)   # Initialize UART (Hardware Serial 1)
uart1.init(baud_rate=115200,bits=8,parity=0,stop=1)  # Initialize UART: `baud_rate` for baud rate, `bits` for the number of data bits (8/9), `parity` for parity check (0 none/1 odd/2 even), `stop` for stop bits (1/2).
while True:
    while uart1.any() > 0:
        data = uart1.readline()
        if data:
            print(ascii_to_char(data))
    time.sleep(1)