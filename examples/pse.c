/// @file: pse.c
/// @author: Jacob Caughfield <jacob.caughfield@onlogic.com>
/// @brief: Core connection and transmission functions for PSE communications

#include <fcntl.h> // open
#include <unistd.h> // close
#include <linux/types.h> // FD_X

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioctl.h> // ioctl
#include <sys/time.h> // timeval

#include "pse.h" // uuid, pse and heci types

/// PSE Character device file
#define PSE_CHRDEV "/dev/pse"

/// Tranmsit data buffer
static uint8_t heci_tx_buffer[256];
static uint8_t heci_rx_buffer[256];

/// Open a connection to the PSE device over ISHTP/HECI
///
/// By default the connection over ISHTP to the firmware client will be closed;
/// after this connection IOCTL is sent, read/write commands can have an effect.
int pse_client_connect(void) {
    int fd, ret;
    struct ishtp_cc_data cc_data;

    // Prep input connection data
    memcpy(&(cc_data.in_client_uuid), &pse_smhi_guid, sizeof(pse_smhi_guid));

    // Open the pse character device for operations
    fd = open(PSE_CHRDEV, O_RDWR);
    if (fd <= 0) {
        printf("Failed to open the pse device file\nAre you running as root?\n");
        return fd;
    }

    // Send the connection IOCTL
    ret = ioctl(fd, IOCTL_ISHTP_CONNECT_CLIENT, &cc_data);
    if (ret) {
        printf("Failed to connect to the PSE over ISHTP/HECI\n");
        return ret;
    }

    return fd;
}

/// Send a command to the PSE over ISHTP/HECI
///
/// @fd: The open pse character file
/// @command: The command-kind identifier
/// @data: The packed 16-bit header argument
/// @body: Extended data/message body
void pse_send_command(int fd, heci_command_id_t command, uint16_t data, heci_body_t * body) {
    size_t len;

    // Create the initial header
    heci_header_t header = {
        .status = 0,
        .is_response = 0,
        .has_next = body != NULL ? 1 : 0,
        .command = (uint8_t)command,
        .argument = data
    };

    // Copy the header into the transmit buffer
    memcpy(heci_tx_buffer, &header, sizeof(heci_header_t));

    // Add a body to the command if one is present
    if (header.has_next) {
        memcpy(heci_tx_buffer + sizeof(heci_header_t), body, sizeof(heci_body_t));

        len = sizeof(heci_header_t) + sizeof(heci_body_t);
    } else {
        len = sizeof(heci_header_t);
    }

    write(fd, heci_tx_buffer, len);
}

/// Read a response from the PSE over ISHTP/HECI
///
/// @fd: The open pse character file
/// @header: Pointer that will be updated to an heci header struct
/// @body: Pointer that will be updated to an heci body struct (may be NULL)
int pse_read_response(int fd, heci_header_t *header, heci_body_t *body) {
    int length = -1;
    uint8_t * data = heci_rx_buffer;

    fd_set pse_file_set;
    struct timeval timeout = {
        .tv_sec = 10,
        .tv_usec = 0
    };

    // Read from the firmware
    FD_ZERO(&pse_file_set);
    FD_SET(fd, &pse_file_set);
    select(fd + 1, &pse_file_set, NULL, NULL, &timeout);

    if (FD_ISSET(fd, &pse_file_set)) {
        // Read the message header
        length = read(fd, data, sizeof(heci_header_t));
        if (length <= 0 || length != sizeof(heci_header_t)) {
            printf("Failed reading header from the pse file (%i)\n", length);
            return length;
        }

        memcpy(header, data, sizeof(heci_header_t));

        // If the header has followup data, read it
        if (header->has_next && !body) {
            printf("Warning: Returned body data was dropped!\n");
            return length;
        } else if (header->has_next) {
            length = read(fd, data + sizeof(heci_header_t), sizeof(heci_body_t));
            if (length <= 0 || length != sizeof(heci_body_t)) {
                printf("Failed reading body from the pse file (%i)\n", length);
                return length;
            } 

            memcpy(body, data + sizeof(heci_header_t), sizeof(heci_body_t));

            length = sizeof(heci_body_t) + sizeof(heci_header_t);
        } else {
            length = sizeof(heci_header_t);
        }
    }

    return length;
}

/// Send a command and check the returned status
///
/// Returns 0 on success with an empty body. Returns 1 on success with a populated body
///
/// @fd: The open pse character file
/// @command: The command-kind identifier
/// @data: The packed 16-bit header argument
/// @in_body: Extended data/message body
/// @out_body: Store the response data
int pse_command_checked(int fd, heci_command_id_t command, uint16_t data, heci_body_t * in_body, heci_body_t * out_body) {
    heci_header_t header;
    header.status = 1;

    pse_send_command(fd, command, data, in_body);

    if (pse_read_response(fd, &header, out_body) < 0) {
        return -1;
    }

    if (header.status) {
        return -1 * header.status;
    }

    return header.has_next;
}