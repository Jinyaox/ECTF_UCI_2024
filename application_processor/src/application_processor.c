/**
 * @file application_processor.c
 * @author Jacob Doll
 * @brief eCTF AP Example Design Implementation
 * @date 2024
 *
 * This source file is part of an example system for MITRE's 2024 Embedded
 * System CTF (eCTF). This code is being provided only for educational purposes
 * for the 2024 MITRE eCTF competition, and may not meet MITRE standards for
 * quality. Use this code at your own risk!
 *
 * @copyright Copyright (c) 2024 The MITRE Corporation
 */

#include "board.h"
#include "i2c.h"
#include "led.h"
#include "mxc_delay.h"
#include "mxc_device.h"
#include "nvic_table.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Rand_lib.h"
#include "aes.h"

#include "board_link.h"
#include "host_messaging.h"
#include "key_exchange.h"

// Includes from containerized build
#include "disable_cache.h"
#include "ectf_params.h"

#include "simple_flash.h"

/********************************* Global Variables **********************************/

// Flash Macros
#define FLASH_ADDR                                                             \
    ((MXC_FLASH_MEM_BASE + MXC_FLASH_MEM_SIZE) - (2 * MXC_FLASH_PAGE_SIZE))
#define FLASH_MAGIC 0xDEADBEEF

// Library call return types
#define SUCCESS_RETURN 0
#define ERROR_RETURN -1

// Secure Communication Macro
// data is the output
// 12 byte number
// data is the output
// 12 byte number
#define RAND_Z_SIZE 8
uint8_t RAND_Z[RAND_Z_SIZE];
uint8_t RAND_Y[RAND_Z_SIZE];

// AES Macros
#define AES_SIZE 16 // 16 bytes

uint8_t synthesized = 0; // when you initiate any command from the host machine, check if the
                         // thing is synthesized yet or not, if not, synthesize the whole thing.
uint8_t GLOBAL_KEY[AES_SIZE];

/******************************** TYPE DEFINITIONS *********************************/
// Data structure for sending commands to component
// Params allows for up to MAX_I2C_MESSAGE_LEN - 1 bytes to be send
// along with the opcode through board_link. This is not utilized by the example
// design but can be utilized by your design.

// Data type for all commands
typedef struct {
    uint8_t opcode;
    uint8_t comp_ID[4];
    uint8_t rand_z[RAND_Z_SIZE];
    uint8_t rand_y[RAND_Z_SIZE];
    uint8_t remain[MAX_I2C_MESSAGE_LEN   - 21];
} message;

// Datatype for information stored in flash
typedef struct {
    uint32_t flash_magic;
    uint32_t component_cnt;
    uint32_t component_ids[32];
} flash_entry;

flash_entry flash_status;

// Datatype for commands sent to components
typedef enum {
    COMPONENT_CMD_NONE,
    COMPONENT_CMD_SCAN,
    COMPONENT_CMD_VALIDATE,
    COMPONENT_CMD_BOOT,
    COMPONENT_CMD_ATTEST,
    COMPONENT_CMD_SECURE_SEND_VALIDATE,
    COMPONENT_CMD_SECURE_SEND_CONFIMRED,
} component_cmd_t;

// forward declaration
int issue_cmd(i2c_addr_t addr, uint8_t *transmit, uint8_t *receive);
void flash_simple_init(void);
int flash_simple_erase_page(uint32_t address);
void flash_simple_read(uint32_t address, uint32_t *buffer, uint32_t size);
int flash_simple_write(uint32_t address, uint32_t *buffer, uint32_t size);
int encrypt_sym(uint8_t *plaintext, size_t len, uint8_t *key,
                uint8_t *ciphertext);

