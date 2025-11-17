#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "pico/bootrom.h"
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/clocks.h"


#include "prawn_do.pio.h"
#include "fast_serial.h"

#define LED_PIN 25
// output pins to use, must match pio
#define OUTPUT_PIN_BASE 0
#define OUTPUT_WIDTH 16
// mask which bits we are using
uint32_t output_mask = ((1 << OUTPUT_WIDTH) - 1) << OUTPUT_PIN_BASE;
// command type enum
enum COMMAND {
	BUFFERED = 1 << OUTPUT_WIDTH,
	HWSTART = 2 << OUTPUT_WIDTH,
	BUFFERED_HWSTART = BUFFERED | HWSTART,
	MANUAL = 0
};

// two DO CMDS per INSTRUCTION
#define MAX_INSTR PRAWNDO_NUM_INSTRUCTIONS
#define MAX_DO_CMDS (2*MAX_INSTR)
uint32_t do_cmds[MAX_DO_CMDS];
uint32_t do_cmd_count = 0;


#define SERIAL_BUFFER_SIZE 256
char serial_buf[SERIAL_BUFFER_SIZE];

// STATUS flag
int status;
#define STOPPED 0
#define TRANSITION_TO_RUNNING 1
#define RUNNING 2
#define ABORT_REQUESTED 3
#define ABORTING 4
#define ABORTED 5
#define TRANSITION_TO_STOP 6

#define INTERNAL 0
#define EXTERNAL 1
int clk_status = INTERNAL;
unsigned short debug = 0;
const char ver[6] = "1.3.0";

// Mutex for status
static mutex_t status_mutex;

// Thread safe functions for getting/setting status
int get_status()
{
	mutex_enter_blocking(&status_mutex);
	int status_copy = status;
	mutex_exit(&status_mutex);
	return status_copy;
}

void set_status(int new_status)
{
	mutex_enter_blocking(&status_mutex);
	status = new_status;
	mutex_exit(&status_mutex);
}

/*
  Start pio state machine

  This function resets the pio state machine,
  then sets up direct memory access (dma) from the pio to do_cmds.
  Finally, it starts the state machine, which will then run the pio program 
  independently of the CPU.

  This function is inspired by the logic_analyser_arm function on page 46
  of the Raspberry Pi Pico C/C++ SDK manual (except for output, rather than 
  input).
 */
void start_sm(PIO pio, uint sm, uint dma_chan, uint offset, uint hwstart){
	pio_sm_set_enabled(pio, sm, false);

	// Clearing the FIFOs and restarting the state machine to prevent old
	// instructions from persisting into future runs
	pio_sm_clear_fifos(pio, sm);
	pio_sm_restart(pio, sm);
	// send initial wait command (preceeds DMA transfer)
	pio_sm_put_blocking(pio, sm, hwstart);
	// Explicitly jump to reset program to the start
	pio_sm_exec(pio, sm, pio_encode_jmp(offset));

	// Create dma configuration object
	dma_channel_config dma_config = dma_channel_get_default_config(dma_chan);
	// Automatically increment read address as pio pulls data
	channel_config_set_read_increment(&dma_config, true);
	// Don't increment write address (pio should not write anyway)
	channel_config_set_write_increment(&dma_config, false);
	// Set data transfer request signal to the one pio uses
	channel_config_set_dreq(&dma_config, pio_get_dreq(pio, sm, true));
	// Start dma with the selected channel, generated config
	dma_channel_configure(dma_chan, &dma_config,
						  &pio->txf[sm], // write address is fifo for this pio 
										 // and state machine
						  do_cmds, // read address is do_cmds
						  do_cmd_count, // read a total of do_cmd_count entries
						  true); // trigger (start) immediately

	// Actually start state machine
	pio_sm_set_enabled(pio, sm, true);
}
/*
  Stop pio state machine

  This function stops dma, stops the pio state machine,
  and clears the transfer fifos of the state machine.
 */
void stop_sm(PIO pio, uint sm, uint dma_chan){
	dma_channel_abort(dma_chan);
	pio_sm_set_enabled(pio, sm, false);
	pio_sm_clear_fifos(pio, sm);
}

