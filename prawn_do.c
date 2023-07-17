#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/clocks.h"



#include "prawn_do.pio.h"
#include "serial.h"

#define LED_PIN 25

#define MAX_DO_CMDS 60000
uint32_t do_cmds[MAX_DO_CMDS];
uint32_t do_cmd_count = 0;


#define SERIAL_BUFFER_SIZE 256
char serial_buf[SERIAL_BUFFER_SIZE];

#define STATUS_OFF 0
#define STATUS_STARTING 1
#define STATUS_RUNNING 2
#define STATUS_ABORTING 3

#define INTERNAL 0
#define EXTERNAL 1
int status;
int clk_status = INTERNAL;
unsigned short wait = 0;
unsigned short end = 0;


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
void start_sm(PIO pio, uint sm, uint dma_chan, uint offset){
	pio_sm_set_enabled(pio, sm, false);

	// Clearing the FIFOs and restarting the state machine to prevent old
	// instructions from persisting into future runs
	pio_sm_clear_fifos(pio, sm);
	pio_sm_restart(pio, sm);
	// Explicitly jump to the beginning of the program to avoid hanging due 
	// to missed triggers.
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

void clk_resus(void) {
	set_sys_clock_khz(100000, false);

	gpio_set_function(20, GPIO_FUNC_NULL);

	clk_status = INTERNAL;

	stdio_init_all();

	printf("System Clock Resus'd\n");
}

int main(){

	// Setup serial
	stdio_init_all();

	clocks_init();

	set_sys_clock_khz(100000, false);

	clocks_enable_resus(&clk_resus);

	// Turn on onboard LED (to indicate device is starting)
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, 1);

	// Finish startup
	printf("Prawn Digital Output online\n");
	gpio_put(LED_PIN, 0);

	// Set status to off
	status = STATUS_OFF;

	// Setup PIO
	PIO pio = pio0;
	uint sm = pio_claim_unused_sm(pio, true);
	uint dma_chan = dma_claim_unused_channel(true);
	uint offset = pio_add_program(pio, &prawn_do_program); // load prawn_do PIO 
														   // program
	// initialize prawn_do PIO program on chosen PIO and state machine at 
	// required offset
	pio_sm_config pio_config = prawn_do_program_init(pio, sm, 1.f, offset);

	while(1){
		// Prompt for user command
		// PIO runs independently, so CPU spends most of its time waiting here
		printf("> ");
		gpio_put(LED_PIN, 1); // turn on LED while waiting for user
		unsigned int buf_len = readline(serial_buf, SERIAL_BUFFER_SIZE);
		gpio_put(LED_PIN, 0);

		// Check for command validity, all are at least three characters long
		if(buf_len < 3){
			printf("Invalid command: %s\n", serial_buf);
			continue;
		}

		// Abort command: stop run by stopping state machine
		if(strncmp(serial_buf, "abt", 3) == 0){
			if(status != STATUS_OFF){ // If already stopped, don't do anything
				if(pio_sm_get_tx_fifo_level(pio, sm)) {
					printf("Sequence Not Fully Completed\n");
				} else {
					printf("Sequence Successfully Completed\n");
				}
				stop_sm(pio, sm, dma_chan);
				status = STATUS_OFF;
			}
		}
		// Clear command: empty the buffered outputs
		else if(strncmp(serial_buf, "cls", 3) == 0){
			if(status == STATUS_OFF){ // Only change the buffered outputs 
									  // when not running
				do_cmd_count = 0;
			}
			else{
				printf("Unable to clear while running, please abort (abt) first\n");
			}
		}
		// Run command: start state machine
		else if(strncmp(serial_buf, "run", 3) == 0){
			if(status == STATUS_OFF){ // Only start running when not running
				start_sm(pio, sm, dma_chan, offset);
				status = STATUS_RUNNING;
				end = 0;
			}
			else{
				printf("Unable to (restart) run while running, please abort (abt) first\n");
			}
		}
		// Add command: read in hexadecimal integers separated by newlines, 
		// append to command array
		else if(strncmp(serial_buf, "add", 3) == 0){
			
			if(status == STATUS_OFF){ // Only add output states when not running
				while(do_cmd_count < MAX_DO_CMDS-3){
					uint32_t output;
					uint32_t reps;
					uint32_t wait_num;
				
					buf_len = readline(serial_buf, SERIAL_BUFFER_SIZE);

					if(buf_len >= 3){
						if(strncmp(serial_buf, "end", 3) == 0){
							break;
						}
					}

					sscanf(serial_buf, "%x %x %d", &output, &reps, &wait_num);
					
					// Removing the appended zero to not take up unecessary
					// space (unless the prior command was a wait)
					if(!wait && do_cmd_count > 0) {
						do_cmd_count--;
					} else {
						wait = 0;
					}

					// Reading in the 16-bit word to output to the pins
					do_cmds[do_cmd_count] = output;
					do_cmd_count++;

					do_cmds[do_cmd_count] = reps;

					if (do_cmds[do_cmd_count] != 0) {
						do_cmds[do_cmd_count] -= 4;
					}
					do_cmd_count++;

					// If reps = 0, then this reads another integer
					// If the number is zero, that signifies a full stop,
					// otherwise this will cause an indefitine wait
					if(reps == 0) {
						do_cmds[do_cmd_count] = wait_num;
						wait = 1;
						do_cmd_count++;

					// Adding on a zero to the end of the instructions to be
					// outputted to the pins to make sure it goes low at the end	
					} else {
						do_cmds[do_cmd_count] = 0;
						do_cmd_count++;
					}
					
				}
				if(do_cmd_count == MAX_DO_CMDS-1){
					printf("Too many DO commands (%d). Please use resources more efficiently or increase MAX_DO_CMDS and recompile.\n", MAX_DO_CMDS);
				}
			}
			else{
				printf("Unable to add while running, please abort (abt) first\n");
			}
		}
		// Add multiple command
		else if(strncmp(serial_buf, "adm", 3) == 0){
			if(status == STATUS_OFF){ // Only add output states when not running
				while(do_cmd_count < MAX_DO_CMDS-3){
					unsigned int num_inputs = 0;
					unsigned int num_pulses = 0;
					uint32_t output;
					uint32_t reps;
					uint32_t waits;
				
					buf_len = readline(serial_buf, SERIAL_BUFFER_SIZE);

					if(buf_len >= 3){
						if(strncmp(serial_buf, "end", 3) == 0){
							break;
						}
					}

					num_inputs = sscanf(serial_buf, "%x %x %d %x", &output, &reps, &num_pulses, &waits);

					// Removing the appended zero to not take up unecessary
					// space (unless the prior command was a wait)
					if(!wait && do_cmd_count > 0) {
						do_cmd_count--;
					} else {
						wait = 0;
					}

					reps -= 4;
					if (num_inputs < 4) {
						waits = reps;
					} else {
						waits -= 4;
					}
					
					while(num_pulses > 0) {
						do_cmds[do_cmd_count] = output;
						do_cmd_count++;

						do_cmds[do_cmd_count] = reps;
						do_cmd_count++;

						do_cmds[do_cmd_count] = 0;
						do_cmd_count++;

						do_cmds[do_cmd_count] = waits;
						do_cmd_count++;

						num_pulses--;
					}
					
					do_cmds[do_cmd_count] = 0;
					do_cmd_count++;
				}
				if(do_cmd_count == MAX_DO_CMDS-1){
					printf("Too many DO commands (%d). Please use resources more efficiently or increase MAX_DO_CMDS and recompile.\n", MAX_DO_CMDS);
				}
			}
			else{
				printf("Unable to add while running, please abort (abt) first\n");
			}
		}
		// Dump command: print the currently loaded buffered outputs
		else if(strncmp(serial_buf, "dmp", 3) == 0){
			// Dump
			int i = 0;
			while(i < do_cmd_count) {
				printf("%d\n", do_cmds[i]);
				i++;
			}
			for(int i = 0; do_cmd_count > 0 && i < do_cmd_count - 1; i++){
				printf("do_cmd: \n");
				printf("\t%04x\n", do_cmds[i]);
				i++;

				
				if(do_cmds[i] != 0) {
					printf("number of reps:");
					printf("\t%d\n", do_cmds[i] + 4);
				} else {
					i++;
					if(do_cmds[i]) {
						printf("Indefinite Wait\n");
					} else {
						printf("Full Stop\n");
					}
				}
			}
		} else if (strncmp(serial_buf, "clk", 3) == 0) {
			if (strncmp(serial_buf + 4, "ext", 3) == 0) {
				// Sync the clock with the input from gpio pin 20
				clock_configure_gpin(clk_sys, 20, 100000000, 100000000);
				clk_status = EXTERNAL;
			} else if (strncmp(serial_buf + 4, "int", 3) == 0) {
				set_sys_clock_khz(100000, false);
				gpio_set_function(20, GPIO_FUNC_NULL);
				clk_status = INTERNAL;
			} else if (strncmp(serial_buf + 4, "set", 3) == 0) {
				unsigned int clock_freq;
				sscanf(serial_buf + 8, "%d", &clock_freq);
				if (clk_status == INTERNAL) {
					set_sys_clock_khz(clock_freq / 1000, false);
				} else {
					clock_configure_gpin(clk_sys, 20, clock_freq, clock_freq);
				}
			}
		} else if (strncmp(serial_buf, "edt", 3) == 0) {
			if (do_cmd_count > 0) {
				uint32_t output;
				uint32_t reps;
				uint32_t waits;
			
				readline(serial_buf, SERIAL_BUFFER_SIZE);

				sscanf(serial_buf, "%x %x %x", &output, &reps, &waits);

				do_cmds[do_cmd_count - 3] = output;
				do_cmds[do_cmd_count - 2] = reps;

				if (output == 0 && reps == 0) {
					do_cmds[do_cmd_count - 1] = waits;
					wait = 1;
				} else {
					do_cmds[do_cmd_count - 1] = 0;
				}
			}
		} else if (strncmp(serial_buf, "cur", 3) == 0) {
			printf("Output: %x\n", do_cmds[do_cmd_count - 3]);
			printf("Reps: %x\n", do_cmds[do_cmd_count - 2] + 4);

			if (do_cmds[do_cmd_count - 3] == 0 && do_cmds[do_cmd_count - 2] == 0) {
				if (do_cmds[do_cmd_count - 1]) {
					printf("Indefinite Wait\n");
				} else {
					printf("Full Stop\n");
				}
			}
		}
		else{
			printf("Invalid command: %s\n", serial_buf);
		}
	}
}