/******************************* POST BOOT FUNCTIONALITY *********************************/
/**
 * @brief Secure Send and Receive
 *
 * @param address: i2c_addr_t, I2C address of sender
 * @param buffer: uint8_t*, pointer to buffer to receive data to
 * @param transmit_buffer: message buffer for transmit
 * @param receive_buffer: message buffer of receive
 *
 * @return int: number of bytes received, negative if error
 *
 * Securely send and receive data over I2C. This function is utilized in
 * POST_BOOT functionality.
 */
int secure_send_and_receive(i2c_addr_t address, uint8_t *transmit_buffer, uint8_t *receive_buffer) {
    uint8_t challenge_buffer[MAX_I2C_MESSAGE_LEN];
    uint8_t answer_buffer[MAX_I2C_MESSAGE_LEN];

    message* send_packet_chlg = (message*)challenge_buffer;
    Rand_ASYC(RAND_Z, RAND_Z_SIZE);
    send_packet_chlg->opcode = COMPONENT_CMD_SECURE_SEND_VALIDATE;
    *send_packet_chlg->rand_z = *RAND_Z;

    int len_chlg = issue_cmd(address, challenge_buffer, answer_buffer);
    if (len_chlg == ERROR_RETURN) {
        print_error("Failed to validate the secure send for post boot\n");
        return ERROR_RETURN;
    }

    message* response_ans = (message*)answer_buffer;
    // compare cmd code
    if (response_ans->opcode != COMPONENT_CMD_SECURE_SEND_VALIDATE) {
        print_error("Invalid command message from component");
    }

    // compare Z value
    if (response_ans->rand_z != RAND_Z) {
        print_error("AP received expired validate message in post boot");
        return ERROR_RETURN;
    }

    message* send_packet_trans = (message*)transmit_buffer;
    *response_ans->rand_y = *RAND_Y;
    send_packet_trans->opcode = COMPONENT_CMD_SECURE_SEND_CONFIMRED;
    *send_packet_trans->rand_z = *RAND_Z;
    *send_packet_trans->rand_y = *RAND_Y;

    int len_trans = issue_cmd(address, transmit_buffer, receive_buffer);
    if (len_trans == ERROR_RETURN) {
        print_error("Failed to send and receive for post boot\n");
        return ERROR_RETURN;
    }

    message* response_rec = (message*)receive_buffer;
    // compare cmd code
    if (response_rec->opcode != COMPONENT_CMD_SECURE_SEND_CONFIMRED) {
        print_error("Invalid command message from component");
        return ERROR_RETURN;
    }
    // compare Y value
    if (response_rec->rand_y != RAND_Y) {
        print_error("AP received expired confirm message in post boot");
        return ERROR_RETURN;
    }
    return len_trans;
}

/**
 * @brief Get Provisioned IDs
 *
 * @param uint32_t* buffer
 *
 * @return int: number of ids
 *
 * Return the currently provisioned IDs and the number of provisioned IDs
 * for the current AP. This functionality is utilized in POST_BOOT
 * functionality. This function must be implemented by your team.
 */
int get_provisioned_ids(uint32_t *buffer) {
    memcpy(buffer, flash_status.component_ids,
           flash_status.component_cnt * sizeof(uint32_t));
    return flash_status.component_cnt;
}

/********************************* UTILITIES **********************************/
//

// Initialize the device
// This must be called on startup to initialize the flash and i2c interfaces
void init() {

    // Enable global interrupts
    __enable_irq();

    // Setup Flash
    flash_simple_init();

    // Test application has been booted before
    flash_simple_read(FLASH_ADDR, (uint32_t *)&flash_status,
                      sizeof(flash_entry));

    // Write Component IDs from flash if first boot e.g. flash unwritten
    if (flash_status.flash_magic != FLASH_MAGIC) {
        print_debug("First boot, setting flash!\n");

        flash_status.flash_magic = FLASH_MAGIC;
        flash_status.component_cnt = COMPONENT_CNT;
        uint32_t component_ids[COMPONENT_CNT] = {COMPONENT_IDS};
        memcpy(flash_status.component_ids, component_ids,
               COMPONENT_CNT * sizeof(uint32_t));

        flash_simple_write(FLASH_ADDR, (uint32_t *)&flash_status,
                           sizeof(flash_entry));
    }

    // Initialize board link interface
    board_link_init();
}