/* Measure system frequencies
From https://github.com/raspberrypi/pico-examples under BSD-3-Clause License
*/
void measure_freqs(void)
{
    uint f_pll_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_SYS_CLKSRC_PRIMARY);
    uint f_pll_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_USB_CLKSRC_PRIMARY);
    uint f_rosc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC);
    uint f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    uint f_clk_peri = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_PERI);
    uint f_clk_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB);
    uint f_clk_adc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_ADC);
#ifdef CLOCKS_FC0_SRC_VALUE_CLK_RTC
    uint f_clk_rtc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_RTC);
#endif

    fast_serial_printf("pll_sys = %dkHz\r\n", f_pll_sys);
    fast_serial_printf("pll_usb = %dkHz\r\n", f_pll_usb);
    fast_serial_printf("rosc = %dkHz\r\n", f_rosc);
    fast_serial_printf("clk_sys = %dkHz\r\n", f_clk_sys);
    fast_serial_printf("clk_peri = %dkHz\r\n", f_clk_peri);
    fast_serial_printf("clk_usb = %dkHz\r\n", f_clk_usb);
    fast_serial_printf("clk_adc = %dkHz\r\n", f_clk_adc);
#ifdef CLOCKS_FC0_SRC_VALUE_CLK_RTC
    fast_serial_printf("clk_rtc = %dkHz\r\n", f_clk_rtc);
#endif
}

/* Resusitation function that restarts the clock internally if there are any 
   issues with syncing the external clock or invalid changes to the clock
*/
void clk_resus(void) {
	// Restarting the internal clock at 100 MHz (set in kHz)
	set_sys_clock_khz(100000, false);

	// Remove the clock syncing functionality
	gpio_set_function(20, GPIO_FUNC_NULL);

	clk_status = INTERNAL;

	// Restart the serial communication
	stdio_init_all();

	// Notify the user that the system clock has been restarted
	fast_serial_printf("System Clock Resus'd\r\n");
}



