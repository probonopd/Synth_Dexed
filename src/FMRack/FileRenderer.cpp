#include "FileRenderer.h"
#include "Rack.h"
#include "Performance.h"
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <sstream>

namespace FMRack {

// Minimal MIDI file parser
struct MidiEvent {
    uint32_t tick;
    std::vector<uint8_t> data;
};
struct MidiTrack {
    std::vector<MidiEvent> events;
};
struct MidiFile {
    uint16_t format = 0;
    uint16_t ntrks = 0;
    uint16_t division = 0;
    std::vector<MidiTrack> tracks;
    bool load(const std::string& filename);
};
static uint32_t read_be32(const uint8_t* p) { return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; }
static uint16_t read_be16(const uint8_t* p) { return (p[0]<<8)|p[1]; }
static uint32_t read_vlq(std::istream& f) {
    uint32_t v = 0; uint8_t b;
    do { f.read((char*)&b, 1); v = (v<<7)|(b&0x7F); } while (b&0x80);
    return v;
}
bool MidiFile::load(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) return false;
    uint8_t hdr[14];
    f.read((char*)hdr, 14);
    if (std::memcmp(hdr, "MThd", 4) != 0) return false;
    format = read_be16(hdr+8);
    ntrks = read_be16(hdr+10);
    division = read_be16(hdr+12);
    tracks.clear();
    for (int t = 0; t < ntrks; ++t) {
        uint8_t trkhdr[8];
        f.read((char*)trkhdr, 8);
        if (std::memcmp(trkhdr, "MTrk", 4) != 0) return false;
        uint32_t trklen = read_be32(trkhdr+4);
        std::vector<uint8_t> trkdata(trklen);
        f.read((char*)trkdata.data(), trklen);
        MidiTrack track;
        size_t pos = 0; uint32_t tick = 0; uint8_t running = 0;
        while (pos < trklen) {
            std::istringstream s(std::string((char*)&trkdata[pos], trklen-pos));
            uint32_t dt = read_vlq(s);
            std::streamoff vlq_len_off = s.tellg();
            size_t vlq_len = vlq_len_off >= 0 ? static_cast<size_t>(vlq_len_off) : 0;
            pos += vlq_len; tick += dt;
            uint8_t b = trkdata[pos++];
            if (b < 0x80) { --pos; b = running; } else { running = b; }
            MidiEvent ev; ev.tick = tick;
            if ((b & 0xF0) == 0xF0) {
                if (b == 0xFF) {
                    uint8_t type = trkdata[pos++];
                    std::istringstream s2(std::string((char*)&trkdata[pos], trklen-pos));
                    uint32_t len = read_vlq(s2);
                    std::streamoff vlq2_off = s2.tellg();
                    size_t vlq2 = vlq2_off >= 0 ? static_cast<size_t>(vlq2_off) : 0;
                    pos += vlq2;
                    ev.data = {0xFF, type};
                    using diff_t = std::vector<uint8_t>::difference_type;
                    ev.data.insert(ev.data.end(), trkdata.begin()+static_cast<diff_t>(pos), trkdata.begin()+static_cast<diff_t>(pos+len));
                    pos += len;
                } else if (b == 0xF0 || b == 0xF7) {
                    std::istringstream s2(std::string((char*)&trkdata[pos], trklen-pos));
                    uint32_t len = read_vlq(s2);
                    std::streamoff vlq2_off = s2.tellg();
                    size_t vlq2 = vlq2_off >= 0 ? static_cast<size_t>(vlq2_off) : 0;
                    pos += vlq2;
                    ev.data = {b};
                    using diff_t = std::vector<uint8_t>::difference_type;
                    ev.data.insert(ev.data.end(), trkdata.begin()+static_cast<diff_t>(pos), trkdata.begin()+static_cast<diff_t>(pos+len));
                    pos += len;
                }
            } else {
                ev.data.push_back(b);
                int datalen = (b>=0xC0&&b<=0xDF)?1:2;
                for (int i=0; i<datalen; ++i) ev.data.push_back(trkdata[pos++]);
            }
            track.events.push_back(ev);
        }
        tracks.push_back(track);
    }
    return true;
}

static void write_wav(const std::string& filename, const std::vector<int16_t>& data, int samplerate) {
    std::ofstream f(filename, std::ios::binary);
    int32_t datasz = (int32_t)data.size()*2;
    int32_t fmtlen = 16, audiofmt = 1, numch = 2, byterate = samplerate*4, align = 4, bits = 16;
    f.write("RIFF",4); int32_t sz = datasz+36; f.write((char*)&sz,4);
    f.write("WAVEfmt ",8); f.write((char*)&fmtlen,4); f.write((char*)&audiofmt,2); f.write((char*)&numch,2);
    f.write((char*)&samplerate,4); f.write((char*)&byterate,4); f.write((char*)&align,2); f.write((char*)&bits,2);
    f.write("data",4); f.write((char*)&datasz,4); f.write((char*)data.data(), datasz);
}

