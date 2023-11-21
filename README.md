# Prawn Digital Output

This program is built off of the [digital output program](https://github.com/carterturn/prawn_do/tree/basis) developed by Carter Turnbaugh, with changes done to the PIO code so that the Raspberry Pi Pico can time itself to allow for a smaller minimum pulse width. Inspiration for the PIO code came from Philip Starkey's [PrawnBlaster PIO code](https://github.com/labscript-suite/PrawnBlaster/tree/master). 

This firmware turns pins 0-15 into programmable digital outputs. Their states are controlled via a single 16-bit tuning word. A predefined sequence of output states can be programmed with precise timing for how long to maintain an output state defined in numbers of system clock cycles.

Pin 20 is reserved for optional external clock input.

## Specs

All timings are given relative to the default system clock of 100 MHz.

* **Resolution**: 1 clock cycle (10 ns)
* **Minimum Pulse Width**: 5 clock cycles (50 ns)
* **Max Pulse Rate**: 1/5 system clock frequency (20 MHz)
* **Maximum Pulse Width**: 2^32 - 1 clock cycles (42.94967295 s)
* **Max Instructions**: 30,000 (23,000 inside Labscript due to ZMQ timeout)
* Supports Indefinite Waits and Full Stops
* Max system clock frequency of 133 MHz

## Installing the .uf2 file
Before plugging in usb, hold down the bootsel button, which should pop-up a window to drag/drop the .uf2 file into, and when that .uf2 file is added, the window should disappear.

## Serial Communication
Commands must end with a newline character: `'\n'`.
All responses are terminated by CRLF: `'\r\n'`.
Commands can also be terminated by CRLF for symmetry.

These commands can be run at any time (ie during sequence execution).

* `sts` - Prints the current running and clock statuses.
  Running statuses include:
  
  * `STOPPED=0` - indicates no sequence is running. This is the default state.
  * `TRANSITION_TO_RUNNING=1` - device is transitions to running a sequence.
  * `RUNNING=2` - device is running a sequence.
  * `ABORT_REQUESTED=3` - Abort has been requested by the user.
  * `ABORTING=4` - device is aborting sequence execution.
  * `ABORTED=5` - device aborted most recent execution.
  * `TRANSITION_TO_STOP=6` - device has ended sequence execution normally and as returning to stopped state.

  Clock statuses are `INTERNAL=0` and `EXTERNAL=1`. Default is internal.
* `deb` - Turns on debugging mode which adds printed output when adding instructions. By default, debugging is off.
* `ndb` - Turns off debugging mode.
* `ver` - Displays the version of the PrawnDO code.
* `abt` - Abort execution of a running sequence.

These commands must be run when the running status is `STOPPED`.

* `add` - Enters mode for adding pulse instructions.
  * Each line has the syntax of `<output word (in hex)> <number of clock cycles (in hex)>`. 
    * The output word sets the binary states of GPIO pins 0-15, aligned such that output 15 is the Most Significant Bit.
    * The number of clock cycles sets how long this state is held before the next instruction.
    * If the number of clock cycles is 0, this indicates an indefinite wait.
    Output word of this instruction is held until an external hardware trigger on pin 16 restarts program execution.
    * If two successive commands have clock cycles of 0, this indicates the end of the program. Output word of this instruction is ignored.
  * `end` command exits this mode.
* `run` - Used to hardware start a programmed sequence (ie waits for external trigger before processing first instruction).
* `swr` - Used to software start a programmed sequence (ie do not wait for a hardware trigger at sequence start).

* `man <output word (in hex)>` - Manually change the output pins' states.
* `gto` - Get the current output state. Returns states of pins 0-15 as a single hex number.

* `cur` - Prints the last command entered.
* `edt` - Allows the user to enter a new command to replace the last command entered.

* `dmp` - Print the current sequence of programmed outputs.
* `len` - Print total number of instructions in the programmed sequence.
* `cls` - Clear the current sequence of programmed outputs.

* `clk <src (0: internal, 1: external)> <freq (in decimal Hz)>` - Sets the system clock and frequency. Maximum frequency allowed is 133 MHz. Default is 100 MHz internal clock. External clock frequency input is GPIO pin 20.
* `frq` - Measure and print system frequencies.

The basis of the functionality for this serial interface was developed by Carter Turnbaugh.

## Clock Sync
Firmware supports the use of an external clock. This prevents any significant phase slip between a pseudoclock and this digital output controller if their clocks are phase synchronous. Without external buffering hardware, clock must be LVCMOS compatible.

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

