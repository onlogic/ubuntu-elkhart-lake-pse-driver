/*
@file:   dio.c
@author: OnLogic
@brief:  Simple write-read scan at 0.05 Second intervals through all available DIO ports. 
         First sweep sets all outputs to 0 (active-low), second sweep resets all to default 1.
*/

#include <stdio.h>
#include <unistd.h> // close

#include "pse.h" // pse_client_connect, pse_send_command, pse_read_response, heci_types

static int set_digital_output(int fd, uint8_t d_out_pin, bool state) {
    // format correct io command struct for do operation
    io_command_t command = {
        .op = state ? kIO_SetOutput : kIO_ClearOutput,
        .dev = kIODev_DO,
        .num = d_out_pin,
    };

    return pse_command_checked(fd, kHECI_IO_COMMAND, *(uint16_t *)&command, NULL, NULL);
}

static uint8_t get_digital_input(int fd, uint8_t d_in_pin) {
    // format correct io command struct for di operation
    io_command_t command = {
        .op = kIO_GetInfo,
        .dev = kIODev_DI,
        .num = d_in_pin,
    };

    // reception data structures
    heci_dio_info_t * dio_info;
    heci_body_t body;
 
    // init to -1 to indicate any errors
    int pin = -1; 

    // issue command and get input
    int ret = pse_command_checked(fd, kHECI_IO_COMMAND, *(uint16_t *)&command, NULL, &body);
    if (ret < 0) {
        printf("Could not read DIO info: %i\n", ret);
        return ret;
    } else if (ret == 0) {
        printf("No DIO info returned from the PSE\n");
        return -1;
    }

    // extract data from heci struct and return
    dio_info = (heci_dio_info_t *)body.data;
    return dio_info->state;
}

int main(void) {
    int fd;
    int ret = 0;
    bool state = true;

    // init and open client connection
    fd = pse_client_connect();

    if (fd <= 0) {
        printf("Failed to establish a connection with the PSE\n");
        return -1;
    }

    // 16 total operations, GPIO high then low
    int PIN_OPS = 16; 
    uint8_t val, pin_idx;

    for (int i = 0; i < PIN_OPS; ++i) {
        // loop indices 0-7 and reverse ouput state per loop
        int pin_idx = i % 8;
        if (pin_idx == 0) {
            state = !state;
        }

        // set output w/ error check
        if(!set_digital_output(fd, pin_idx, state))
            printf("Set Output Pin Number %d to %d\n",pin_idx, state);
        else 
            printf("Error setting the pin output\n");
        
        usleep(50000);

        // read input w/ error check
        val = get_digital_input(fd, pin_idx);
        if (val == 0 || val == 1) 
            printf("Read Input Pin Number %d, value: %d\n", pin_idx, val);
        else
            printf("Error Reading the input pin occurred\n");

        usleep(50000);
    }

    close(fd);
    return ret;
}