// Send a command to a component and receive the result
int issue_cmd(i2c_addr_t addr, uint8_t *transmit, uint8_t *receive) {
    // Send message
    int result = secure_send_packet(addr, sizeof(uint8_t), transmit,GLOBAL_KEY); 
    if (result == ERROR_RETURN) {
        return ERROR_RETURN;
    }

    // Receive message
    int len = secure_poll_and_receive_packet(
        addr, receive, GLOBAL_KEY); // Use secure custom function
    if (len == ERROR_RETURN) {
        return ERROR_RETURN;
    }
    return len;
}

/******************************** COMPONENT COMMS ********************************/

// We're assuming this doesn't need protection/modification
int scan_components() {
    // Print out provisioned component IDs
    for (unsigned i = 0; i < flash_status.component_cnt; i++) {
        print_info("P>0x%08x\n", flash_status.component_ids[i]);
    }

    // Buffers for board link communication
    uint8_t receive_buffer[MAX_I2C_MESSAGE_LEN];
    uint8_t transmit_buffer[MAX_I2C_MESSAGE_LEN];

    // Scan scan command to each component
    for (i2c_addr_t addr = 0x8; addr < 0x78; addr++) {
        // I2C Blacklist:
        // 0x18, 0x28, and 0x36 conflict with separate devices on MAX78000FTHR
        if (addr == 0x18 || addr == 0x28 || addr == 0x36) {
            continue;
        }

        // Create command message
        message* command = (message*)transmit_buffer;

        uint8_t msg[AES_SIZE];
        uint8_t ciphertext[AES_SIZE];
        msg[0] = COMPONENT_CMD_SCAN;
        //Calling simple_crypto.c
        encrypt_sym(msg, AES_SIZE, GLOBAL_KEY, ciphertext);
        //uint8_t *plaintext, size_t len, uint8_t *key, uint8_t *ciphertext

        // put ciphertext in transmit_buffer memcpy
        for (int i = 0; i < AES_SIZE; i++) {
            transmit_buffer[i] = ciphertext[i];
        }
        // Send out command and receive resultGLOBAL_KEY, c

        command->opcode = (uint8_t)COMPONENT_CMD_SCAN;

        // Send out command and receive result
        int len = issue_cmd(addr, transmit_buffer, receive_buffer);

        // Success, device is present
        if (len > 0) {
            message* scan = (message*)receive_buffer;
            print_info("F>0x%08x\n", scan->comp_ID);
        }
    }
    print_success("List\n");
    return SUCCESS_RETURN;
}

int validate_and_boot_components() {
    // Buffers for board link communication
    uint8_t receive_buffer[MAX_I2C_MESSAGE_LEN];
    uint8_t transmit_buffer[MAX_I2C_MESSAGE_LEN];

    // Send validate command to each component
    for (unsigned i = 0; i < flash_status.component_cnt; i++) {
        // Set the I2C address of the component
        uint32_t component_id = flash_status.component_ids[i];
        i2c_addr_t addr = component_id_to_i2c_addr(component_id);

        // Create Validate and boot message
        message* command = (message*)transmit_buffer;

        // opcode
        command->opcode = COMPONENT_CMD_VALIDATE;

        // comp_ID
        command->comp_ID = component_id;

        Rand_NASYC(RAND_Z, RAND_Z_SIZE);

        // rand_z
        *command->rand_z = *RAND_Z;

        // Send out command and receive result
        int len = issue_cmd(addr, transmit_buffer, receive_buffer);
        if (len == ERROR_RETURN) {
            print_error("Could not validate or boot component\n");
            return ERROR_RETURN;
        }

        message* response = (message* )receive_buffer;

        // compare cmd code
        if (response->opcode != COMPONENT_CMD_BOOT) {
            print_error("Invalid command message from component");
            return ERROR_RETURN;
        }

        // compare cid
        if (response->comp_ID != component_id) {
            print_error("Component ID: 0x%08x invalid\n",
                        flash_status.component_ids[i]);
            return ERROR_RETURN;
        }

        // compare Z value
        if (response->rand_z != RAND_Z) {
            print_error("Random number provided is invalid");
            return ERROR_RETURN;
        }
    }
    return SUCCESS_RETURN;
}

