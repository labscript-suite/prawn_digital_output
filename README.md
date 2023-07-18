# Prawn Digital Output

This program is built off of the digital output program developed by Carter Turnbaugh (https://github.com/carterturn/prawn_do/tree/basis), with changes done to the PIO code so that the Raspberry Pi Pico can time itself to allow for a smaller minimum pulse width. Inspiration for the PIO code came from Philip Starkey's PrawnBlaster PIO code (https://github.com/labscript-suite/PrawnBlaster/tree/master). 

This firmware turns pins 0-15 into programmable digital outputs which are triggered by input to pin 16.

Pin 20 is reserved for optional external clock syncing.

# Specs
Resolution: 10ns

Max Pulse Rate: 20 MHz

Minimum Pulse Width: 50ns (5 10ns periods)

Maximum Pulse Width: 42.94967295s (2^32 - 1 reps)

Max Instructions: 30,000

Supports Indefinite Waits and Full Stops (Indicated by inputting 0 reps)

Serial notification of any interruptions mid-sequence after 'abt' command (Currently does not work for interuptions in the middle of the last pulse)

# Installing the .uf2 file
Before plugging in usb, hold down the bootsel button, which should pop-up a window to drag/drop the .uf2 file into, and when that .uf2 file is added, the window should disappear.

# Serial Communication
The basis of the functionality for this serial was developed in Carter Turnbaugh's code.

add - Sets the pico to accept instructions in the format shown below and append them into memory. 'end' exits the add mode.

FORMAT: <output word in hex> <number of reps in hex> <Full Stop (0) or Indefinite Wait (1)>

adm - Same functionality as add, but allows for multiple repeated pulses to be added in the format below. Allows for an optionally controlled wait reps between pulses, otherwise by default the wait reps equals the input reps. 'end' command exits this mode.

FORMAT: <output word in hex> <number of reps in hex> <number of pulses in decimal> <OPTIONAL: wait reps in hex>

run - Used to start waiting for the hardware trigger to begin the programmed sequence of digital outputs.

abt - Exit the run sequence.

dmp - Print the current sequence of programmed outputs.

cls - Clear the current sequence of programmed outputs.

clk ext: Sets the system clock to the clock input at GPIO pin 20.

clk int: Sets the system clock to the internal system clock.

clk set <clock frequency in decimal>: Sets the clock to the specified clock frequency.

cur - Prints the last command entered.

edt - Allows the user to enter a new command to replace the last command entered.

deb - Turns on debugging mode which adds printed output when adding instructions. By default, debugging is off.

ndb - Turns off debugging mode.

# Clock Sync
A firmware that includes a clock sync is available, and this prevents any significant phase slip between a pseudoclock and this digital output controller. By default, the clock accepts a 100 MHz clock input.
