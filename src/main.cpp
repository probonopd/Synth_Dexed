// Standalone synth using WinMM for MIDI and waveOut for audio (Windows)
// or ALSA for audio and MIDI (Linux)
#ifdef _WIN32
#include "dexed.h"
#include <windows.h>
#include <mmsystem.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <string>

#pragma comment(lib, "winmm.lib")

constexpr unsigned int SAMPLE_RATE = 44100;
constexpr unsigned int BUFFER_FRAMES = 512;
constexpr uint8_t MAX_NOTES = 16;
constexpr unsigned int NUM_BUFFERS = 4;

std::atomic<bool> running{true};
Dexed* synth = nullptr;

// Audio buffer
std::vector<short> audioBuffers[NUM_BUFFERS];
WAVEHDR waveHeaders[NUM_BUFFERS];
HWAVEOUT hWaveOut = nullptr;

// MIDI callback
void CALLBACK midiInProc(HMIDIIN, UINT uMsg, DWORD_PTR, DWORD_PTR dwParam1, DWORD_PTR, DWORD_PTR) {
    if (!synth) return;
    if (uMsg == MIM_DATA) {
        DWORD msg = (DWORD)dwParam1;
        uint8_t status = msg & 0xF0;
        uint8_t note = (msg >> 8) & 0x7F;
        uint8_t velocity = (msg >> 16) & 0x7F;
        std::cout << "MIDI: status=0x" << std::hex << (int)status
                  << " note=" << std::dec << (int)note
                  << " velocity=" << (int)velocity << std::endl;
        if (status == 0x90 && velocity > 0) {
            synth->keydown(note, velocity);
        } else if ((status == 0x80) || (status == 0x90 && velocity == 0)) {
            synth->keyup(note);
        }
    }
}

