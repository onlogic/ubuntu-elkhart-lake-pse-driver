/// @file: can.c
/// @author: Jacob Caughfield <jacob.caughfield@onlogic.com>
/// @brief: Send and receive CAN messages via the PSE 

#include <stdio.h>
#include <unistd.h> // close
#include <endian.h> // htobe32, be32toh

#include "pse.h" // pse_client_connect, pse_send_command, pse_read_response, heci_types

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

/// Send a can (data) frame
static int can_send(int fd, uint8_t device, uint32_t id, uint8_t length, uint32_t data_0, uint32_t data_1) {
    can_command_t command = {
        .op = kCAN_Write,
        .dev = device,
    };

    heci_body_t body = {
        .kind = kHeciData_Can,
        .length = sizeof(heci_can_data_t),
    };

    heci_can_data_t *frame = (heci_can_data_t *)(body.data);

    // Sanity checks
    if (length > 8) {
        printf("The maximum CAN frame length is 8 bytes\n");
        return -1;
    }

    if (id > 0x1FFFFFFF) {
        printf("The message ID exceded the maximum CAN frame ID\n");
        return -1;
    }

    // Build the can frame
    frame->id = id;
    frame->id_type = id < 0x7FF ? 0 : 1;
    frame->frame_type = 0;
    frame->length = length;
    frame->data_word_0 = htobe32(data_0);
    frame->data_word_1 = htobe32(data_1);

    // Send the CAN frame
    if (pse_command_checked(fd, kHECI_CAN_COMMAND, *(uint16_t *)&command, &body, NULL) < 0) {
        printf("Failed to send the CAN frame\n");
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
        printf("Failed to request a CAN frame from the PSE (%i)\n", ret);
        return ret;
    } else if (ret != 1) {
        printf("Read a CAN frame from the PSE, but did not receive any data\n");
        return -1;
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

    fd = pse_client_connect();

    if (fd <= 0) {
        printf("Failed to establish a connection with the PSE\n");
        return -1;
    }

    if(can_open(fd, candev, 1000)) {
        close(fd);
        return -1;
    }

    ret = can_send(fd, candev, 0x123, 8, 0x11223344, 0x55667788);

    if (!ret) {
        // Read all CAN frames from the PSE buffer
        while((ret = can_recv(fd, candev)) == 0) { ; }
    }

    can_close(fd, candev);
    close(fd);
    
    return ret;
}