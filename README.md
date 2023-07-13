# Prawn Digital Output

This program is built off of the digital output program developed by Carter Turnbaugh (https://github.com/carterturn/prawn_do/tree/basis), with changes done to the PIO code so that the Raspberry Pi Pico can time itself to allow for a smaller minimum pulse width. Inspiration for the PIO code came from Philip Starkey's PrawnBlaster PIO code (https://github.com/labscript-suite/PrawnBlaster/tree/master). 

This firmware turns pins 0-15 into programmable digital outputs which are triggered by input to pin 16.

# Specs
Resolution: 10ns

Max Pulse Rate: 20 MHz

Minimum Pulse Width: 50ns (5 10ns periods)

Maximum Pulse Width: 42.94967295s (2^32 - 1 reps)

Supports Indefinite Waits and Full Stops (Indicated by inputting 0 reps)

Serial notification of any interruptions mid-sequence after 'abt' command (Currently does not work for interuptions in the middle of the last pulse)

# Installing the .uf2 file
Before plugging in usb, hold down the bootsel button, which should pop-up a window to drag/drop the .uf2 file into, and when that .uf2 file is added, the window should disappear.

# Serial Communication
The basis of the functionality for this serial was developed in Carter Turnbaugh's code.

add - Used to add a digital output pulse, first asks for 16-bit output word in hexadecimal, then asks for the number of repetitions in decimal. If the number of reps is given as zero, then the command will ask for either 0 for a full stop or 1 for an indefinite wait.

adm - Same functionality as add, but allows for multiple repeated pulses to be added (with wait times between each pulse equivalent to how long each pulse is). Does not support indefinite waits or full stops currently.

run - Used to start waiting for the hardware trigger to begin the programmed sequence of digital outputs.

abt - Exit the run sequence

dmp - Print the current sequence of programmed outputs.

cls - Clear the current sequence of programmed outputs.



# Clock Sync
A firmware that includes a clock sync is available, and this prevents any significant phase slip between a pseudoclock and this digital output controller. By default, the clock accepts a 100 MHz clock input.
