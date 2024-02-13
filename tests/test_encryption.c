#include "aes.h"
#include "Code_warehouse/c/Rand_lib.h"
#include "application_processor/inc/simple_crypto.h"
#include "stdio.h"
#include "inttypes.h"


/********************************* Global Variables **********************************/

// Random Number
#define RAND_Z_SIZE 8
uint8_t RAND_Z[RAND_Z_SIZE];

// Maximum length of an I2C register
#define MAX_I2C_MESSAGE_LEN 256

// 16 bytes
#define AES_SIZE 16

// Success & Error flags
#define SUCCESS_RETURN 0
#define ERROR_RETURN -1

typedef enum {
    COMPONENT_CMD_NONE,
    COMPONENT_CMD_SCAN,
    COMPONENT_CMD_VALIDATE,
    COMPONENT_CMD_BOOT,
    COMPONENT_CMD_ATTEST,
    COMPONENT_CMD_SECURE_SEND_VALIDATE,
    COMPONENT_CMD_SECURE_SEND_CONFIMRED,
} component_cmd_t;

/******************************** Testing Variables ********************************/

uint32_t COMP_ID1 = 123456789
uint32_t COMP_ID2 = 987654321

uint8_t GLOBAL_KEY[AES_SIZE]; // Need to define this better

/******************************** TYPE DEFINITIONS ********************************/

// Datatype for all messages
typedef struct {
    uint8_t opcode;
    uint8_t comp_ID[4];
    uint8_t rand_z[RAND_Z_SIZE];
    uint8_t rand_y[RAND_Z_SIZE];
    uint8_t remain[MAX_I2C_MESSAGE_LEN   - 21];
} message;

int test_validate_and_boot_protocol():
    uint8_t receive_buffer[MAX_I2C_MESSAGE_LEN];
    uint8_t transmit_buffer[MAX_I2C_MESSAGE_LEN];

    message* command = (message*)transmit_buffer;

    command->opcode = COMPONENT_CMD_VALIDATE;
    uint32_to_uint8(command->comp_ID, COMP_ID1);

    Rand_NASYC(RAND_Z, RAND_Z_SIZE);
    *command->rand_z = *RAND_Z;

    printf("Data before encryption:\n\n
        opcode = %c\n
        Comp_ID = %"PRIu32"\n
        Rand_Z = ", command->opcode, COMP_ID1);
    for(int x = 0; x < RAND_Z_SIZE; x++){
        printf("%c", RAND_Z[x]);
    }


    return SUCCESS_RETURN

int test_attest_protocol():
    return SUCCESS_RETURN

int test_post_boot_protocol():
    return SUCCESS_RETURN
