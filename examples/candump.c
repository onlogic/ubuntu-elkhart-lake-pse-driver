/// @file: candump.c
/// @author: Jacob Caughfield <jacob.caughfield@onlogic.com>
///          Jonathan Liang <jonathan.liang@onlogic.com>
/// @brief: Receive CAN messages via the PSE 

#include <stdio.h>
#include <unistd.h> // close
#include <endian.h> // htobe32, be32toh
#include <signal.h>
#include "pse.h" // pse_client_connect, pse_send_command, pse_read_response, heci_types

static volatile int running = 1;
static volatile sig_atomic_t signal_num;

static void sigterm(int signo)
{
	running = 0;
	signal_num = signo;
}

/// Disable the can device
static int can_close(int fd, uint8_t device) {
    can_command_t command = {
        .op = kCAN_Disable,
        .dev = device
    };

    if(pse_command_checked(fd, kHECI_CAN_COMMAND, *(uint16_t *)&command, NULL, NULL) < 0) {
        printf("Failed to close the CAN device\n");
        return -1;
    }

    return 0;
}

/// Set the CAN baudrate and enable the device
static int can_open(int fd, uint8_t device, uint16_t baudrate) {
    can_command_t command = {
        .op = kCAN_SetBaudrate,
        .dev = device,
        .arg = baudrate,
    };

    // Set the baudrate
    if(pse_command_checked(fd, kHECI_CAN_COMMAND, *(uint16_t *)&command, NULL, NULL) < 0) {
        printf("Could not set the CAN baudrate\n");
        return -1;
    }

    // Open the CAN device
    command.op = kCAN_Enable;
    if(pse_command_checked(fd, kHECI_CAN_COMMAND, *(uint16_t *)&command, NULL, NULL) < 0) {
        printf("Could not open the CAN device\n");
        return -1;
    }

    return 0;
}

/// Receive a CAN (data) frame
static int can_recv(int fd, uint8_t device) {
    int ret;
    heci_body_t body;
    heci_can_data_t * frame;
    char data[18];

    can_command_t command = {
        .op = kCAN_Read,
        .dev = device
    };

    ret = pse_command_checked(fd, kHECI_CAN_COMMAND, *(uint16_t *)&command, NULL, &body);

    if (ret < 0) {
        // Failed to request a CAN frame from the PSE
        return 0;
    } else if (ret != 1) {
        // Read a CAN frame from the PSE, but did not receive any data\n");
        return 0;
    }

    frame = (heci_can_data_t *)body.data;

    // Nicely format data
    for (uint8_t i = 0; i < frame->length; i++) {
        if (i < 4) {
            sprintf(data + (i * 2), "%02X", *((uint8_t *)&frame->data_word_0 + i));
        } else {
            sprintf(data + (i * 2), "%02X", *((uint8_t *)&frame->data_word_1 + i - 4));
        }
    }

    printf("%08X %X %s\n", frame->id, frame->length, data);

    return 0;
}

int main(void) {
    int fd;
    int ret;
    uint8_t candev = 0;

    signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);

    fd = pse_client_connect();

    if (fd <= 0) {
        printf("Failed to establish a connection with the PSE\n");
        return -1;
    }

    if(can_open(fd, candev, 1000)) {
        close(fd);
        return -1;
    }

    while (running) {
        can_recv(fd, candev);
    }

    can_close(fd, candev);
    close(fd);

    if (signal_num)
		return 128 + signal_num;
    
    return 0;
}
