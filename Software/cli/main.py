import socket
import struct
import time

# Konfiguracja IP (taka sama jak w Pico)
PICO_IP = "192.168.4.1"
PICO_PORT = 4242

def calculate_checksum(data: bytes) -> int:
    """Oblicza prostą sumę XOR dla danych."""
    cs = 0
    for b in data:
        cs ^= b
    return cs

def parse_telemetry(frame: bytes, last_cmd_type=None):
    """
    Rozparsowuje ramkę TM (10 bajtów) i wyświetla ją w czytelnej formie.
    last_cmd_type: 'MOVE' lub 'READ' - pomaga zinterpretować dane.
    """
    if len(frame) != 10:
        print(f"[!] Błąd: Otrzymano niepełną ramkę o długości {len(frame)} bajtów.")
        return

    header = frame[0:2]
    if header != b"TM":
        print(f"[!] Błąd: Nieprawidłowy nagłówek: {header}")
        return

    # --- POPRAWKA TUTAJ (zmieniono [2:8] na [2:9]) ---
    # Status(1) + Value(2) + Timestamp(4) = 7 bajtów
    try:
        status, value, timestamp = struct.unpack("!BHI", frame[2:9])
    except struct.error as e:
        print(f"[!] Błąd rozpakowywania danych: {e}")
        return

    received_checksum = frame[9]

    # Weryfikacja sumy kontrolnej (liczymy dla pierwszych 9 bajtów)
    calculated_cs = calculate_checksum(frame[:9])

    if calculated_cs != received_checksum:
        print(f"[!] Błąd sumy kontrolnej! Otrzymana: {received_checksum}, Obliczona: {calculated_cs}")
        return

    # --- INTERPRETACJA WYNIKÓW ---
    status_str = "OK" if status == 0 else f"BŁĄD (Kod {status})"
    
    print("\n" + "="*30)
    print(f" ODPOWIEDŹ Z PICO (Czas: {timestamp} ms)")
    print("="*30)
    print(f" Status:      {status_str}")
    print(f" Dane surowe: {value}")
    
    # Dodatkowa interpretacja zależna od tego, co wysłaliśmy
    if last_cmd_type == 'READ':
        # Wartość z ADC (0 - 4095)
        voltage = (value * 3.3) / 4095
        print(f" Interpretacja: Odczyt czujnika")
        print(f" Napięcie:    {voltage:.2f} V")
        if value > 2000:
            print(" >> WNIOSEK: Wykryto OKNO (Silny sygnał)")
        else:
            print(" >> WNIOSEK: Zasłonięte (Słaby sygnał)")
            
    elif last_cmd_type == 'MOVE':
        # Zwraca indeks pozycji
        print(f" Interpretacja: Ruch zakończony na pozycję #{value}")

    print("="*30 + "\n")

def main():
    print(f"Łączenie z {PICO_IP}:{PICO_PORT}...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(5) # Timeout 5 sekund
        s.connect((PICO_IP, PICO_PORT))
        print("Połączono!")
    except Exception as e:
        print(f"Nie można połączyć: {e}")
        return

    while True:
        try:
            print("\n--- MENU KOMEND ---")
            print("0: MOVE (Ruch do filtra)")
            print("1: READ (Odczyt czujnika)")
            print("2: CONTROL (Ruch o określony kąt)")
            print("q: Wyjście")
            
            choice = input("Wybierz komendę: ").strip()
            
            if choice.lower() == 'q':
                break
            
            cmd_id = -1
            p1 = 0
            p2 = 0
            cmd_type = 'UNKNOWN'

            if choice == '0':
                cmd_id = 0
                cmd_type = 'MOVE'
                print("  Opcje: 0=Koło A (domyślne)")
                p1 = 0 
                p2 = int(input("  Podaj indeks filtra (0-3): "))
            
            elif choice == '1':
                cmd_id = 1
                cmd_type = 'READ'
                print("  Opcje: 0=IR (Wykrywanie okien), 1=UV")
                p1 = int(input("  Wybierz czujnik (0 lub 1): "))
                p2 = 0

            elif choice == '2':
                cmd_id = 2
                cmd_type = 'CONTROL'
                print("  Opcje: 0=Koło A (domyślne)")
                p1 = 0 
                p2 = int(input("  Podaj kąt: "))
            
            else:
                print("Nieznana komenda.")
                continue

            # Budowanie ramki TC (TeleCommand)
            # ! = Big Endian, 2s = string 2 znaki, B = unsigned char (1 bajt)
            payload = struct.pack("!2sBBB", b"TC", cmd_id, p1, p2)
            cs = calculate_checksum(payload)
            frame = payload + struct.pack("!B", cs)

            # Wysyłanie
            s.sendall(frame)

            # Odbieranie odpowiedzi (Czekamy na dokładnie 10 bajtów)
            response = s.recv(10)
            
            if not response:
                print("Rozłączono przez serwer.")
                break
                
            parse_telemetry(response, cmd_type)

        except ValueError:
            print("Błąd: Wprowadź poprawną liczbę!")
        except socket.timeout:
            print("Błąd: Brak odpowiedzi od Pico (timeout).")
        except KeyboardInterrupt:
            print("\nZamykanie...")
            break

    s.close()

if __name__ == "__main__":
    main()