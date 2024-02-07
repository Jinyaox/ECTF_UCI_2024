/**
 * @file component.c
 * @author Jacob Doll
 * @brief eCTF Component Example Design Implementation
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
#include "mxc_errors.h"
#include "nvic_table.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "board_link.h"
#include "simple_i2c_peripheral.h"

// Includes from containerized build
#include "ectf_params.h"

// Include cache disable
#include "disable_cache.h"
#include "Rand_lib.h"
#include "key_exchange.h"


#ifdef POST_BOOT
#include "led.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#endif

/********************************* CONSTANTS **********************************/

// Passed in through ectf-params.h
// Example of format of ectf-params.h shown here
/*
#define COMPONENT_ID 0x11111124
#define COMPONENT_BOOT_MSG "Component boot"
#define ATTESTATION_LOC "McLean"
#define ATTESTATION_DATE "08/08/08"
#define ATTESTATION_CUSTOMER "Fritz"
*/
//AES
#define AES_SIZE 16// 16 bytes
#define RAND_Z_SIZE 16
#define RAND_Y_SIZE 16
uint8_t RAND_Y[RAND_Y_SIZE];
uint8_t GLOBAL_KEY[AES_SIZE];
uint8_t synthesized=0;

/******************************** TYPE DEFINITIONS ********************************/
// Commands received by Component using 32 bit integer
typedef enum {
    COMPONENT_CMD_NONE,
    COMPONENT_CMD_SCAN,
    COMPONENT_CMD_VALIDATE,
    COMPONENT_CMD_BOOT,
    COMPONENT_CMD_ATTEST,
    COMPONENT_CMD_SECURE_SEND_VALIDATE,
    COMPONENT_CMD_SECURE_SEND_CONFIMRED,
} component_cmd_t;

/******************************** TYPE DEFINITIONS ********************************/
// Data structure for receiving messages from the AP
typedef struct {
    uint8_t opcode;
    uint32_t comp_ID;
    uint8_t rand_z[RAND_Z_SIZE];
    uint8_t rand_y[RAND_Z_SIZE];
    uint8_t remain[MAX_I2C_MESSAGE_LEN-21];
} message;


/********************************* FUNCTION DECLARATIONS **********************************/
// Core function definitions
void component_process_cmd(void);
void process_boot(void);
void process_scan(void);
void process_validate(void);
void process_attest(void);

/********************************* GLOBAL VARIABLES **********************************/
// Global varaibles
uint8_t receive_buffer[MAX_I2C_MESSAGE_LEN];
uint8_t transmit_buffer[MAX_I2C_MESSAGE_LEN];
uint8_t string_buffer[MAX_I2C_MESSAGE_LEN-21];

/******************************* POST BOOT FUNCTIONALITY *********************************/
/**
 * @brief Secure Send
 *
 * @param buffer: uint8_t*, pointer to data to be send
 * @param len: uint8_t, size of data to be sent
 *
 * Securely send data over I2C. This function is utilized in POST_BOOT
 * functionality. This function must be implemented by your team to align with
 * the security requirements.
 */
void secure_send(uint8_t *buffer, uint8_t len) {
    send_packet_and_ack(len, buffer);
}

/**
 * @brief Secure Receive
 *
 * @param buffer: uint8_t*, pointer to buffer to receive data to
 *
 * @return int: number of bytes received, negative if error
 *
 * Securely receive data over I2C. This function is utilized in POST_BOOT
 * functionality. This function must be implemented by your team to align with
 * the security requirements.
 */
int secure_receive(uint8_t *buffer) { return wait_and_receive_packet(buffer); }


// Not sure what the component will send back to AP, for Now I Just assume the trasmit_buffer input will have the message already
void secure_receive_and_send(uint8_t * receive_buffer, uint8_t * transmit_buffer, uint8_t len){
    memset(receive_buffer, 0, 256);//Keep eye on all the memset method, Zuhair says this could be error pron
    secure_wait_and_receive_packet(receive_buffer, GLOBAL_KEY);
    message * command = (message *)receive_buffer;
    Rand_ASYC(RAND_Y, RAND_Y_SIZE);
    uint8_t validate_buffer[MAX_I2C_MESSAGE_LEN];
    message * send_packet = (message *)validate_buffer;
    send_packet->opcode = COMPONENT_CMD_SECURE_SEND_VALIDATE;
    memcpy(send_packet->rand_z, command->rand_z, RAND_Z_SIZE);
    memcpy(send_packet->rand_y, RAND_Y, RAND_Y_SIZE);
    secure_send_packet_and_ack(MAX_I2C_MESSAGE_LEN, validate_buffer, GLOBAL_KEY);
    memset(receive_buffer, 0, 256);//Keep eye on all the memset method, Zuhair says this could be error pron
    if(secure_timed_wait_and_receive_packet(receive_buffer, GLOBAL_KEY)<0){
        printf("Component transmitting failed, the transmitting takes too long");
        return;
    }
    command = (message*) receive_buffer;
    if(command->rand_y != RAND_Y){
        printf("Component has received expired message");
    }
    send_packet = (message *)transmit_buffer;
    send_packet->opcode = COMPONENT_CMD_SECURE_SEND_CONFIMRED;
    memcpy(send_packet->rand_z, command->rand_z, RAND_Z_SIZE);
    secure_send_packet_and_ack(MAX_I2C_MESSAGE_LEN, transmit_buffer, GLOBAL_KEY);
}
/******************************* FUNCTION DEFINITIONS *********************************/

