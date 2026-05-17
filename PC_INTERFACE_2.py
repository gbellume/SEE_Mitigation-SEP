import serial
import time
import random

# --- Configuration ---
COM_PORT = 'COM3'      
BAUD_RATE = 115200
SEND_SIZE = 512        # payload
RESPONSE_SIZE = 520

# Impostazioni del Test
NUM_WORDS_TO_TEST = 100000

def main():
    print(f"Opening {COM_PORT} at {BAUD_RATE} baud...")
    try:
        with serial.Serial(COM_PORT, BAUD_RATE, timeout=2) as ser:
            print(f"Inizio Stress Test: invio di {NUM_WORDS_TO_TEST} parole...\n")
            
            # Contatori per le statistiche
            successful_responses = 0
            total_bch_corrections = 0
            total_crc_errors = 0
            
            start_time = time.time()
            
            for i in range(NUM_WORDS_TO_TEST):
                # 1. Genera dati casuali (512 byte)
                payload = bytearray(random.getrandbits(8) for _ in range(SEND_SIZE))
                
                # 2. Invia al Microcontrollore
                ser.write(payload)
                
                # 3. Leggi la risposta
                response = ser.read(RESPONSE_SIZE)
                
                if len(response) == RESPONSE_SIZE:
                    successful_responses += 1
                    
                    # 4. Estrai i byte di stato alla fine della parola (Byte 520, 521, 522, 523, 524)
                    bch_status = int.from_bytes(response[520:524], byteorder='little')
                    crc_status = response[524]
                    
                    # Logica di conteggio (adatta questi if in base a cosa ritorna il tuo C++)
                    if bch_status > 0:
                        total_bch_corrections += bch_status # Assumendo che ritorni il numero di errori corretti
                        
                    if crc_status != 0:
                        total_crc_errors += 1
                        
                else:
                    print(f"Timeout o errore alla parola {i}! Ricevuti {len(response)}/{RESPONSE_SIZE} byte.")
                
                # (Opzionale) Stampa un puntino ogni 100 parole per far vedere che non è bloccato
                if (i + 1) % 100 == 0:
                    print(f"Progresso: {i + 1}/{NUM_WORDS_TO_TEST} parole testate...")

            end_time = time.time()
            
            # --- STAMPA DEL REPORT FINALE ---
            elapsed_time = end_time - start_time
            throughput = (NUM_WORDS_TO_TEST * SEND_SIZE) / elapsed_time / 1024 # KB/s
            
            print("\n" + "="*40)
            print(" RISULTATI STRESS TEST EDAC")
            print("="*40)
            print(f"Tempo impiegato:       {elapsed_time:.2f} secondi")
            print(f"Throughput in invio:   {throughput:.2f} KB/s")
            print(f"Pacchetti Ricevuti:    {successful_responses} / {NUM_WORDS_TO_TEST}")
            print(f"Correzioni BCH totali: {total_bch_corrections}")
            print(f"Fallimenti CRC totali: {total_crc_errors}")
            print("="*40)
            
    except serial.SerialException as e:
        print(f"Impossibile connettersi alla scheda. Errore: {e}")

if __name__ == '__main__':
    main()