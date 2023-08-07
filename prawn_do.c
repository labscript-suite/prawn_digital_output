#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "prawn_do.pio.h"
#include "serial.h"

#define LED_PIN 25

#define MAX_DO_CMDS 32768
uint32_t do_cmds[MAX_DO_CMDS];
uint32_t do_cmd_count;

#define SERIAL_BUFFER_SIZE 256
char serial_buf[SERIAL_BUFFER_SIZE];

#define STATUS_OFF 0
#define STATUS_STARTING 1
#define STATUS_RUNNING 2
#define STATUS_ABORTING 3
int status;

const char ver[6] = '1.0.0'

/*
  Start pio state machine

  This function resets the pio state machine,
  then sets up direct memory access (dma) from the pio to do_cmds.
  Finally, it starts the state machine, which will then run the pio program independently of the CPU.

  This function is inspired by the logic_analyser_arm function on page 46
  of the Raspberry Pi Pico C/C++ SDK manual (except for output, rather than input).
 */
void start_sm(PIO pio, uint sm, uint dma_chan, uint offset){
	pio_sm_restart(pio, sm);
	// Explicitly jump to the beginning of the program to avoid hanging due to missed triggers.
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
						  &pio->txf[sm], // write address is fifo for this pio and state machine
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

int main(){
	// Setup serial
	stdio_init_all();

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
	uint offset = pio_add_program(pio, &prawn_do_program); // load prawn_do PIO program
	// initialize prawn_do PIO program on chosen PIO and state machine at required offset, PIO clock 1/10 system clock
	pio_sm_config pio_config = prawn_do_program_init(pio, sm, 10.f, offset);

	while(1){
		// Prompt for user command
		// PIO runs independently, so CPU spends most of its time waiting here
		printf("> ");
		gpio_put(LED_PIN, 1); // turn on LED while waiting for user
		unsigned int buf_len = readline(serial_buf, SERIAL_BUFFER_SIZE);
		gpio_put(LED_PIN, 0);

		// Quick check for command validity, all are at least three characters long
		if(buf_len < 3){
			printf("Invalid command: %s\n", serial_buf);
			continue;
		}

		// Abort command: stop run by stopping state machine
		if(strncmp(serial_buf, "abt", 3) == 0){
			if(status != STATUS_OFF){ // If already stopped, no need to do anything
				stop_sm(pio, sm, dma_chan);
				status = STATUS_OFF;
			}
		}
		// Clear command: empty the buffered outputs
		else if(strncmp(serial_buf, "cls", 3) == 0){
			if(status == STATUS_OFF){ // Only change the buffered outputs when not running
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
			}
			else{
				printf("Unable to (restart) run while running, please abort (abt) first\n");
			}
		}
		// Add command: read in hexadecimal integers separated by newlines, append to command array
		else if(strncmp(serial_buf, "add", 3) == 0){
			if(status == STATUS_OFF){ // Only add output states while not running
				while(do_cmd_count < MAX_DO_CMDS-1){
					buf_len = readline(serial_buf, SERIAL_BUFFER_SIZE);

					if(buf_len >= 3){
						if(strncmp(serial_buf, "end", 3) == 0){
							break;
						}
					}

					do_cmds[do_cmd_count] = strtoul(serial_buf, NULL, 0);
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
			for(int i = 0; i < do_cmd_count; i++){
				printf("do_cmd #%d\n", i);
				printf("\t0x%08x\n", do_cmds[i]);
			}
		} else if(strncmp(serial_buf, "ver", 3) == 0) {
			printf("Version: %s\n", ver)
		}
		else{
			printf("Invalid command: %s\n", serial_buf);
		}
	}
}