// Example boot sequence
// Your design does not need to change this
void boot() {

// POST BOOT FUNCTIONALITY
// DO NOT REMOVE IN YOUR DESIGN
#ifdef POST_BOOT
    POST_BOOT
#else
    // Anything after this macro can be changed by your design
    // but will not be run on provisioned systems
    LED_Off(LED1);
    LED_Off(LED2);
    LED_Off(LED3);
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

// Handle a command from the AP
void component_process_cmd() {
    message* command = (message*) receive_buffer;

    // Output to application processor dependent on command received
    switch (command->opcode) {
    case COMPONENT_CMD_VALIDATE:
        process_boot();
        break;
    case COMPONENT_CMD_SCAN:
        process_scan();
        break;
    case COMPONENT_CMD_ATTEST:
        process_attest();
        break;
    default:
        printf("Error: Unrecognized command received %d\n", command->opcode);
        break;
    }
}

// This if for the functionality of Boot
void process_boot() {
    // The AP requested a boot. 
    //Validate the Component ID
    message* command = (message*) receive_buffer;

    if(command->comp_ID != COMPONENT_ID){
        printf("The Component ID checks failed at the component sided");
        return;
    }
    //Validation passed
    //Starts Boot
    boot();
    //Send Boot comfirmation message back to AP
    memset(transmit_buffer, 0, MAX_I2C_MESSAGE_LEN);//DO WE NEED THIS?
    message * send_packet = (message*) transmit_buffer;
    send_packet->opcode = COMPONENT_CMD_BOOT;
    memcpy(send_packet->rand_z, command->rand_z, RAND_Z_SIZE);
    send_packet->comp_ID = send_packet->comp_ID;
    secure_send_packet_and_ack(MAX_I2C_MESSAGE_LEN, transmit_buffer, GLOBAL_KEY);
}

void process_scan() {
    // The AP requested a scan. Respond with the Component ID

    message* command = (message*) receive_buffer;
    memset(transmit_buffer, 0, MAX_I2C_MESSAGE_LEN);//DO WE NEED THIS?
    message * send_packet = (message*) transmit_buffer;
    send_packet->opcode = COMPONENT_CMD_SCAN;
    memcpy(send_packet->rand_z, command->rand_z, RAND_Z_SIZE);
    send_packet->comp_ID = COMPONENT_ID;
    secure_send_packet_and_ack(MAX_I2C_MESSAGE_LEN, transmit_buffer, GLOBAL_KEY);
}

void process_attest() {
    // The AP requested attestation. Respond with the attestation data

    //Validate the Component ID; plaintext[1:4]
    message* command = (message*) receive_buffer;

    if(command->comp_ID != COMPONENT_ID){
        printf("The Component ID checks failed at the component sided");
        return;
    }

    // Start to move atttestation data into the transmit_buffer
    memset(string_buffer, 0, MAX_I2C_MESSAGE_LEN);
    uint8_t len = sprintf((char*)string_buffer, "LOC>%s\nDATE>%s\nCUST>%s\n",
                ATTESTATION_LOC, ATTESTATION_DATE, ATTESTATION_CUSTOMER) + 1;
    

    //Move the cipher text into the transmit_buffer and reday for transfer
    memset(transmit_buffer, 0, MAX_I2C_MESSAGE_LEN);//DO WE NEED THIS?
    message* send_packet = (message*)transmit_buffer;
    send_packet->opcode = COMPONENT_CMD_ATTEST;
    memcpy(send_packet->rand_z, command->rand_z, RAND_Z_SIZE);
    send_packet->comp_ID = COMPONENT_ID;
    memcpy(send_packet->remain, string_buffer, sizeof(send_packet->remain));
    secure_send_packet_and_ack(MAX_I2C_MESSAGE_LEN, transmit_buffer, GLOBAL_KEY);
}


/*********************************** MAIN *************************************/

int main(void) {
    printf("Component Started\n");
    // Disable the cache
    disable_cache();
    // Enable Global Interrupts
    __enable_irq();

    // Initialize Component
    i2c_addr_t addr = component_id_to_i2c_addr(COMPONENT_ID);
    board_link_init(addr);

    LED_On(LED2);

    while (1) {
        if(synthesized == 0){

            key_sync(GLOBAL_KEY);
            synthesized = 1;
        }
        component_process_cmd();
    }
}
