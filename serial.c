#include "serial.h"

#include "pico/stdio.h"
#include <stdlib.h>
#include <string.h>

unsigned int readline(char * buf, unsigned int buf_len){
	int read_len = 0;
	int in;
	do {
		in = getchar_timeout_us(10000);
		if(in > 0 && in != '\n' && in != '\r'){
			buf[read_len] = (char) in;
			read_len++;
		}
	} while(in != '\n' && read_len < buf_len - 1);
	buf[read_len] = '\0';
	read_len++;
	return read_len;
}