bool FileRenderer::renderMidiToWav(Rack* rack,
                                   const std::string& midiFile, const std::string& wavFile,
                                   int sampleRate, int bufferFrames) {
    MidiFile midi;
    if (!midi.load(midiFile)) {
        std::cerr << "Failed to load MIDI file: " << midiFile << std::endl;
        return false;
    }
    // Flatten all events from all tracks, sort by tick
    struct FlatEvent { uint32_t tick; std::vector<uint8_t> data; };
    std::vector<FlatEvent> events;
    for (const auto& trk : midi.tracks) {
        for (const auto& ev : trk.events) {
            events.push_back({ev.tick, ev.data});
        }
    }
    std::sort(events.begin(), events.end(), [](const FlatEvent& a, const FlatEvent& b) { return a.tick < b.tick; });
    // std::cout << "Loaded MIDI events: " << events.size() << std::endl;
    // MIDI timing
    double ticksPerQuarter = midi.division;
    double tempo = 500000.0; // default 120 BPM (microseconds per quarter)
    double tickTime = tempo / 1000000.0 / ticksPerQuarter; // seconds per tick
    // Removed unused: double curTime = 0.0;
    // Removed unused: uint32_t curTick = 0;
    std::vector<int16_t> audio;
    std::vector<float> left(static_cast<size_t>(bufferFrames)), right(static_cast<size_t>(bufferFrames));
    // Render loop (sample-accurate MIDI timing)
    int tailFrames = sampleRate * 2; // render 2s tail after last event
    int tailLeft = 0;
    double bufferStartTime = 0.0;
    // Removed unused: uint32_t bufferStartTick = 0;
    // Removed unused: int loopCount = 0;
    size_t eventIdx = 0;
    while (eventIdx < events.size() || tailLeft > 0) {
        int framesRendered = 0;
        while (framesRendered < bufferFrames) {
            int framesToNextEvent = bufferFrames - framesRendered;
            if (eventIdx < events.size()) {
                double eventTime = events[eventIdx].tick * tickTime;
                double curTimeInner = bufferStartTime + (framesRendered / (double)sampleRate);
                int eventOffset = (int)std::round((eventTime - curTimeInner) * sampleRate);
                if (eventOffset < 0) eventOffset = 0;
                if (eventOffset < framesToNextEvent) framesToNextEvent = eventOffset;
            } else {
                framesToNextEvent = bufferFrames - framesRendered;
            }
            if (framesToNextEvent <= 0) framesToNextEvent = 1;
            rack->processAudio(left.data() + static_cast<size_t>(framesRendered), right.data() + static_cast<size_t>(framesRendered), framesToNextEvent);
            for (int i = 0; i < framesToNextEvent; ++i) {
                int16_t l = (int16_t)std::clamp(left[static_cast<size_t>(framesRendered) + static_cast<size_t>(i)] * 32767.0f, -32768.0f, 32767.0f);
                int16_t r = (int16_t)std::clamp(right[static_cast<size_t>(framesRendered) + static_cast<size_t>(i)] * 32767.0f, -32768.0f, 32767.0f);
                audio.push_back(l);
                audio.push_back(r);
            }
            framesRendered += framesToNextEvent;
            bufferStartTime += framesToNextEvent / (double)sampleRate;
            // Removed unused: bufferStartTick = (uint32_t)(bufferStartTime / tickTime + 0.5);
            if (eventIdx >= events.size() && tailLeft > 0) tailLeft -= framesToNextEvent;
            while (eventIdx < events.size()) {
                double eventTime = events[eventIdx].tick * tickTime;
                double curTimeInner2 = bufferStartTime;
                if (eventTime <= curTimeInner2 + (framesToNextEvent / (double)sampleRate)) {
                    const auto& data = events[eventIdx].data;
                    if (data.size() >= 1 && data[0] == 0xFF && data.size() >= 3 && data[1] == 0x51) {
                        if (data.size() >= 6) {
                            tempo = (data[3] << 16) | (data[4] << 8) | data[5];
                            tickTime = tempo / 1000000.0 / ticksPerQuarter;
                        }
                    } else if (data.size() == 3 && (data[0] & 0xF0) != 0xF0) {
                        rack->processMidiMessage(data[0], data[1], data[2]);
                    } else if (data.size() == 2 && (data[0] & 0xF0) == 0xC0) {
                        rack->processMidiMessage(data[0], data[1], 0);
                    } else if (data.size() > 0 && data[0] == 0xF0) {
                        rack->routeSysexToModules(data.data(), (int)data.size(), 0);
                    }
                    ++eventIdx;
                    tailLeft = tailFrames;
                } else {
                    break;
                }
            }
        }
    }
    write_wav(wavFile, audio, sampleRate);
    std::cout << "Rendered " << audio.size() / 2 << " stereo samples to " << wavFile << std::endl;
    return true;
}

} // namespace FMRack