void core1_entry() {
	// Setup PIO
	PIO pio = pio0;
	uint sm = pio_claim_unused_sm(pio, true);
	uint dma_chan = dma_claim_unused_channel(true);
	uint offset = pio_add_program(pio, &prawn_do_program); // load prawn_do PIO 
														   // program

	// initialize prawn_do PIO program on chosen PIO and state machine at 
	// required offset
	pio_sm_config pio_config = prawn_do_program_init(pio, sm, offset);

	// signal core1 ready for commands
	multicore_fifo_push_blocking(0);

	while(1){
		// wait for message from main core
		uint32_t command = multicore_fifo_pop_blocking();

		if(command & BUFFERED){
			// buffered execution
			uint32_t hwstart = (command & HWSTART);

			set_status(TRANSITION_TO_RUNNING);
			if(debug){
				fast_serial_printf("hwstart: %d\r\n", hwstart);
			}
			// start the state machine
			start_sm(pio, sm, dma_chan, offset, hwstart);
			set_status(RUNNING);

			// can save IRQ PIO instruction by using the following check instead
			//while ((dma_channel_is_busy(dma_chan) // checks if dma finished
			//        || pio_sm_is_tx_fifo_empty(pio, sm)) // ensure fifo is empty once dma finishes
			//	     && get_status() != ABORT_REQUESTED) // breaks if Abort requested
			while (!pio_interrupt_get(pio, sm) // breaks if PIO program reaches end
				&& get_status() != ABORT_REQUESTED // breaks if Abort requested
				){
				// tight loop checking for run completion
				// exits if program signals IRQ (at end) or abort requested
				continue;
			}
			// ensure interrupt is cleared
			pio_interrupt_clear(pio, sm);

			if(debug){
				fast_serial_printf("Tight execution loop ended\r\n");
				uint8_t pc = pio_sm_get_pc(pio, sm);
				fast_serial_printf("Program ended at instr %d\r\n", pc-offset);
			}

			if(get_status() == ABORT_REQUESTED){
				set_status(ABORTING);
				stop_sm(pio, sm, dma_chan);
				set_status(ABORTED);
				if(debug){
					fast_serial_printf("Aborted execution\r\n");
				}
			}
			else{
				set_status(TRANSITION_TO_STOP);
				stop_sm(pio, sm, dma_chan);
				set_status(STOPPED);
				if(debug){
					fast_serial_printf("Execution stopped\r\n");
				}
			}
			if(debug){
				fast_serial_printf("Core1 loop ended\r\n");
			}
		}
		else{
			// manual update
			uint32_t manual_state = command;
			pio_sm_set_pins_with_mask(pio, sm, manual_state, output_mask);
			if(debug){
				fast_serial_printf("Output commanded: %x\r\n", manual_state);
			}
			
		}
	}
}
int main(){

	// initialize status mutex
	mutex_init(&status_mutex);
	
	// Setup serial
	fast_serial_init();

	// By default, set the system clock to 100 MHz
	set_sys_clock_khz(100000, false);

	// Allow the clock to be restarted in case of any errors
	clocks_enable_resus(&clk_resus);

	// Turn on onboard LED (to indicate device is starting)
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, 1);

	// Finish startup
	fast_serial_printf("Prawn Digital Output online\r\n");
	gpio_put(LED_PIN, 0);

	multicore_launch_core1(core1_entry);
    multicore_fifo_pop_blocking();

	// Set status to off
	set_status(STOPPED);


	while(1){
		
		// Prompt for user command
		// PIO runs independently, so CPU spends most of its time waiting here
		gpio_put(LED_PIN, 1); // turn on LED while waiting for user
		unsigned int buf_len = fast_serial_read_until(serial_buf, SERIAL_BUFFER_SIZE, '\n');
		gpio_put(LED_PIN, 0);
		int local_status = get_status();

		// Check for command validity, all are at least three characters long
		if(buf_len < 3){
			fast_serial_printf("Invalid command: %s\r\n", serial_buf);
			continue;
		}

		// // These commands are allowed during buffered execution
		if(strncmp(serial_buf, "ver", 3) == 0) {
			fast_serial_printf("Version: %s\r\n", ver);
		}
		else if(strncmp(serial_buf, "brd", 3) == 0) {
			fast_serial_printf("board: pico%d\r\n", PRAWNDO_PICO_BOARD);
		}
		// Status command: return running status
		else if(strncmp(serial_buf, "sts", 3) == 0){
			fast_serial_printf("run-status:%d clock-status:%d\r\n", local_status, clk_status);
		}
		// Enable debug mode
		else if (strncmp(serial_buf, "deb", 3) == 0) {
			debug = 1;
			fast_serial_printf("ok\r\n");
		}
		// Disable debug mode
		else if (strncmp(serial_buf, "ndb", 3) == 0) {
			debug = 0;
			fast_serial_printf("ok\r\n");
		}
		// Abort command: stop run by stopping state machine
		else if(strncmp(serial_buf, "abt", 3) == 0){
			if(local_status == RUNNING || local_status == TRANSITION_TO_RUNNING){
				set_status(ABORT_REQUESTED);
				fast_serial_printf("ok\r\n");
			}
			else {
				fast_serial_printf("Can only abort when status is 1 or 2\r\n");
			}
		}

		// // These commands can only happen in manual mode
		else if (local_status != ABORTED && local_status != STOPPED){
			fast_serial_printf("Cannot execute command %s during buffered execution.\r\n", serial_buf);
		}
		
		// Clear command: empty the buffered outputs
		else if(strncmp(serial_buf, "cls", 3) == 0){
			do_cmd_count = 0;
			fast_serial_printf("ok\r\n");
		}
		// Run command: start state machine
		else if(strncmp(serial_buf, "run", 3) == 0){
			multicore_fifo_push_blocking(BUFFERED_HWSTART);
			fast_serial_printf("ok\r\n");
		}
		// Software start: start state machine without waiting for trigger
		else if(strncmp(serial_buf, "swr", 3) == 0){
			multicore_fifo_push_blocking(BUFFERED);
			fast_serial_printf("ok\r\n");
		}
		// Manual update of outputs
		else if(strncmp(serial_buf, "man", 3) == 0){
			unsigned int manual_state;
			int parsed = sscanf(serial_buf, "%*s %x", &manual_state);
			if(parsed != 1){
				fast_serial_printf("invalid request\r\n");
			}
			else{
				// bit-shift state up by one to signal manual update
				multicore_fifo_push_blocking(manual_state);
				fast_serial_printf("ok\r\n");
			}
		}
		// Get current output state
		else if(strncmp(serial_buf, "gto", 3) == 0){
			unsigned int all_state = gpio_get_all();
			unsigned int manual_state = (output_mask & all_state) >> OUTPUT_PIN_BASE;
			fast_serial_printf("%x\r\n", manual_state);
		}
		// Set instruction by address
		else if(strncmp(serial_buf, "set", 3) == 0){
			uint32_t addr;
			uint32_t do_cmd_addr;
			uint32_t output;
			uint32_t reps;
			int parsed = sscanf(serial_buf, "%*s %x %x %x", &addr, &output, &reps);
			if (parsed < 3) {
				fast_serial_printf("Invalid instruction\r\n");
			}
			else if (addr >= MAX_INSTR){
				fast_serial_printf("Invalid instruction address %x\r\n", addr);
			}
			// confirm output is valid
			else if(output & ~output_mask){
				fast_serial_printf("Invalid output specification %x\r\n", output);
			}
			// confirm reps is valid
			else if(reps < 5 && reps != 0){
				fast_serial_printf("Reps must be 0 or greater than 4, got %x\r\n", reps);
			}
			else {
				do_cmd_addr = addr * 2;
				do_cmds[do_cmd_addr] = output;
				if(reps != 0){
					// Adjust from the number of 10ns reps 
					// to reps adding onto the base 50 ns pulse width
					reps -= 4;
				}
				do_cmds[do_cmd_addr + 1] = reps;
				// update do_cmd_count if we have increased it
				if(do_cmd_addr+1 > do_cmd_count){
					// +2 to account for zero indexing of addr
					do_cmd_count = do_cmd_addr + 2;
				}
				else if(reps == 0 && addr != 0 && do_cmds[do_cmd_addr-1] == 0){
					// reset if we just set a stop command (two reps=0 commands in a row)
					do_cmd_count = do_cmd_addr + 2;
				}
				fast_serial_printf("ok\r\n");
			}
		}
		// Get instruction at address
		else if(strncmp(serial_buf, "get", 3) == 0){
			uint32_t addr;
			uint32_t output;
			uint32_t reps;
			int parsed = sscanf(serial_buf, "%*s %x", &addr);
			if(parsed < 1){
				fast_serial_printf("Invalid request\r\n");
			}
			else if(addr*2+1 > do_cmd_count){
				fast_serial_printf("Invalid address\r\n");
			}
			else {
				output = do_cmds[addr*2];
				reps = do_cmds[addr*2+1];
				if(reps != 0){
					reps += 4;
				}
				fast_serial_printf("%x %x\r\n", output, reps);
			}
		}
		// Add command: read in hexadecimal integers separated by newlines, 
		// append to command array
		else if(strncmp(serial_buf, "add", 3) == 0){
			
			while(do_cmd_count < MAX_DO_CMDS-3){
				uint32_t output;
				uint32_t reps;
				unsigned short num_inputs = 0;

				do {
				// Read in the command provided by the user
				// FORMAT: <output> <reps> <REPS = 0: Indefinite Wait>
					buf_len = fast_serial_read_until(serial_buf, SERIAL_BUFFER_SIZE, '\n');

				// Check if the user inputted "end", and if so, exit add mode
				if(buf_len >= 3){
					if(strncmp(serial_buf, "end", 3) == 0){
						break; // breaks inner read loop
					}
				}

				// Read the input provided in the serial buffer into the 
				// output, and reps variables. Also storing the return
				// value of sscanf (number of variables successfully read in)
				// to determine if the user wants to program a stop/wait
				num_inputs = sscanf(serial_buf, "%x %x", &output, &reps);

				} while (num_inputs < 2);

				if(strncmp(serial_buf, "end", 3) == 0){
					fast_serial_printf("ok\r\n");
					break; // breaks add mode loop
				}

				//DEBUG MODE:
				// Printing to the user what the program received as input
				// for the output, reps, and optionally wait if the user inputted
				// that
				if (debug) {
					fast_serial_printf("Output: %x\r\n", output);
					fast_serial_printf("Number of Reps: %d\r\n", reps);

					if (reps == 0){
						fast_serial_printf("Wait\r\n");
					}
				}

				// confirm output is valid
				if(output & ~output_mask){
					fast_serial_printf("Invalid output specification %x\r\n", output);
					break;
				}
				// confirm reps is valid
				if(reps < 5 && reps != 0){
					fast_serial_printf("Reps must be 0 or greater than 4, got %x\r\n", reps);
					break;
				}

				// Reading in the 16-bit word to output to the pins
				do_cmds[do_cmd_count] = output;
				do_cmd_count++;

				do_cmds[do_cmd_count] = reps;

				// If reps is not zero, this adjusts them from the number
				// of 10ns reps to reps adding onto the base 50 ns pulse
				// width
				if (do_cmds[do_cmd_count] != 0) {
					do_cmds[do_cmd_count] -= 4;
				}
				do_cmd_count++;
				
			}
			if(do_cmd_count == MAX_DO_CMDS-1){
				fast_serial_printf("Too many DO commands (%d). Please use resources more efficiently or increase MAX_DO_CMDS and recompile.\r\n", MAX_DO_CMDS);
			}
		}
		// Add many command: read in a fixed number of binary integers without separation,
		// append to command array
		else if(strncmp(serial_buf, "adm", 3) == 0){
			// Get how many instructions this adm command contains and where to insert them
			uint32_t start_addr;
			uint32_t inst_count;
			int parsed = sscanf(serial_buf, "%*s %x %x", &start_addr, &inst_count);
			if(parsed < 2){
				fast_serial_printf("Invalid request\r\n");
				continue;
			}
			// Check that the instructions will fit in the do_cmds array
			else if(inst_count + start_addr > MAX_INSTR){
				fast_serial_printf("Invalid address and/or too many instructions (%d + %d).\r\n", start_addr, inst_count);
				continue;
			}
			else{
				fast_serial_printf("ready\r\n");
			}

			// reset do_cmd_count to start_address
			do_cmd_count = start_addr * 2;

			uint32_t reps_error_count = 0;
			uint32_t last_reps_error_idx = 0;

			// It takes 6 bytes to describe an instruction: 2 bytes for values, 4 bytes for time
			uint32_t inst_per_buffer = SERIAL_BUFFER_SIZE / 6;
			// In this loop, we read nearly full serial buffers and load them into do_cmds.
			while(inst_count > inst_per_buffer){
				fast_serial_read(serial_buf, 6*inst_per_buffer);

				for(int i = 0; i < inst_per_buffer; i++){
					do_cmds[do_cmd_count] = (serial_buf[6*i+1] << 8) | serial_buf[6*i];
					do_cmd_count++;
					uint32_t reps = ((serial_buf[6*i+5] << 24)
									 | (serial_buf[6*i+4] << 16)
									 | (serial_buf[6*i+3] << 8)
									 | serial_buf[6*i+2]);
					if(reps < 5 && reps != 0){
						reps_error_count++;
						last_reps_error_idx = (do_cmd_count + 1) / 2;
						reps = 0;
					}
					if(reps != 0){
						reps -= 4;
					}
					do_cmds[do_cmd_count] = reps;
					do_cmd_count++;
				}

				inst_count -= inst_per_buffer;
			}

			// In this if statement, we read a final serial buffer and load it into do_cmds.
			if(inst_count > 0){
				fast_serial_read(serial_buf, 6*inst_count);

				for(int i = 0; i < inst_count; i++){
					do_cmds[do_cmd_count] = (serial_buf[6*i+1] << 8) | serial_buf[6*i];
					do_cmd_count++;
					uint32_t reps = ((serial_buf[6*i+5] << 24)
									 | (serial_buf[6*i+4] << 16)
									 | (serial_buf[6*i+3] << 8)
									 | serial_buf[6*i+2]);
					if(reps < 5 && reps != 0){
						reps_error_count++;
						last_reps_error_idx = (do_cmd_count + 1) / 2;
						reps = 0;
					}
					if(reps != 0){
						reps -= 4;
					}
					do_cmds[do_cmd_count] = reps;
					do_cmd_count++;
				}
			}

			if(reps_error_count > 0){
				fast_serial_printf("Invalid number of reps in %d instructions, most recent error at instruction %d. Setting reps to zero for these instructions.\r\n", reps_error_count, last_reps_error_idx);
			}
			else{
				fast_serial_printf("ok\r\n");
			}
		}
		// Dump command: print the currently loaded buffered outputs
		else if(strncmp(serial_buf, "dmp", 3) == 0){
			// Dump
			for(int i = 0; do_cmd_count > 0 && i < do_cmd_count - 1; i++){
				// Printing out the output word
				fast_serial_printf("do_cmd: %04x\r\n", do_cmds[i]);
				i++;

				// Either printing out the number of reps, or if the number
				// of reps equals zero printing out whether it is a full stop
				// or an indefinite wait
				if (do_cmds[i] == 0){
					fast_serial_printf("\tWait\r\n");
				}
				else {
					fast_serial_printf("\treps: %x\r\n", do_cmds[i]+4);
				}
				
			}
		}
		// Program length command: print number of instructions currently in program
		else if (strncmp(serial_buf, "len", 3) == 0){
			fast_serial_printf("Number of command lines: %d\r\n", do_cmd_count);
			fast_serial_printf("Number of instructions: %d\r\n", do_cmd_count/2);
		}
		// Clk configuration command
		// FORMAT: clk <src:0,1> <freq:int>
		else if (strncmp(serial_buf, "clk", 3) == 0){
			unsigned int src; // 0 = internal, 1 = external (GPIO pin 20)
			unsigned int freq; // in Hz (up to 133 MHz or 150 MHz depending on board)
			int parsed = sscanf(serial_buf, "%*s %u %u", &src, &freq);
			// validation checks of the inputs
			if (parsed < 2) {
				fast_serial_printf("invalid clock request\r\n");
				continue;
			} else if (src > 2) {
				fast_serial_printf("invalid clock source request\r\n");
				continue;
#if PRAWNDO_PICO_BOARD == 1
			} else if (freq > 133000000) {
#elif PRAWNDO_PICO_BOARD == 2
			} else if (freq > 150000000) {
#else
#    error "Unsupported PICO_BOARD"
#endif // PRAWNDO_PICO_BOARD
				fast_serial_printf("invalid clock frequency request\r\n");
				continue;
			}
			// set new clock source and frequency
			if (src == 0) { // internal
				if (set_sys_clock_khz(freq / 1000, false)) {
					fast_serial_printf("ok\r\n");
					clk_status = INTERNAL;
				} else {
					fast_serial_printf("Failure. Cannot exactly achieve that clock frequency\r\n");
				}
			} else { // external
				// update status first, then resus can correct of configuration fails
				clk_status = EXTERNAL;
				clock_configure_gpin(clk_sys, 20, freq, freq);
				fast_serial_printf("ok\r\n");
			}
		}
		// Editing the current command with the instruction provided by the
		// user 
		// FORMAT: <output> <reps> <REPS = 0: Indefinite Wait>
		else if (strncmp(serial_buf, "edt", 3) == 0) {
			if (do_cmd_count > 0) {
				uint32_t output;
				uint32_t reps;
				unsigned short num_inputs;
			
				do {
					// Reading in an instruction from the user serial input
					fast_serial_read_until(serial_buf, SERIAL_BUFFER_SIZE, '\n');

					// Storing the input from the user into the respective output,
					// and reps variables to be stored in memory
					num_inputs = sscanf(serial_buf, "%x %x", &output, &reps);

				} while (num_inputs < 2);
				// Immediately replacing the output and reps stored for the
				// last sequence with the newly inputted values
				do_cmds[do_cmd_count - 2] = output;
				do_cmds[do_cmd_count - 1] = reps;

			} else {
				fast_serial_printf("No commands to edit\r\n");
			}
			fast_serial_printf("ok\r\n");
		
		}
		// Printing out the latest digital output command added to the current 
		// running program
		else if (strncmp(serial_buf, "cur", 3) == 0) {
				fast_serial_printf("Output: %x\r\n", do_cmds[do_cmd_count - 2]);
				fast_serial_printf("Reps: %d\r\n", do_cmds[do_cmd_count - 1] + 4);
				if(do_cmds[do_cmd_count - 1] == 0){
					fast_serial_printf("Wait\r\n");
				}
		}
		// Measure system frequencies
		else if(strncmp(serial_buf, "frq", 3) == 0) {
			measure_freqs();
		}
		// Reboot into programming mode
		else if(strncmp(serial_buf, "prg", 3) == 0) {
			reset_usb_boot(0, 0);
		}
		else{
			fast_serial_printf("Invalid command: %s\r\n", serial_buf);
		}
	}
}
