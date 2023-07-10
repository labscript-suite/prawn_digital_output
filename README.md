# prawn_digital_output

This program is built off of the digital output program developed by Carter Turnbaugh (https://github.com/carterturn/prawn_do/tree/basis), with major changes done to the PIO code so that the Raspberry Pi Pico can time itself to allow for a smaller minimum pulse width. Inspiration for the PIO code came from Philip Starkey's PrawnBlaster PIO code (https://github.com/labscript-suite/PrawnBlaster/tree/master)

This firmware turns pins 0-15 into programmable digital outputs which are triggered by input from pin 16.

Specs:
Resolution: 10ns
Max Pulse Rate: 20 MHz
Minimum Pulse Width: 50ns


Clock Sync:
A firmware that includes a clock sync is available, and this prevents any significant phase slip between a pseudoclock and this digital output controller.
