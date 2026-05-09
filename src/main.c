/*******************************************************************************
  Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main.c

  Summary:
    This file contains the "main" function for a project.

  Description:
    This file contains the "main" function for a project.  The
    "main" function calls the "SYS_Initialize" function to initialize the state
    machines of all modules in the system
 *******************************************************************************/

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include <stddef.h>                     // Defines NULL
#include <stdbool.h>                    // Defines true
#include <stdlib.h>                     // Defines EXIT_FAILURE
#include "definitions.h"                // SYS function prototypes


// *****************************************************************************
// *****************************************************************************
// Section: Main Entry Point
// *****************************************************************************
// *****************************************************************************

#include <stdint.h>
#include "gf2_poly.h"

// --- Configuration for 1-Word Batching ---
#define PAYLOAD_SIZE_BYTES 512 
#define CODEWORD_SIZE_BYTES (PAYLOAD_SIZE_BYTES + 8) // Assuming 32-bit CRC overhead

// The target error weight N (0 to 4). 
// 'volatile' allows you to change this in the debugger without recompiling.
volatile uint8_t TARGET_BIT_FLIPS = 2; 

// --- Hardware Abstraction Placeholders ---
extern void UART_Receive(uint8_t* buffer, uint32_t length);
extern void UART_Transmit(uint8_t* buffer, uint32_t length);
extern void NAND_Flash_Write(uint32_t address, uint8_t* data, uint32_t length);
extern void NAND_Flash_Read(uint32_t address, uint8_t* data, uint32_t length);

// --- Error Injection Function ---
void inject_errors(uint8_t* codeword, uint32_t byte_length, uint8_t num_flips) {
    if (num_flips == 0) return;

    uint32_t total_bits = byte_length * 8;
    uint32_t flipped_positions[4] = {0}; 
    uint8_t flips_applied = 0;

    while (flips_applied < num_flips) {
        uint32_t bit_idx = rand() % total_bits;
        
        bool already_flipped = false;
        for (uint8_t i = 0; i < flips_applied; i++) {
            if (flipped_positions[i] == bit_idx) {
                already_flipped = true;
                break;
            }
        }

        if (!already_flipped) {
            flipped_positions[flips_applied] = bit_idx;
            uint32_t byte_idx = bit_idx / 8;
            uint8_t bit_offset = bit_idx % 8;
            codeword[byte_idx] ^= (1 << bit_offset);
            flips_applied++;
        }
    }
}

// --- Main Application ---
int main(void) {
    // 1. Hardware Initialization
    // System_Initialize(); 
    srand(12345); // Seed PRNG
    gf2_initialize(); // Init C++ ECC Engine

    // Buffers now only need to hold ONE word at a time
    uint8_t rx_buffer[PAYLOAD_SIZE_BYTES];
    uint8_t codeword_buffer[CODEWORD_SIZE_BYTES];
    uint8_t flash_read_buffer[CODEWORD_SIZE_BYTES];
    
    uint32_t current_flash_address = 0x00000000;

    while (1) {
        // 2. Wait for exactly ONE word from Host
        UART_Receive(rx_buffer, PAYLOAD_SIZE_BYTES);

        // 3. Encode ONE word
        // (You will need to create this C wrapper in gf2_poly.cpp)
        gf2_encode_data(rx_buffer, PAYLOAD_SIZE_BYTES, codeword_buffer);

        // 4. Inject N errors into this single codeword
        inject_errors(codeword_buffer, CODEWORD_SIZE_BYTES, TARGET_BIT_FLIPS);

        // 5. Memory Operations
        NAND_Flash_Write(current_flash_address, codeword_buffer, CODEWORD_SIZE_BYTES);
        NAND_Flash_Read(current_flash_address, flash_read_buffer, CODEWORD_SIZE_BYTES);
        current_flash_address += CODEWORD_SIZE_BYTES;

        // 6. Decode ONE word
        int correction_status = gf2_correct_errors(flash_read_buffer, CODEWORD_SIZE_BYTES);

        // 7. Send the corrected word and status back to Host
        UART_Transmit(flash_read_buffer, CODEWORD_SIZE_BYTES);
        UART_Transmit((uint8_t*)&correction_status, sizeof(correction_status));
    }

    return 0;
}