void audioThread() {
    int bufferIndex = 0;
    while (running) {
        WAVEHDR& hdr = waveHeaders[bufferIndex];
        if (!(hdr.dwFlags & WHDR_INQUEUE)) {
            // Zero the buffer before filling
            std::fill(audioBuffers[bufferIndex].begin(), audioBuffers[bufferIndex].end(), 0);
            synth->getSamples(reinterpret_cast<int16_t*>(audioBuffers[bufferIndex].data()), BUFFER_FRAMES);
            waveOutWrite(hWaveOut, &hdr, sizeof(WAVEHDR));
        }
        bufferIndex = (bufferIndex + 1) % NUM_BUFFERS;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

int main() {
    synth = new Dexed(MAX_NOTES, SAMPLE_RATE);
    synth->loadInitVoice();
    // --- FORCE ALL OPERATOR OUTPUT LEVELS TO 99 AND ALGORITHM TO 0 FOR TESTING ---
    for (int op = 0; op < 6; ++op) {
        synth->setOPOutputLevel(op, 99); // Set all operator output levels to max
    }
    synth->setAlgorithm(0); // Set algorithm to 0 (all carriers)
    // --------------------------------------------------------------------------

    // List audio devices
    UINT numAudioDevs = waveOutGetNumDevs();
    std::cout << "Available audio output devices:" << std::endl;
    for (UINT i = 0; i < numAudioDevs; ++i) {
        WAVEOUTCAPS caps;
        waveOutGetDevCaps(i, &caps, sizeof(caps));
        std::wcout << L"  [" << i << L"] " << caps.szPname << std::endl;
    }
    std::cout << "Select audio device number: ";
    UINT audioDev = 0;
    std::cin >> audioDev;
    std::cin.ignore();

    // List MIDI input devices
    UINT numMidiDevs = midiInGetNumDevs();
    std::cout << "Available MIDI input devices:" << std::endl;
    for (UINT i = 0; i < numMidiDevs; ++i) {
        MIDIINCAPS caps;
        midiInGetDevCaps(i, &caps, sizeof(caps));
        std::wcout << L"  [" << i << L"] " << caps.szPname << std::endl;
    }
    std::cout << "Select MIDI input device number: ";
    UINT midiDev = 0;
    std::cin >> midiDev;
    std::cin.ignore();

    // Set up audio output
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;
    if (waveOutOpen(&hWaveOut, audioDev, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        std::cerr << "Failed to open audio device!\n";
        return 1;
    }
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        audioBuffers[i].resize(BUFFER_FRAMES);
        waveHeaders[i] = {};
        waveHeaders[i].lpData = reinterpret_cast<LPSTR>(audioBuffers[i].data());
        waveHeaders[i].dwBufferLength = BUFFER_FRAMES * sizeof(short);
        waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
    }
    std::thread audio(audioThread);

    // Set up MIDI input
    HMIDIIN hMidiIn = nullptr;
    bool midiAvailable = (numMidiDevs > 0);
    if (midiAvailable && midiInOpen(&hMidiIn, midiDev, (DWORD_PTR)midiInProc, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
        midiInStart(hMidiIn);
        std::cout << "Listening for MIDI on port " << midiDev << "...\n";
    } else {
        std::cerr << "Warning: No MIDI input available. Synth will play a C major chord for 1 second.\n";
        synth->keydown(60, 100); // C4
        synth->keydown(64, 100); // E4
        synth->keydown(67, 100); // G4
        std::this_thread::sleep_for(std::chrono::seconds(1));
        synth->notesOff();
    }

    std::cout << "Synth_Dexed running. Press Enter to quit.\n";
    std::cin.get();
    running = false;
    audio.join();

    // Cleanup
    if (hMidiIn) {
        midiInStop(hMidiIn);
        midiInClose(hMidiIn);
    }
    waveOutReset(hWaveOut);
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        waveOutUnprepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
    }
    waveOutClose(hWaveOut);
    delete synth;
    return 0;
}
#else // Linux/ALSA
#include "dexed.h"
#include <alsa/asoundlib.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <string>

constexpr unsigned int SAMPLE_RATE = 44100;
constexpr unsigned int BUFFER_FRAMES = 512;
constexpr uint8_t MAX_NOTES = 16;
constexpr unsigned int NUM_BUFFERS = 4;

std::atomic<bool> running{true};
Dexed* synth = nullptr;

void alsa_audio_thread(snd_pcm_t* pcm_handle) {
    std::vector<int16_t> buffer(BUFFER_FRAMES);
    while (running) {
        synth->getSamples(buffer.data(), BUFFER_FRAMES);
        snd_pcm_writei(pcm_handle, buffer.data(), BUFFER_FRAMES);
    }
}

void alsa_midi_thread(snd_seq_t* seq_handle, int in_port) {
    snd_seq_event_t* ev = nullptr;
    while (running) {
        snd_seq_event_input(seq_handle, &ev);
        if (!ev) continue;
        if (ev->type == SND_SEQ_EVENT_NOTEON) {
            synth->keydown(ev->data.note.note, ev->data.note.velocity);
        } else if (ev->type == SND_SEQ_EVENT_NOTEOFF) {
            synth->keyup(ev->data.note.note);
        }
    }
}

int main() {
    synth = new Dexed(MAX_NOTES, SAMPLE_RATE);
    synth->loadInitVoice();
    // --- FORCE ALL OPERATOR OUTPUT LEVELS TO 99 AND ALGORITHM TO 0 FOR TESTING ---
    for (int op = 0; op < 6; ++op) {
        synth->setOPOutputLevel(op, 99); // Set all operator output levels to max
    }
    synth->setAlgorithm(0); // Set algorithm to 0 (all carriers)
    // --------------------------------------------------------------------------

    // List ALSA audio devices (PCM)
    void **hints;
    snd_device_name_hint(-1, "pcm", &hints);
    std::vector<std::string> audioDevs;
    std::cout << "Available ALSA audio output devices:" << std::endl;
    for (void **hint = hints; *hint != nullptr; ++hint) {
        char* name = snd_device_name_get_hint(*hint, "NAME");
        if (name && std::string(name) != "null" && std::string(name).find("hw:") == 0) {
            audioDevs.push_back(name);
            std::cout << "  [" << (audioDevs.size()-1) << "] " << name << std::endl;
        }
        if (name) free(name);
    }
    if (audioDevs.empty()) {
        std::cerr << "No usable ALSA audio output devices found! Exiting.\n";
        snd_device_name_free_hint(hints);
        delete synth;
        return 1;
    }
    std::cout << "Select audio device number: ";
    int audioDev = 0;
    std::cin >> audioDev;
    std::cin.ignore();
    std::string audioDevName = audioDevs[audioDev];
    snd_device_name_free_hint(hints);

    // List ALSA MIDI input ports
    snd_seq_t* seq_handle;
    snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_INPUT, 0);
    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t* pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);
    std::vector<std::pair<int,int>> midiPorts;
    std::cout << "Available ALSA MIDI input ports:" << std::endl;
    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq_handle, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq_handle, pinfo) >= 0) {
            if ((snd_seq_port_info_get_capability(pinfo) & SND_SEQ_PORT_CAP_WRITE) &&
                (snd_seq_port_info_get_capability(pinfo) & SND_SEQ_PORT_CAP_SUBS_WRITE)) {
                midiPorts.emplace_back(client, snd_seq_port_info_get_port(pinfo));
                std::cout << "  [" << (midiPorts.size()-1) << "] client " << client << ", port " << snd_seq_port_info_get_port(pinfo) << std::endl;
            }
        }
    }
    bool midiAvailable = !midiPorts.empty();
    int midiPortIdx = 0;
    if (midiAvailable) {
        std::cout << "Select MIDI input port number: ";
        std::cin >> midiPortIdx;
        std::cin.ignore();
    } else {
        std::cerr << "Warning: No ALSA MIDI input ports found. Synth will run without MIDI input.\n";
        // Play C major chord for 1 second, then all notes off
        synth->keydown(60, 100); // C4
        synth->keydown(64, 100); // E4
        synth->keydown(67, 100); // G4
        std::this_thread::sleep_for(std::chrono::seconds(1));
        synth->notesOff();
    }
    auto midiClient = midiAvailable ? midiPorts[midiPortIdx].first : -1;
    auto midiPort = midiAvailable ? midiPorts[midiPortIdx].second : -1;
    snd_seq_close(seq_handle);

    // ALSA audio setup
    snd_pcm_t* pcm_handle;
    if (snd_pcm_open(&pcm_handle, audioDevName.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "Failed to open ALSA audio device: " << audioDevName << "\n";
        delete synth;
        return 1;
    }
    snd_pcm_set_params(pcm_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, SAMPLE_RATE, 1, 500000);
    // ALSA MIDI setup
    std::thread midi;
    if (midiAvailable) {
        snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_INPUT, 0);
        snd_seq_set_client_name(seq_handle, "Synth_Dexed");
        int in_port = snd_seq_create_simple_port(seq_handle, "Input",
            SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
            SND_SEQ_PORT_TYPE_APPLICATION);
        snd_seq_connect_from(seq_handle, in_port, midiClient, midiPort);
        midi = std::thread(alsa_midi_thread, seq_handle, in_port);
    }
    std::thread audio(alsa_audio_thread, pcm_handle);
    std::cout << "Synth_Dexed running (ALSA). Press Enter to quit.\n";
    std::cin.get();
    running = false;
    audio.join();
    if (midiAvailable) midi.join();
    snd_pcm_close(pcm_handle);
    if (midiAvailable) snd_seq_close(seq_handle);
    delete synth;
    return 0;
}
#endif
