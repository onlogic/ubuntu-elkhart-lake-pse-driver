// --------------------------------------------------------------------------------------------------------------------------
// --- HECI TYPES -----------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------------

// Type declarations for various HECI commands. These are all casts of the core "heci request" and "heci body" types, that
// allow for simpler organization and clearer usage. The actual data is serialized/deserialized for transmission.

// Types are broken out here, so that they can be re-used by both user applications and core microcontroller code.

#ifndef __HECI_TYPES
#define __HECI_TYPES

// --------------------------------------------------------------------------------------------------------------------------
// INCLUDES -----------------------------------------------------------------------------------------------------------------

#if defined(CONFIG_BOARD_ONLOGIC_IRON) || defined(CONFIG_BOARD_ONLOGIC_IRIS)
// Zephyr RTOS (PSE real-time OS)
#include <zephyr.h>
#else
// Linux/user applications
#include <linux/uuid.h>
#include <sys/types.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#endif

// --------------------------------------------------------------------------------------------------------------------------
// DEFINES ------------------------------------------------------------------------------------------------------------------

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#if defined(CONFIG_BOARD_ONLOGIC_IRON) || defined(CONFIG_BOARD_ONLOGIC_IRIS)

#define __heci_packed __attribute__((packed, aligned(1)))

#define HECI_GUID {                                          \
    0xbb579a2e, 0xcc54, 0x4450, {                            \
            0xb1, 0xd0, 0x5e, 0x75, 0x20, 0xdc, 0xad, 0x25   \
    }                                                        \
}
#else

#define __heci_packed __attribute__((packed, aligned(sizeof(uint16_t))))

static const uuid_le pse_smhi_guid = UUID_LE(
    0xbb579a2e, 0xcc54, 0x4450, 0xb1, 0xd0, 0x5e,0x75, 0x20, 0xdc, 0xad, 0x25
);
#endif

#define MAX_HECI_DATA_LEN 224

// --------------------------------------------------------------------------------------------------------------------------
// CORE TYPES ---------------------------------------------------------------------------------------------------------------

// Valid HECI data casts
typedef enum _heci_data_kind_t {
    kHeciData_Raw = 0,
    kHeciData_Version,
    kHeciData_Can,
    kHeciData_I2C,
    kHeciData_Dio,
    kHeciData_Uart,
    kHeciData_Pwm,
    kHeciData_String,
    kHeciData_Qep,
    kHeciData_Last
} heci_data_kind_t;

// Possible HECI commands
typedef enum {
    kHECI_SYS_INFO = 0x01,
    kHECI_IO_COMMAND,
    kHECI_UART_COMMAND,
    kHECI_CAN_COMMAND,
    kHECI_PWM_COMMAND,
    kHECI_I2C_COMMAND,
    kHECI_QEP_COMMAND,
    kHECI_COMMAND_LAST
} heci_command_id_t;

// Heci command/request
typedef struct {
    uint8_t command;
    bool is_response;
    bool has_next;
    uint16_t argument;
    uint8_t status;
} __packed heci_header_t;

// Heci data body (has_next == 1)
typedef struct {
    heci_data_kind_t kind: 8;
    uint32_t length;
    uint32_t padding;
    uint8_t data[MAX_HECI_DATA_LEN];
} __packed heci_body_t;

// --------------------------------------------------------------------------------------------------------------------------
// OPERATION ENUMS ----------------------------------------------------------------------------------------------------------

typedef enum _pwm_operation_t {
    kPWM_Start = 0,
    kPWM_Stop,
    kPWM_SetCycles,
    kPWM_NumOps
} pwm_operation_t;

typedef enum _io_operation_t {
    kIO_GetInfo = 0,
    kIO_SetOutput,
    kIO_ClearOutput,
    kIO_ClearCount,
    kIO_NumOps
} io_operation_t;

typedef enum _io_device_t {
    kIODev_LED = 0,
    kIODev_DO,
    kIODev_DI,
    kIODev_NumDevs
} io_device_t;

typedef enum _can_operation_t {
    kCAN_Read = 0,
    kCAN_Write,
    kCAN_Enable,
    kCAN_Disable,
    kCAN_SetBaudrate,
    kCAN_StatusReport,
    kCAN_StatusClear,
    kCAN_NumOps
} can_operation_t;

typedef enum _i2c_operation_t {
    kI2C_Read = 0,
    kI2C_Write,
    kI2C_SetSpeedStandard,
    kI2C_SetSpeedFast,
    kI2C_SetSpeedFastPlus
} i2c_operation_t;

typedef enum _qep_operation_t {
    kQEP_Configure = 0, // QEP CONFIG DATA, status
    kQEP_StartDecode,   // No data, status
    kQEP_StopDecode,    // No data, status
    kQEP_GetDirection,  // No data, direction + status
    kQEP_GetPosCount,
    kQEP_StartCapture,
    kQEP_StopCapture,
    kQEP_EnableEvent,
    kQEP_DisableEvent,
    kQEP_GetPhaseError,
    kQEP_NumOps,
} qep_operation_t;

typedef enum _uart_operation_t {
    kUART_Read = 0,
    kUART_Write,
    kUART_Transfer,
    kUART_NumOps
} uart_operation_t;

// --------------------------------------------------------------------------------------------------------------------------
// REQUEST TYPES ------------------------------------------------------------------------------------------------------------

typedef struct _uart_command_t {
    uint8_t read_write; // read: 0, write: 1
    uint8_t device;
} __heci_packed uart_command_t;

typedef struct _i2c_command_t {
    i2c_operation_t op: 8;
    uint8_t dev: 8;
} __heci_packed i2c_command_t;

typedef struct _can_command_t {
    can_operation_t op: 3;
    uint8_t dev: 3;
    uint16_t arg: 10;
} __heci_packed can_command_t;

typedef struct _pwm_command_t {
    pwm_operation_t op: 8;
    uint8_t dev: 8;
} __heci_packed pwm_command_t;

typedef struct _io_command_t {
    io_operation_t op: 8;
    io_device_t dev: 4;
    uint8_t num: 4;
} __heci_packed io_command_t;

typedef struct _qep_command_t {
    qep_operation_t op: 8;
    uint8_t dev: 8;
} __heci_packed qep_command_t;

// --------------------------------------------------------------------------------------------------------------------------
// BODY TYPES ---------------------------------------------------------------------------------------------------------------

// Version data structure
typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t hotfix;
    uint16_t build;
}  __heci_packed heci_version_t;

// CAN Message Structure
typedef struct {
    uint32_t id;
    uint8_t id_type;
    uint8_t frame_type;
    uint8_t length;
    uint32_t data_word_0;
    uint32_t data_word_1;
} __heci_packed heci_can_data_t;

// DIO Info Structure
typedef struct {
    uint8_t state;
    uint64_t count;
} __heci_packed heci_dio_info_t;

// HECI PWM Cycle configuration
typedef struct {
    uint64_t period_usec;
    uint64_t pulse_usec;
} __heci_packed heci_pwm_data_t;

// HECI i2c message
typedef struct {
    uint8_t addr;
    uint8_t sub;
    uint8_t data;
} __heci_packed heci_i2c_data_t;

// HECI qep configuration
typedef struct {
    uint32_t data;
    uint64_t buffer[16];
} __heci_packed heci_qep_data_t;

// --------------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------------

#endif /* __HECI_TYPES */
