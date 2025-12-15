/// @file: automotive.c
/// @author: Jacob Caughfield <jacob.caughfield@onlogic.com>
/// @brief: Get and set system automotive features

#include <stdio.h>
#include <unistd.h> // close
#include <string.h> // sprintf

#include "pse.h" // pse_client_connect, pse_send_command, pse_read_response, heci_types

typedef enum _automotive_config_t {
    kAM_IgnitionSense,
    kAM_LowPowerMode,
    kAM_ShutdownTimer,
    kAM_HardOffTimer,
    kAM_StartupTimer,
    kAM_ShutdownVoltage,
    kAM_LowVoltageShutdownTimer,
    kAM_CurrentInputVoltage,
    kAM_NumAutomotiveConfigs
} automotive_config_t;

/// Short names consumed by the automotive mode controller
const char g_config_short_names[kAM_NumAutomotiveConfigs][4] = {
    "amd\0", "lpe\0", "sot\0", "hot\0", "sut\0", "sdv\0", "lvt\0", "cvl\0"
};

/// Friendly names for printing the entire configuration list
const char * g_config_long_names[kAM_NumAutomotiveConfigs] = {
    "Automotive Mode Enabled",
    "Low Power Mode Enabled",
    "Shutdown Timer",
    "Hard Off Timer",
    "Startup Timer",
    "Shutdown Voltage",
    "Low Voltage Off Timer",
    "Current Input Voltage",
};

#define AMD_UART_DEV     4
#define AMD_CONFIG_GET  "cfg get %s\r\a"
#define AMD_CONFIG_SET  "cfg set %s %u\r"
#define AMD_VERSION_GET "ver\r\a"
#define AMD_MIN_VERSION  123

// #define SET_SAMPLE_VALUES

/// Get the automotive controller firmware version
static int get_version(int fd, uint32_t * version) {
    int ret;
    heci_body_t body;

    uart_command_t command = {
        .read_write = kUART_Transfer,
        .device = AMD_UART_DEV
    };


    sprintf(body.data, AMD_VERSION_GET);
    body.length = strlen(body.data);
    body.kind = kHeciData_Uart;

    ret = pse_command_checked(fd, kHECI_UART_COMMAND, *(uint16_t *)&command, &body, &body);
    
    if (ret < 0) {
        printf("Error sending the automotive configuration\n");
        return ret;
    } else if (ret == 0) {
        printf("Error reading back the configuration value\n");
        return -1;
    }

    *version = strtoul(body.data, NULL, 10);

    return 0;
}

/// Read an automotive configuration value over uart
static int get_configuration(int fd, automotive_config_t config, uint32_t * value) {
    int ret;
    heci_body_t body;

    uart_command_t command = {
        .read_write = kUART_Transfer,
        .device = AMD_UART_DEV
    };

    // Build the command string
    sprintf(body.data, AMD_CONFIG_GET, g_config_short_names[config]);
    body.length = strlen(body.data);
    body.kind = kHeciData_Uart;

    ret = pse_command_checked(fd, kHECI_UART_COMMAND, *(uint16_t *)&command, &body, &body);
    
    if (ret < 0) {
        printf("Error sending the automotive configuration\n");
        return ret;
    } else if (ret == 0) {
        printf("Error reading back the configuration value\n");
        return -1;
    }

    *value = strtoul(body.data + 4, NULL, 10);

    // Check for invalid zero
    if (*value == 0 && body.data[4] != '0') {
        printf("Could not parse a valid unsigned int from `%s`\n", body.data);
        return -2;
    }

    return 0;
}

/// Program an automotive configuration value over uart
static int set_configuration(int fd, automotive_config_t config, uint32_t value) {
    int ret;
    heci_body_t body;

    uart_command_t command = {
        .read_write = kUART_Write,
        .device = AMD_UART_DEV
    };

    sprintf(body.data, AMD_CONFIG_SET, g_config_short_names[config], value);
    body.length = strlen(body.data);
    body.kind = kHeciData_Uart;

    ret = pse_command_checked(fd, kHECI_UART_COMMAND, *(uint16_t *)&command, &body, NULL);
    if ( ret < 0) {
        printf("Failed to program the configuration value (%i)\n", ret);
        return ret;
    }

    // !Important: Wait for programming and storing the setting to complete
    usleep(10 * 1000);

    return 0;
}

/// Program an automotive configuration and validate that it worked
static int set_configuration_checked(int fd, automotive_config_t config, uint32_t value) {
    int ret;
    uint32_t r_value;

    ret = set_configuration(fd, config, value);
    if (ret) { return ret; }

    ret = get_configuration(fd, config, &r_value);
    if (ret) { return ret; }

    if (r_value != value) {
        printf("Failed to set the value of %s to %u (got %u)\n", g_config_long_names[config], value, r_value);
        return -1;
    }

    printf("%s --> %u\n", g_config_long_names[config], r_value);
    return 0;
}

/// Print the entire bank of automotive configuration values
static int show_configuration(int fd) {
    int ret = 0;
    uint32_t value;

    for (int i = 0; i < kAM_NumAutomotiveConfigs; i++) {
        if (get_configuration(fd, i, &value)) {
            printf("Failed to get the value for `%s`\n", g_config_long_names[i]);
            ret = -1;
            continue;
        }

        printf("%-25s %u\n", g_config_long_names[i], value);
    }

    return ret;
}

int main(void) {
    int fd;
    int ret;
    uint32_t version;

    fd = pse_client_connect();

    if (fd <= 0) {
        printf("Failed to establish a connection with the PSE\n");
        return -1;
    }

    // Check the automotive controller firmware version
    ret = get_version(fd, &version);
    if (ret) {
        close(fd);
        return ret;
    }

    printf("Firmware Version: %u\n\n", version);
    if (version < AMD_MIN_VERSION) {
        printf("Automotive controller firmware is out of date!\n");
        close(fd);
        return -1;
    }

    // Print all of the current configuration values
    printf("Current Configuration:\n\n");
    ret = show_configuration(fd);

    // define SET_SAMPLE_VALUES above to program some configuration values:
    #ifdef SET_SAMPLE_VALUES
    if (!ret) {
        // Set the shutdown timer to 20 seconds
        printf("Update Shutdown Timer:\n");
        ret = set_configuration_checked(fd, kAM_ShutdownTimer, 20);
    }

    if (!ret) {
        // Set the shutdown voltage to 9.5V
        printf("Update Shutdown Voltage:\n");
        ret = set_configuration_checked(fd, kAM_ShutdownVoltage, 950);
    }
    #endif

    close(fd);

    return ret;
}