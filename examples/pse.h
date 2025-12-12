/// PSE HECI Helper Functions

#ifndef _PSE_H_
#define _PSE_H_

#include <linux/uuid.h>
#include <linux/ioctl.h>

#include "heci_types.h"

/// This IOCTL associates the PSE character device with a specific firmware client
///
/// After it has been performed, future reads/writes will be attached to this new client
#define IOCTL_ISHTP_CONNECT_CLIENT _IOWR('H', 0x01, struct ishtp_cc_data)

/// ISHTP Client information returned by IOCTL_ISHTP_CONNECT_CLIENT
struct ishtp_client {
    __u32 max_message_length;
    __u8  protocol_version;
    __u8  reserved[3];
};

/// Union of input/output types of IOCTL_ISHTP_CONNECT_CLIENT
struct ishtp_cc_data {
    union {
        uuid_le             in_client_uuid;
        struct ishtp_client out_client_props;
    };
};

/// Establish a connection to the PSE firmware client
int pse_client_connect(void);

/// Write command data to the PSE firmware client
void pse_send_command(int fd, heci_command_id_t command, uint16_t data, heci_body_t * body);

/// Read a command response from the PSE firmware client
int pse_read_response(int fd, heci_header_t *header, heci_body_t *body);

/// Send a command, check the response status, and return the data
/// Returns 0 on success with an empty body. Returns 1 on success with a populated body
int pse_command_checked(int fd, heci_command_id_t command, uint16_t data, heci_body_t * in_body, heci_body_t * out_body);

#endif /* _PSE_H_ */