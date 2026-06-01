import serial
import time

# INSERISCI QUI LA TUA COM PORT (es. 'COM3', 'COM5', '/dev/ttyACM0')
SERIAL_PORT = 'COM10' 

def read_from_mcu():
    print(f"Tentativo di connessione a {SERIAL_PORT}...")
    try:
        # Apre la porta seriale
        with serial.Serial(SERIAL_PORT, baudrate=9600, timeout=1) as ser:
            
            # CRITICO: Forza i segnali hardware per dire all'MCU "Sono pronto ad ascoltare"
            ser.setDTR(True)
            ser.setRTS(True)
            time.sleep(0.5) # Pausa per far recepire il segnale all'MCU
            
            print("Connesso! In attesa di messaggi dall'MCU...\n")
            
            while True:
                # Legge una riga finché non trova \n
                line = ser.readline()
                
                if line:
                    # Decodifica i byte in stringa di testo
                    print("MCU Says:", line.decode('utf-8', errors='ignore').strip())
                    
    except serial.SerialException as e:
        print(f"\n[ERRORE SERIALE] Impossibile aprire la porta. Dettagli: {e}")
        print("Assicurati di aver inserito la COM port corretta e che la scheda sia collegata.")
    except KeyboardInterrupt:
        print("\nTest interrotto dall'utente.")

if __name__ == "__main__":
    read_from_mcu()