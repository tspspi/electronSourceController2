# Electron gun power supply and monitoring controller

This is firmware for a simple electron gun controller that is controlling

* Up to four FUG HCP 14-6500 high voltage power supplies (acceleration
  voltage, Wehnelt cylinder, focus voltage and a single deflection voltage
  power supply)
* The voltage and current measured by FUG HCP 14-6500 can also be used for
  general monitoring and beam loss measurement

## External communication ports

* UART0 provides main communication interface (and is also used by avrdude
  during flashing - there is an Arduino bootloader flashed on the board so
  flash method ```ẁiring``` can be used via USB

* Other UARTs may be used for RS485 or other components later on

## Protocol

In contrast to other implementations this board supports an ASCII based
communication protocol. For synchronization all messages start with a
fixed synchronization pattern consisting of three dollar signs. Each command
is terminated via a CR LF or only LF (CR is simply ignored by the serial
parser). Numbers are transmitted as ASCII decimal numbers.

| Command | Sequence |
| ID      | $$$ID<CR,LF> |


* Per PSU (use PSU index):
    * Output enable on/off + status
    * PSU polarity (?expose?) + status
    * Set Voltage
    * Set Current Limit
    * Get Mode (Status: CV or CC)
    * Get current voltage
    * Get current current
* External switch for filament:
    * Filament power on/off (bypassing external switch)
    * Filament voltage
    * Filament current
* High level:
    * Beam on (enable )


## Pin assignment
