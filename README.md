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

## Pin assignment

| Pin | Assignment                          | Mode                      |
| --- | ----------------------------------- | ------------------------- |
| 22  | PSU1: Output enable                 | Digital out (optocoupled) |
| 23  | PSU1: Polarity set                  | Digital out (optocoupled) |
| 24  | PSU1: Current control mode          | Digital in                |
| 25  | PSU1: Voltage control mode          | Digital in                |
| 46  | PSU1: Voltage set                   | Analog out                |
| 47  | PSU1: Current limit set             | Analog out                |
| A0  | PSU1: Voltage sense                 | Analog in                 |
| A1  | PSU1: Current sense                 | Analog in                 |
| 26  | PSU2: Output enable                 | Digital out (optocoupled) |
| 27  | PSU2: Polarity set                  | Digital out (optocoupled) |
| 28  | PSU2: Current control mode          | Digital in                |
| 29  | PSU2: Voltage control mode          | Digital in                |
| 48  | PSU2: Voltage set                   | Analog out                |
| 49  | PSU2: Current limit set             | Analog out                |
| A2  | PSU2: Voltage sense                 | Analog in                 |
| A3  | PSU2: Current sense                 | Analog in                 |
| 30  | PSU3: Output enable                 | Digital out (optocoupled) |
| 31  | PSU3: Polarity set                  | Digital out (optocoupled) |
| 32  | PSU3: Current control mode          | Digital in                |
| 33  | PSU3: Voltage control mode          | Digital in                |
| 50  | PSU3: Voltage set                   | Analog out                |
| 51  | PSU3: Current limit set             | Analog out                |
| A4  | PSU3: Voltage sense                 | Analog in                 |
| A5  | PSU3: Current sense                 | Analog in                 |
| 34  | PSU4: Output enable                 | Digital out (optocoupled) |
| 35  | PSU4: Polarity set                  | Digital out (optocoupled) |
| 36  | PSU4: Current control mode          | Digital in                |
| 37  | PSU4: Voltage control mode          | Digital in                |
| 52  | PSU4: Voltage set                   | Analog out                |
| 53  | PSU4: Current limit set             | Analog out                |
| A6  | PSU4: Voltage sense                 | Analog in                 |
| A7  | PSU4: Current sense                 | Analog in                 |
| 38  | Filament power supply (230V side)   | Digital out               |

## Protocol

In contrast to other implementations this board supports an ASCII based
communication protocol. For synchronization all messages start with a
fixed synchronization pattern consisting of three dollar signs. Each command
is terminated via a CR LF or only LF (CR is simply ignored by the serial
parser). Numbers are transmitted as ASCII decimal numbers.

| Command | Sequence     | Response                                                                                             |
| ID      | $$$ID<CR,LF> | Sends a simple ID response that identifies the firmware (ex. ```$$$electronctrl_20210707_001<LF>```) |


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
    * Beam HV on (enable / disable beam)
        Procedure (doing an insulation test):
            * First sets low current limit (~ 20 uA)
            * Set voltages to 0
            * Wait for ~ 2 secs
            * Set output on
            * Increases voltage slowly (~ 10 V / sec?)
            * Check if not going into current limiting mode
                If current limiting -> error status, turn off everything
            * If reaching target: Set current limit high
    * Beam HV off:
            * All voltages set 0
            * Output off
    * Filament current on
    * Filament current off
    * Get estimated beam current
