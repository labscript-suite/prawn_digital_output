# Prawn Digital Output

This program is built off of the [digital output program](https://github.com/carterturn/prawn_do/tree/basis) developed by Carter Turnbaugh, with changes done to the PIO code so that the Raspberry Pi Pico can time itself to allow for a smaller minimum pulse width. Inspiration for the PIO code came from Philip Starkey's [PrawnBlaster PIO code](https://github.com/labscript-suite/PrawnBlaster/tree/master). 

This firmware turns pins 0-15 into programmable digital outputs. Their states are controlled via a single 16-bit tuning word. A predefined sequence of output states can be programmed with precise timing for how long to maintain an output state defined in numbers of system clock cycles.

Pin 20 is reserved for optional external clock input.

## Supported boards

We support either the official [Raspberry Pi Pico (RP2040 chip)](https://www.raspberrypi.com/products/raspberry-pi-pico/) board or the official [Raspberry Pi Pico 2 (RP2350 chip)](https://www.raspberrypi.com/products/raspberry-pi-pico-2/) board.
We recommend the Pico 2 (RP2350) board due to its faster clock and larger RAM.

## Specs

All timings are given relative to the default system clock of 100 MHz.

* **Resolution**: 1 clock cycle (10 ns)
* **Minimum Pulse Width**: 5 clock cycles (50 ns)
* **Max Pulse Rate**: 1/10 system clock frequency (10 MHz)
* **Maximum Pulse Width**: 2^32 - 1 clock cycles (42.94967295 s)
* **Max Instructions (RP2350)**: 60,000 (23,000 inside Labscript due to ZMQ timeout)
* **Max Instructions (RP2040)**: 30,000 (23,000 inside Labscript due to ZMQ timeout)
* Supports Indefinite Waits and Full Stops
* Max system clock frequency of 150 MHz (RP2350) or 133 MHz (RP2040)

## Installing the .uf2 file
Download the latest prawn_do.uf2 file: [Pico - RP2040](https://github.com/labscript-suite/prawn_digital_out/releases/latest/download/prawn_do_rp2040.uf2), [Pico 2 - RP2350](https://github.com/labscript-suite/prawn_digital_out/releases/latest/download/prawn_do_rp2350.uf2).
On your Raspberry Pi Pico, hold down the "bootsel" button while plugging the Pico into USB port on a PC (that must already be turned on).
The Pico should mount as a mass storage device (if it doesn't, try again or consult the Pico documentation).
Drag and drop the `.uf2` file into the mounted mass storage device.
The mass storage device should unmount after the copy completes.
Your Pico is now running the Prawn Digital Output firmware!

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
* `brd` - Responds with a string containing the board version (`pico1` or `pico2`).
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
* `set <address (in hex)> <output word (in hex)> <number of clock cycles (in hex)>` - Sets instruction at address (0 indexed).
* `get <address (in hex)>` - Gets instruction at address. Returns output word and number of clock cycles separated by a space, in same format as `set`.
* `run` - Used to hardware start a programmed sequence (ie waits for external trigger before processing first instruction).
* `swr` - Used to software start a programmed sequence (ie do not wait for a hardware trigger at sequence start).
* `adm <starting instruction address (in hex)> <number of instructions (in hex)>` - Enters mode for adding pulse instructions in binary.
  * This command over-writes any existing instructions in memory. The starting instruction address specifies where to insert the block of instructions. This is generally set to 0 to write a complete instruction set from scratch.
  * The number of instructions must be specified with the command, which is used to determine the total number of bytes to be read (6 6 times the number of instructions).
  * This command returns `ready\r\n` to signify it is ready for binary data. The Pico will then read the total number of bytes. This mode can not be terminated until that many bytes are read.
  * Each instruction is specified by a 16 bit unsigned integer (little Endian, output 15 is most significant) specifying the state of the outputs and a 32 bit unsigned integer (little Endian) specifying the number of clock cycles.
    * The number of clock cycles sets how long this state is held before the next instruction.
    * If the number of clock cycles is 0, this indicates an indefinite wait.
    Output word of this instruction is held until an external hardware trigger on pin 16 restarts program execution.
    * If two successive commands have clock cycles of 0, this indicates the end of the program. Output word of this instruction is ignored.

* `man <output word (in hex)>` - Manually change the output pins' states.
* `gto` - Get the current output state. Returns states of pins 0-15 as a single hex number.

* `cur` - Prints the last command entered.
* `edt` - Allows the user to enter a new command to replace the last command entered using `add`.

* `dmp` - Print the current sequence of programmed outputs.
* `len` - Print total number of instructions in the programmed sequence.
* `cls` - Clear the current sequence of programmed outputs.

* `clk <src (0: internal, 1: external)> <freq (in decimal Hz)>` - Sets the system clock and frequency. Maximum frequency allowed is 150 MHz (RP2350) or 133 MHz (RP2040). Default is 100 MHz internal clock. External clock frequency input is GPIO pin 20.
* `frq` - Measure and print system frequencies.
* `prg` - Equivalent to disconnecting the Pico, holding down the "bootsel" button, and reconnecting the Pico. Places the Pico into firmware flashing mode; the PrawnDO serial port should disappear and the Pico should mount as a mass storage device.

The basis of the functionality for this serial interface was developed by Carter Turnbaugh.

## Clock Sync
Firmware supports the use of an external clock. This prevents any significant phase slip between a pseudoclock and this digital output controller if their clocks are phase synchronous. Without external buffering hardware, clock must be LVCMOS compatible.

## Examples:
Below python script sets one output high for 1 microsecond, then low while setting the next output high for 1 microsecond (64 in hex = 100 in decimal) for 6 outputs, then stopping. `do` is a pyserial handle to the pico.

```python
bits = [1, 2, 3, 8, 10, 20, 0, 0]
cycles = [100, 100, 100, 100, 100, 100, 0, 0]

do.write('add\n'.encode())
for bit, cycle in zip(bits, cycles):
  do.write(f'{bit:x} {cycle:x}\n'.encode())

do.write('end\n'.encode())
resp = do.readline().decode()
assert resp == 'ok\r\n'
```

Output of the above sequence.
<img width="470" alt="documentation" src="https://github.com/pmiller2022/prawn_digital_output/assets/75953337/932b784f-346f-4598-8679-b857578e0291">

This is a python script that employs the binary write `adm` command.
In order to function correctly, the data to be written must be in a numpy structured array where each column dtype can be specified uniquely.
Below assumes the `bits` and `cycles` are 1-D arrays that have already been created.
It also assumes `do` is a pyserial handle to the device. 

```python
data = np.zeros(len(bits), dtype=[('bits', 'u2'), ('cycles','u4')])
data['bits'] = bits
data['cycles'] = cycles

serial_buffer = data.tobytes()

do.write(f'adm 0 {len(data):x}\r\n'.encode())
resp = do.readline().decode()
assert resp == 'ready\r\n', f'Not ready for binary data. Response was {repr(resp)}'
do.write(serial_buffer)
resp = do.readline().decode()
if resp != 'ok\r\n':
  # if response not ok, probably more than one line, read them all
  # done this way to prevent readlines timeout for standard operation
  extra_resp = do.readlines()
  resp += ''.join([st.decode() for st in extra_resp])
  print(f'Write had errors. Response was {repr(resp)}')
```

## Compiling the firmware

If you want to make changes to the firmware, or want to compile it yourself (because you don't trust binary blobs from the internet), we provide a docker configuration to help you do that.

1. Install docker desktop and make sure it is running (if you are on Windows, you may have to mess around a bit to get virtualisation working at an operating system level)
2. Clone this repository
3. Open a terminal with the current working directory set to the repository root (the `docker-compose.yaml`` file should be there)
4. Run `docker compose build --pull` to build the docker container
5. Run `docker compose up` to build the PrawnDO firmware.

Step 4 will take a while as it has to build the docker container.
If it is slow to download packages from the Ubuntu package repositories, consider providing an explicit apt mirror that is fast for you: `docker compose build --pull --build-arg APT_MIRROR="http://azure.archive.ubuntu.com/ubuntu/"`.

If you want to change which version of the pico SDK it builds against, this is set in the `build/docker/Dockerfile` file.
Just change the git tag of the pico SDK that gets cloned out by git, then rebuild the docker container (see step 4).

Note once the docker container is built, you can run step 5 as many times as you like.
You do not need to rebuild the container, even if you make changes to the source code.
You only need to rebuild the docker container if you modify the `build/docker/Dockerfile` file.

By default, running `docker compose up` builds the all variations of the firmware.
If you only want to build for a specific board, run either `docker compose up build_rp2040_firmware` or `docker compose up build_rp2350_firmware`.

The firmware will be located in `build_rp2xxx/prawn_do/prawn_do_rp2xxx.uf2` where `rp2xxx` will be either `rp2040` or `rp2350`.
