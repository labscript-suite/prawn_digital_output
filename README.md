# Prawn Digital Output

This program is built off of the [digital output program](https://github.com/carterturn/prawn_do/tree/basis) developed by Carter Turnbaugh, with changes done to the PIO code so that the Raspberry Pi Pico can time itself to allow for a smaller minimum pulse width. Inspiration for the PIO code came from Philip Starkey's [PrawnBlaster PIO code](https://github.com/labscript-suite/PrawnBlaster/tree/master). 

This firmware turns pins 0-15 into programmable digital outputs which are triggered by input to pin 16.

Pin 20 is reserved for optional external clock input.

## Specs
* `Resolution`: 10 ns
* `Max Pulse Rate`: 20 MHz
* `Minimum Pulse Width`: 50 ns (x5 10 ns clock periods)
* `Maximum Pulse Width`: 42.94967295s (2^32 - 1 clock periods)
* `Max Instructions`: 30,000 (23,000 inside Labscript due to ZMQ timeout)
* Supports Indefinite Waits and Full Stops (Indicated by inputting 0 time instructions)
* Serial notification of any interruptions mid-sequence after `abt` command (Currently does not work for interuptions in the middle of the last pulse)

## Installing the .uf2 file
Before plugging in usb, hold down the bootsel button, which should pop-up a window to drag/drop the .uf2 file into, and when that .uf2 file is added, the window should disappear.

## Serial Communication
The basis of the functionality for this serial interface was developed in Carter Turnbaugh's code.

Commands are separated by a newline character: `'\n'`

* `sts` - Prints the current running and clock statuses.
  Running statuses include:
  
  * `STOPPED = 0`: indicates no sequence is running. This is the default state.
  * `TRANSITION_TO_RUNNING = 1`: device is transitions to running a sequence.
  * `RUNNING = 2`: device is running a sequence.
  * `ABORT_REQUESTED = 3`: Abort has been requested by the user.
  * `ABORTING = 4`: device is aborting sequence execution.
  * `ABORTED = 5`: device aborted most recent execution.
  * `TRANSITION_TO_STOP = 6`: device has ended sequence execution normally and as returning to stopped state.

  Clock statuses are `INTERNAL=0` and `EXTERNAL=1`. Default is internal.

* `add` - Enters mode for adding pulse instructions. The `end` command exits this mode.
  Each line has the syntax of `<output word (in hex)> <number of clock cycles (in hex)>`. The output word (in hex) sets the binary states of GPIO pins 0-15, aligned such that output 15 is the Most Significant Bit.
  The number of clock cycles sets how long this state is held before the next instruction.
  If the number of clock cycles is 0, this indicates an indefinite wait and an
  external hardware trigger on pin 16 restarts program execution.
  If two successive commands have clock cycles of 0, this indicates the end of the program.
* `run` - Used to start waiting for the hardware trigger to begin the programmed sequence of digital outputs.
* `swr` - Used to software start a programmed sequence of digital outputs (ie do not wait for a hardware trigger at program start).
* `abt` - Abort execution of a running sequence.
* `man` - Manually change the output pins' states. Syntax is `man <output word (in hex)>`.
* `gto` - Get the current output state. Returns states of pins 0-15 as a single hex number.
* `dmp` - Print the current sequence of programmed outputs.
* `cls` - Clear the current sequence of programmed outputs.
* `clk ext` - Sets the system clock to the clock input at GPIO pin 20.
* `clk int` - Sets the system clock to the internal system clock.
* `clk set <clock frequency in decimal>` - Sets the clock to the specified clock frequency, in Hz.
* `frq` - Measure and print system frequencies.
* `cur` - Prints the last command entered.
* `edt` - Allows the user to enter a new command to replace the last command entered.
* `deb` - Turns on debugging mode which adds printed output when adding instructions. By default, debugging is off.
* `ndb` - Turns off debugging mode.
* `ver` - Displays the version of the PrawnDO code.

## Clock Sync
Firmware supports the use of an external clock. This prevents any significant phase slip between a pseudoclock and this digital output controller if their clocks are phase synchronous. By default, the clock accepts a 100 MHz LVCMOS clock input.

## Example:
Below program sets one output high for 1 microsecond, then low while setting the next output high for 1 microsecond (64 in hex = 100 in decimal) for 6 outputs, then stopping:

Commands (`\n` explicitly denotes newline/pressing enter):

```
add\n
1 64\n
2 64\n
4 64\n
8 64\n
10 64\n
20 64\n
0 0\n
0 0\n
end\n
```


<img width="470" alt="documentation" src="https://github.com/pmiller2022/prawn_digital_output/assets/75953337/932b784f-346f-4598-8679-b857578e0291">

