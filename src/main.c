#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include "definitions.h" 
#include "gf2_poly.h"

// --- Configuration Test ---
#define PAYLOAD_SIZE_BYTES 590 
#define CODEWORD_SIZE_BYTES 598 
volatile uint8_t TARGET_BIT_FLIPS = 2; 

// --- USB Variables ---
static bool is_usb_configured = false;
static bool is_read_complete = false;
static bool is_write_complete = false;
static USB_DEVICE_CDC_TRANSFER_HANDLE readTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
static USB_DEVICE_CDC_TRANSFER_HANDLE writeTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
static USB_DEVICE_HANDLE usbDeviceHandle = USB_DEVICE_HANDLE_INVALID; // NEW: Device Handle

// Aligned buffers for USB DMA hardware
uint8_t CACHE_ALIGN rx_buffer[PAYLOAD_SIZE_BYTES];
uint8_t CACHE_ALIGN tx_buffer[CODEWORD_SIZE_BYTES + 5]; // 598 + 4 (BCH) + 1 (CRC) = 603 bytes

uint8_t codeword_buffer[CODEWORD_SIZE_BYTES];
uint8_t flash_read_buffer[CODEWORD_SIZE_BYTES];

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
                already_flipped = true; break;
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

// --- USB Callbacks ---
USB_DEVICE_CDC_EVENT_RESPONSE APP_USBDeviceCDCEventHandler(
    USB_DEVICE_CDC_INDEX index, 
    USB_DEVICE_CDC_EVENT event, 
    void * pData, uintptr_t userData) 
{
    switch (event) {
        case USB_DEVICE_CDC_EVENT_SET_LINE_CODING:
        case USB_DEVICE_CDC_EVENT_SET_CONTROL_LINE_STATE:
            USB_DEVICE_ControlStatus(usbDeviceHandle, USB_DEVICE_CONTROL_STATUS_OK);
            break;
        case USB_DEVICE_CDC_EVENT_READ_COMPLETE:
            is_read_complete = true; 
            break;
        case USB_DEVICE_CDC_EVENT_WRITE_COMPLETE:
            is_write_complete = true; 
            break;
        default: break;
    }
    return USB_DEVICE_CDC_EVENT_RESPONSE_NONE;
}

void APP_USBDeviceEventHandler(USB_DEVICE_EVENT event, void * eventData, uintptr_t context) {
    switch(event) {
        case USB_DEVICE_EVENT_CONFIGURED:
            is_usb_configured = true;
            USB_DEVICE_CDC_EventHandlerSet(USB_DEVICE_CDC_INDEX_0, APP_USBDeviceCDCEventHandler, 0);
            break;
        case USB_DEVICE_EVENT_SUSPENDED:
        case USB_DEVICE_EVENT_RESET: // FIXED: Was DETACHED
        case USB_DEVICE_EVENT_DECONFIGURED:
            is_usb_configured = false;
            break;
        default: break;
    }
}

// --- Main Application ---
int main(void) {
    SYS_Initialize(NULL); 
    srand(12345); 
    gf2_initialize(); 

    enum { WAIT_FOR_CONFIG, WAIT_FOR_READ, PROCESS_DATA, WAIT_FOR_WRITE } appState = WAIT_FOR_CONFIG;

    while (1) {
        SYS_Tasks();

        switch(appState) {
            case WAIT_FOR_CONFIG:
                // FIXED: Open the USB device properly to get the Handle
                if (usbDeviceHandle == USB_DEVICE_HANDLE_INVALID) {
                    usbDeviceHandle = USB_DEVICE_Open(USB_DEVICE_INDEX_0, DRV_IO_INTENT_READWRITE);
                    if (usbDeviceHandle != USB_DEVICE_HANDLE_INVALID) {
                        USB_DEVICE_EventHandlerSet(usbDeviceHandle, APP_USBDeviceEventHandler, 0);
                        USB_DEVICE_Attach(usbDeviceHandle); 
                    }
                }

                if (is_usb_configured) {
                    is_read_complete = false;
                    USB_DEVICE_CDC_Read(USB_DEVICE_CDC_INDEX_0, &readTransferHandle, rx_buffer, PAYLOAD_SIZE_BYTES);
                    appState = WAIT_FOR_READ;
                }
                break;

            case WAIT_FOR_READ:
                if (!is_usb_configured) { appState = WAIT_FOR_CONFIG; break; } 
                
                if (is_read_complete) {
                    appState = PROCESS_DATA;
                }
                break;

            case PROCESS_DATA:
                gf2_encode_data(rx_buffer, PAYLOAD_SIZE_BYTES, codeword_buffer);
                inject_errors(codeword_buffer, CODEWORD_SIZE_BYTES, TARGET_BIT_FLIPS);

                for(int i=0; i<CODEWORD_SIZE_BYTES; i++) flash_read_buffer[i] = codeword_buffer[i];

                uint8_t crc_status = 0;
                int bch_status = gf2_correct_errors(flash_read_buffer, CODEWORD_SIZE_BYTES, &crc_status);

                for(int i=0; i<CODEWORD_SIZE_BYTES; i++) tx_buffer[i] = flash_read_buffer[i];
                tx_buffer[CODEWORD_SIZE_BYTES]     = (bch_status >> 0) & 0xFF;
                tx_buffer[CODEWORD_SIZE_BYTES + 1] = (bch_status >> 8) & 0xFF;
                tx_buffer[CODEWORD_SIZE_BYTES + 2] = (bch_status >> 16) & 0xFF;
                tx_buffer[CODEWORD_SIZE_BYTES + 3] = (bch_status >> 24) & 0xFF;
                tx_buffer[CODEWORD_SIZE_BYTES + 4] = crc_status;

                is_write_complete = false;
                USB_DEVICE_CDC_Write(USB_DEVICE_CDC_INDEX_0, &writeTransferHandle, tx_buffer, sizeof(tx_buffer), USB_DEVICE_CDC_TRANSFER_FLAGS_DATA_COMPLETE);
                appState = WAIT_FOR_WRITE;
                break;

            case WAIT_FOR_WRITE:
                if (!is_usb_configured) { appState = WAIT_FOR_CONFIG; break; }
                
                if (is_write_complete) {
                    is_read_complete = false;
                    USB_DEVICE_CDC_Read(USB_DEVICE_CDC_INDEX_0, &readTransferHandle, rx_buffer, PAYLOAD_SIZE_BYTES);
                    appState = WAIT_FOR_READ;
                }
                break;
        }
    }
    return 0;
}