int attest_component(uint32_t component_id) {
    // Buffers for board link communication
    uint8_t receive_buffer[MAX_I2C_MESSAGE_LEN];
    uint8_t transmit_buffer[MAX_I2C_MESSAGE_LEN];

    // Set the I2C address of the component
    i2c_addr_t addr = component_id_to_i2c_addr(component_id);

    // Create Validate and boot message
    message* command = (message*)transmit_buffer;

    // op_code
    command->opcode = COMPONENT_CMD_ATTEST;

    // comp_ID
    command->comp_ID = component_id;

    Rand_NASYC(RAND_Z, RAND_Z_SIZE);

    // rand_z
    *command->rand_z = *RAND_Z;

    // Send out command and receive result
    int len = issue_cmd(addr, transmit_buffer, receive_buffer);
    if (len == ERROR_RETURN) {
        print_error("Could not attest\n");
        return ERROR_RETURN;
    }

    // decrypt attestation data
    message* response = (message*)receive_buffer;

    // compare Z value
    if (response->rand_z != RAND_Z) {
        print_error("Random number provided is invalid");
        return ERROR_RETURN;
    }
    // Print out attestation data
    // TODO: it was originally right above the SUCCESS_RETURN, if you want
    // these to keep below the for loop, we need to store the values from
    // the receive_buffer - AJ
    print_info("C>0x%08x\n", component_id);
    print_info("%s", response->remain);

    return SUCCESS_RETURN;
}

/********************************* AP LOGIC ***********************************/

// Boot sequence
// YOUR DESIGN MUST NOT CHANGE THIS FUNCTION
// Boot message is customized through the AP_BOOT_MSG macro
void boot() {
// Example of how to utilize included simple_crypto.h
#ifdef CRYPTO_EXAMPLE
    // This string is 16 bytes long including null terminator
    // This is the block size of included symmetric encryption
    char *data = "Crypto Example!";
    uint8_t ciphertext[BLOCK_SIZE];
    uint8_t key[KEY_SIZE];

    // Zero out the key
    bzero(key, BLOCK_SIZE);

    // Encrypt example data and print out
    encrypt_sym((uint8_t *)data, BLOCK_SIZE, key, ciphertext);
    print_debug("Encrypted data: ");
    print_hex_debug(ciphertext, BLOCK_SIZE);

    // Hash example encryption results
    uint8_t hash_out[HASH_SIZE];
    hash(ciphertext, BLOCK_SIZE, hash_out);

    // Output hash result
    print_debug("Hash result: ");
    print_hex_debug(hash_out, HASH_SIZE);

    // Decrypt the encrypted message and print out
    uint8_t decrypted[BLOCK_SIZE];
    decrypt_sym(ciphertext, BLOCK_SIZE, key, decrypted);
    print_debug("Decrypted message: %s\r\n", decrypted);
#endif

// POST BOOT FUNCTIONALITY
// DO NOT REMOVE IN YOUR DESIGN
#ifdef POST_BOOT
    POST_BOOT
#else
    // Everything after this point is modifiable in your design
    // LED loop to show that boot occurred
    while (1) {
        LED_On(LED1);
        MXC_Delay(500000);
        LED_On(LED2);
        MXC_Delay(500000);
        LED_On(LED3);
        MXC_Delay(500000);
        LED_Off(LED1);
        MXC_Delay(500000);
        LED_Off(LED2);
        MXC_Delay(500000);
        LED_Off(LED3);
        MXC_Delay(500000);
    }
#endif
}

