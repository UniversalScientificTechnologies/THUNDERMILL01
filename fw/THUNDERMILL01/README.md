

# flashování 

  pio run -e THUNDERMILL01_isp -t upload



# Flashování bootloaderu

```

~/.platformio/packages/tool-avrdude/avrdude \
  -C ~/.platformio/packages/tool-avrdude/avrdude.conf \
  -p atmega1284p -c stk500v2 -P /dev/ttyUSB0 -b115200 -B10 -e \
  -Uflash:w:$HOME/.platformio/packages/framework-arduino-avr-mightycore/bootloaders/optiboot_flash/bootloaders/atmega1284p/16000000L/optiboot_flash_atmega1284p_UART0_115200_16000000L_B7_BIGBOOT.hex:i \
  -Uefuse:w:0xFD:m -Uhfuse:w:0xD2:m -Ulfuse:w:0xF7:m


```

# nahravani pres uart


  pio run -e THUNDERMILL01_uart --target upload


Funguje to i s TFUSBSERIAL prevodnikem
