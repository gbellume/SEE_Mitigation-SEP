#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include "definitions.h" // Questo include i driver USB generati da Harmony
#include "gf2_poly.h"

// --- Configurazione Test ---
#define PAYLOAD_SIZE_BYTES 590 
#define CODEWORD_SIZE_BYTES 598 
volatile uint8_t TARGET_BIT_FLIPS = 2; 

// --- Variabili USB ---
static bool is_usb_configured = false;
static bool is_read_complete = false;
static bool is_write_complete = false;
static USB_DEVICE_CDC_TRANSFER_HANDLE readTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
static USB_DEVICE_CDC_TRANSFER_HANDLE writeTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;

// Buffer allineati per l'USB (requisito hardware del DMA)
uint8_t CACHE_ALIGN rx_buffer[PAYLOAD_SIZE_BYTES];
uint8_t CACHE_ALIGN tx_buffer[CODEWORD_SIZE_BYTES + 5]; // 598 + 4 (BCH) + 1 (CRC) = 603 bytes

uint8_t codeword_buffer[CODEWORD_SIZE_BYTES];
uint8_t flash_read_buffer[CODEWORD_SIZE_BYTES];

// --- Funzione Iniezione Errori (invariata) ---
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

// --- Callback USB ---
// Questa funzione viene chiamata in automatico dall'hardware quando succede qualcosa sull'USB
USB_DEVICE_CDC_EVENT_RESPONSE APP_USBDeviceCDCEventHandler(
    USB_DEVICE_CDC_INDEX index, 
    USB_DEVICE_CDC_EVENT event, 
    void * pData, uintptr_t userData) 
{
    switch (event) {
        case USB_DEVICE_CDC_EVENT_SET_LINE_CODING:
        case USB_DEVICE_CDC_EVENT_SET_CONTROL_LINE_STATE:
            USB_DEVICE_ControlStatus(0, USB_DEVICE_CONTROL_STATUS_OK);
            break;
        case USB_DEVICE_CDC_EVENT_READ_COMPLETE:
            is_read_complete = true; // Il PC ci ha inviato i dati!
            break;
        case USB_DEVICE_CDC_EVENT_WRITE_COMPLETE:
            is_write_complete = true; // Abbiamo finito di inviare i dati al PC
            break;
        default: break;
    }
    return USB_DEVICE_CDC_EVENT_RESPONSE_NONE;
}

void APP_USBDeviceEventHandler(USB_DEVICE_EVENT event, void * eventData, uintptr_t context) {
    switch(event) {
        case USB_DEVICE_EVENT_CONFIGURED:
            is_usb_configured = true;
            // Colleghiamo la callback CDC
            USB_DEVICE_CDC_EventHandlerSet(USB_DEVICE_CDC_INDEX_0, APP_USBDeviceCDCEventHandler, 0);
            break;
        case USB_DEVICE_EVENT_SUSPENDED:
        case USB_DEVICE_EVENT_DETACHED:
            is_usb_configured = false;
            break;
        default: break;
    }
}

// --- Main Application ---
int main(void) {
    // 1. Inizializzazione Hardware Completa (Generata da Harmony)
    SYS_Initialize(NULL); 
    
    srand(12345); 
    gf2_initialize(); 

    // Apriamo il driver USB
    USB_DEVICE_EventHandlerSet(APP_USBDeviceEventHandler, 0);
    USB_DEVICE_Attach(USB_DEVICE_INDEX_0);

    // Variabile di stato per il nostro ciclo
    enum { WAIT_FOR_CONFIG, WAIT_FOR_READ, PROCESS_DATA, WAIT_FOR_WRITE } appState = WAIT_FOR_CONFIG;

    while (1) {
        // Mantiene in vita i task di sistema (USB, ecc.)
        SYS_Tasks();

        switch(appState) {
            case WAIT_FOR_CONFIG:
                if (is_usb_configured) {
                    // Cavo collegato e driver riconosciuto dal PC. Mettiamoci in ascolto.
                    is_read_complete = false;
                    USB_DEVICE_CDC_Read(USB_DEVICE_CDC_INDEX_0, &readTransferHandle, rx_buffer, PAYLOAD_SIZE_BYTES);
                    appState = WAIT_FOR_READ;
                }
                break;

            case WAIT_FOR_READ:
                if (!is_usb_configured) { appState = WAIT_FOR_CONFIG; break; } // Cavo staccato
                
                if (is_read_complete) {
                    // Dati ricevuti! Passiamo alla matematica
                    appState = PROCESS_DATA;
                }
                break;

            case PROCESS_DATA:
                // 3. Encode
                gf2_encode_data(rx_buffer, PAYLOAD_SIZE_BYTES, codeword_buffer);

                // 4. Corrupt
                inject_errors(codeword_buffer, CODEWORD_SIZE_BYTES, TARGET_BIT_FLIPS);

                // 5. Memory Simulation (Per ora copia diretta)
                for(int i=0; i<CODEWORD_SIZE_BYTES; i++) flash_read_buffer[i] = codeword_buffer[i];

                // 6. Decode
                uint8_t crc_status = 0;
                int bch_status = gf2_correct_errors(flash_read_buffer, CODEWORD_SIZE_BYTES, &crc_status);

                // Prepariamo il pacchetto di risposta (603 byte totali)
                for(int i=0; i<CODEWORD_SIZE_BYTES; i++) tx_buffer[i] = flash_read_buffer[i];
                tx_buffer[CODEWORD_SIZE_BYTES]     = (bch_status >> 0) & 0xFF;
                tx_buffer[CODEWORD_SIZE_BYTES + 1] = (bch_status >> 8) & 0xFF;
                tx_buffer[CODEWORD_SIZE_BYTES + 2] = (bch_status >> 16) & 0xFF;
                tx_buffer[CODEWORD_SIZE_BYTES + 3] = (bch_status >> 24) & 0xFF;
                tx_buffer[CODEWORD_SIZE_BYTES + 4] = crc_status;

                // 7. Trasmetti
                is_write_complete = false;
                USB_DEVICE_CDC_Write(USB_DEVICE_CDC_INDEX_0, &writeTransferHandle, tx_buffer, sizeof(tx_buffer), USB_DEVICE_CDC_TRANSFER_FLAGS_DATA_COMPLETE);
                appState = WAIT_FOR_WRITE;
                break;

            case WAIT_FOR_WRITE:
                if (!is_usb_configured) { appState = WAIT_FOR_CONFIG; break; }
                
                if (is_write_complete) {
                    // Invio completato. Rimettiamoci subito in ascolto per il prossimo blocco!
                    is_read_complete = false;
                    USB_DEVICE_CDC_Read(USB_DEVICE_CDC_INDEX_0, &readTransferHandle, rx_buffer, PAYLOAD_SIZE_BYTES);
                    appState = WAIT_FOR_READ;
                }
                break;
        }
    }
    return 0;
}