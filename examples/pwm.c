/// @file: pwm.c
/// @author: Jonathan Liang <jonathan.liang@onlogic.com>
/// @brief: Start PWM ch0 with fix period and pulse width for 10 seconds

#include <stdio.h>
#include <unistd.h> // close

#include "pse.h" // pse_client_connect, pse_send_command, pse_read_response, heci_types

static int pwm_set_cycle(int fd, uint8_t device, const uint64_t period_us, const int duty_p) {
    pwm_command_t command = {
        .op = kPWM_SetCycles,
        .dev = device
    };

    heci_body_t body = {
        .kind = kHeciData_Pwm,
        .length = sizeof(heci_pwm_data_t),
    };

    heci_pwm_data_t *cycle = (heci_pwm_data_t *)(body.data);
    cycle->period_usec = period_us;
    cycle->pulse_usec = period_us * duty_p / 100;

    if(pse_command_checked(fd, kHECI_PWM_COMMAND, *(uint16_t *)&command, &body, NULL) < 0) {
        printf("Failed to set PWM device\n");
        return -1;
    }

    return 0;
}

static int pwm_start(int fd, uint8_t device) {
    pwm_command_t command = {
        .op = kPWM_Start,
        .dev = device
    };

    if(pse_command_checked(fd, kHECI_PWM_COMMAND, *(uint16_t *)&command, NULL, NULL) < 0) {
        printf("Failed to start the PWM device\n");
        return -1;
    }
    
    return 0;
}

static int pwm_stop(int fd, uint8_t device) {
    pwm_command_t command = {
        .op = kPWM_Stop,
        .dev = device
    };

    if(pse_command_checked(fd, kHECI_PWM_COMMAND, *(uint16_t *)&command, NULL, NULL) < 0) {
        printf("Failed to stop the PWM device\n");
        return -1;
    }
    
    return 0;
}


int main(void) {
    int fd;
    int ret = 0;

    fd = pse_client_connect();

    if (fd <= 0) {
        printf("Failed to establish a connection with the PSE\n");
        return -1;
    }

    ret = pwm_set_cycle(fd, 0, 1000, 50);
    if (ret < 0) {
        printf("Error setting PWM device\n");
            close(fd);
            return -1;
    }

    printf("PWM device is starting...\n");

    for (int i = 0; i < 11; i++) {
        ret = pwm_start(fd, 0);
        if (ret < 0) {
            printf("Error starting PWM device\n");
            break;
        }

        sleep(1);
    }

    pwm_stop(fd, 0);
    printf("PWM device stopped...\n");

    close(fd);

    return ret;
}