// Compare the entered PIN to the correct PIN
int validate_pin() {
    char buf[50];
    recv_input("Enter pin: ", buf);
    if (!strncmp(buf, AP_PIN, 6)) { // TODO: 1
        print_debug("Pin Accepted!\n");
        return SUCCESS_RETURN;
    }
    print_error("Invalid PIN!\n");
    return ERROR_RETURN;
}

// Function to validate the replacement token
int validate_token() {
    char buf[50];
    recv_input("Enter token: ", buf);
    if (!strncmp(buf, AP_TOKEN, 16)) { // TODO: 2
        print_debug("Token Accepted!\n");
        return SUCCESS_RETURN;
    }
    print_error("Invalid Token!\n");
    return ERROR_RETURN;
}

// Boot the components and board if the components validate
void attempt_boot() {
    if (validate_and_boot_components()) {
        print_error("Failed to validate and/or boot components\n");
        return;
    }
    print_debug("All Components validated\n");

    // Print boot message
    // This always needs to be printed when booting
    print_info("AP>%s\n", AP_BOOT_MSG);
    print_success("Boot\n");
    // Boot
    boot();
}

// Replace a component if the PIN is correct
void attempt_replace() {
    char buf[50];

    if (validate_token()) {
        return;
    }

    uint32_t component_id_in = 0;
    uint32_t component_id_out = 0;

    recv_input("Component ID In: ", buf);
    sscanf(buf, "%x", &component_id_in);
    recv_input("Component ID Out: ", buf);
    sscanf(buf, "%x", &component_id_out);

    // Find the component to swap out
    for (unsigned i = 0; i < flash_status.component_cnt; i++) {
        if (flash_status.component_ids[i] == component_id_out) {
            flash_status.component_ids[i] = component_id_in;

            // write updated component_ids to flash
            flash_simple_erase_page(FLASH_ADDR);
            flash_simple_write(FLASH_ADDR, (uint32_t *)&flash_status,
                               sizeof(flash_entry));

            print_debug("Replaced 0x%08x with 0x%08x\n", component_id_out,
                        component_id_in);
            print_success("Replace\n");
            return;
        }
    }

    // Component Out was not found
    print_error("Component 0x%08x is not provisioned for the system\r\n",
                component_id_out);
}

// Attest a component if the PIN is correct
void attempt_attest() {
    char buf[50];

    if (validate_pin()) {
        return;
    }
    uint32_t component_id;
    recv_input("Component ID: ", buf);
    sscanf(buf, "%x", &component_id);
    if (attest_component(component_id) == SUCCESS_RETURN) {
        print_success("Attest\n");
    }
}

/*********************************** MAIN *************************************/

int main() {
    // Initialize board
    init();
    Rand_NASYC(RAND_Z, RAND_Z_SIZE);

    // Print the component IDs to be helpful
    // Your design does not need to do this
    print_info("Application Processor Started\n");

    // Handle commands forever
    char buf[100];
    while (1) {
        memset(buf, 0, 100);
        recv_input("Enter Command: ", buf);

        // Shouldn't the merging happen here?
        if ((synthesized == 0) && (strlen(buf) != 0)) {
            key_sync(GLOBAL_KEY, flash_status.component_cnt,
                     flash_status.component_ids[0],
                     flash_status.component_ids[1]);
            synthesized = 1;
        }

        // Execute requested command
        if (!strcmp(buf, "list")) { // TODO: 3
            scan_components();
        } else if (!strcmp(buf, "boot")) { // TODO: 4
            attempt_boot();
        } else if (!strcmp(buf, "replace")) { // TODO: 5
            attempt_replace();
        } else if (!strcmp(buf, "attest")) { // TODO: 6
            attempt_attest();
        } else {
            print_error("Unrecognized command '%s'\n", buf);
        }
    }

    // Code never reaches here
    return 0;
}
