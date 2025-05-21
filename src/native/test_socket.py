import socket
import time

HOST = '127.0.0.1'
PORT = 51662

def send_midi_message(midi_bytes):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.sendto(bytes(midi_bytes), (HOST, PORT))
        print(f"Sent: {midi_bytes}")

if __name__ == "__main__":
    brass_voice = "F0 43 10 00 01 1B 4D 38 14 46 63 00 00 00 31 00 00 00 00 07 00 01 4F 00 07 15 07 30 37 16 32 62 3D 3E 00 31 00 00 00 00 00 00 01 46 00 03 06 06 42 5C 16 32 35 3D 3E 00 31 00 00 00 00 00 00 01 4F 00 01 00 07 2E 23 16 38 63 56 56 00 31 00 00 00 00 01 00 03 4F 00 01 00 07 25 22 0F 40 55 00 00 00 31 00 00 00 00 02 00 02 43 00 01 00 07 39 18 13 3C 63 56 56 00 31 00 00 00 00 02 00 02 63 00 01 00 07 5E 43 5F 63 35 31 32 32 11 07 01 1F 00 00 00 00 00 01 0C 48 4F 52 4E 20 53 45 43 2E 41 5A F7"
    brass_voice_bytes = bytes.fromhex(brass_voice.replace(" ", ""))
    send_midi_message(brass_voice_bytes)  # Send the BRASS 1 voice
    time.sleep(0.5)

    send_midi_message([0x90, 60, 100])  # Note On
    time.sleep(0.5)
    send_midi_message([0x80, 60, 0])    # Note Off
    time.sleep(0.5)
    send_midi_message([0x90, 62, 100])  # Note On for D2
    time.sleep(0.5)
    send_midi_message([0x80, 62, 0])    # Note Off for D2
    time.sleep(0.5)
    send_midi_message([0x90, 64, 100])  # Note On for E2
    time.sleep(0.5)
    send_midi_message([0x80, 64, 0])    # Note Off for E2
    time.sleep(0.5)