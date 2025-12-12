/// @file: version.c
/// @author: Jacob Caughfield <jacob.caughfield@onlogic.com>
/// @brief: Read the firmware version from the PSE endpoint

#include <stdio.h>
#include <unistd.h> // close

#include "pse.h" // pse_client_connect, pse_send_command, pse_read_response, heci_types

static int get_version(int fd) {
    int ret;
    heci_body_t body;
    heci_version_t * version;

    ret = pse_command_checked(fd, kHECI_SYS_INFO, 0, NULL, &body);

    if (ret < 0) {
        printf("Could not read the version information: %i\n", ret);
        return ret;
    } else if (ret == 0) {
        printf("No version data returned from the PSE\n");
        return -1;
    }

    version = (heci_version_t *)body.data;

    printf("Version: %u.%u.%u.%u\n", 
        version->major, version->minor, version->hotfix, version->build);

    return 0;
}

int main(void) {
    int fd;
    int ret;

    fd = pse_client_connect();

    if (fd <= 0) {
        printf("Failed to establish a connection with the PSE\n");
        return -1;
    }

    ret = get_version(fd);

    close(fd);

    return ret;
}