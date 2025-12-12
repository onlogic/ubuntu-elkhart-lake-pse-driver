/// PSE HECI ISHTP Device Driver Header

#ifndef _PSE_H_
#define _PSE_H_

#include <linux/uuid.h>
#include <linux/ioctl.h>
#include <linux/version.h>

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION( 6, 4, 0 ) )
  #include <linux/mei_uuid.h>
  #include "intel-ish-hid-linux-6.5/ishtp-hid.h"
  #include "intel-ish-hid-linux-6.5/ishtp/ishtp-dev.h"
  #include "intel-ish-hid-linux-6.5/ishtp/client.h"
  #define NEWER_KENREL  1
#else
  #include "intel-ish-hid-5.15/ishtp-hid.h"
  #include "intel-ish-hid-5.15/ishtp/ishtp-dev.h"
  #include "intel-ish-hid-5.15/ishtp/client.h"
  #define NEWER_KENREL  0
#endif

/// This IOCTL associates the PSE character device with a specific firmware client
///
/// After it has been performed, future reads/writes will be attached to this new client
#define IOCTL_ISHTP_CONNECT_CLIENT _IOWR('H', 0x01, struct ishtp_cc_data)

#define UUID_LE_g(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7)		\
((guid_t)								\
{{ (a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff, \
   (b) & 0xff, ((b) >> 8) & 0xff,					\
   (c) & 0xff, ((c) >> 8) & 0xff,					\
   (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) }})

/// ISHTP Client information returned by IOCTL_ISHTP_CONNECT_CLIENT
struct ishtp_client {
    __u32 max_message_length;
    __u8  protocol_version;
    __u8  reserved[3];
};

/// Union of input/output types of IOCTL_ISHTP_CONNECT_CLIENT
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION( 6, 4, 0 ) )
struct ishtp_cc_data {
    union {
        const guid_t           in_client_uuid;
        struct ishtp_client out_client_props;
    };
};
#else
struct ishtp_cc_data {
    union {
        uuid_le             in_client_uuid;
        struct ishtp_client out_client_props;
    };
};
#endif
#endif /* _PSE_H_ */