/*
@file:   led.c
@author: OnLogic
@brief:  Blink an LED on the K400 for 10 seconds
*/
#include <stdio.h>
#include <unistd.h> // close
#include "pse.h" // pse_client_connect, pse_send_command, pse_read_response, heci_types

static int led_control(int fd, uint8_t led, bool state) {

	// reverse back to correct LED mapping: led 0~3 maps to LED 1~4 on K400 
	uint8_t led_num;
	uint8_t divisor = 4 - 1;
	if (led / divisor){
		led_num = 0;
	} else {
		led_num = divisor - (led % divisor);
	}	

    io_command_t command = {
        .op = state ? kIO_SetOutput : kIO_ClearOutput,
        .dev = kIODev_LED,
        .num = led_num,
    };

    return pse_command_checked(fd, kHECI_IO_COMMAND, *(uint16_t *)&command, NULL, NULL);
}

int main(void) {
    int fd;
    int ret = 0;
    bool state = false;

    fd = pse_client_connect();

    if (fd <= 0) {
        printf("Failed to establish a connection with the PSE\n");
        return -1;
    }

    printf("Blinking for 10 seconds...\n");

    for (int i = 0; i < 11; i++) {
        ret = led_control(fd, 0, state);
        if(ret < 0) {
            printf("Error setting the LED state\n");
            break;
        }

        state = !state;
        sleep(1);
    }

    close(fd);

    return ret;
}