# ZMK firmware for mEnki
This branch is intended for the regular version of the keyboard. For V2 use [another branch](https://github.com/aroum/zmk-mEnki/tree/mEnki42_v2). The bootloader and compiled firmware can be downloaded in the [releases section](https://github.com/aroum/zmk-mEnki/releases). You can change keymap using [this tool](https://nickcoutsos.github.io/keymap-editor/).


![image](pics\mEnki.png)
-----

## RGB Indicator

The mEnki keyboard includes an RGB indicator with specific color-coded signals to display battery status, Bluetooth profile, USB connection, and other states. Below are the indicator patterns:

### Battery Status
- **Low Battery**: Flashes **yellow** 3 times when the battery is low.
- **Battery Level**: Displays **green** flashes indicating battery level:
  - 1 flash: Low charge
  - 2 flashes: Medium charge
  - 3 flashes: High charge

### Bluetooth Status
- **Bluetooth Profile**: Displays **blue** flashes to indicate the active Bluetooth profile:
  - 1 flash: Profile 1
  - 2 flashes: Profile 2
  - 3 flashes: Profile 3
- **Bluetooth Connection Lost**: Flashes **red** 3 times when the keyboard loses connection to a paired Bluetooth device.

### USB Connection
- **USB Connected**: Flashes **purple** 1 time when the keyboard is connected via USB.

This RGB indicator setup provides quick, color-coded feedback on the keyboard’s status for efficient monitoring.

