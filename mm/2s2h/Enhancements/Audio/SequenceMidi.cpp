#include "SequenceMidi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/File.h>
#include <tinyxml2.h>

#include "AudioCollection.h"
#include "2s2h/BenPort.h"
#include "2s2h/BenGui/Translation.h"
#include "2s2h/resource/type/AudioSequence.h"

extern "C" {
extern char** gSequenceMap;
extern size_t gSequenceMapSize;
extern char** gFontMap;
extern size_t gFontMapSize;
}

namespace fs = std::filesystem;

namespace SequenceMidi {

namespace {

// The sequence player, reduced to what decides which notes play when. It follows AudioScript_* in
// src/audio/lib/seqplayer.c command by command; sound (envelopes, vibrato, filters, reverb) is left out.

constexpr int kTatumsPerBeat = 48;
constexpr int kChannels = 16;
constexpr int kLayers = 4;
constexpr int kIoNone = -1;
constexpr uint32_t kMaxTicks = kTatumsPerBeat * 120 * 15; // 15 minutes at 120 BPM

const uint8_t kDefaultShortVelocity[16] = { 12, 25, 38, 51, 57, 64, 71, 76, 83, 89, 96, 102, 109, 115, 121, 127 };
const uint8_t kDefaultShortGate[16] = { 229, 203, 177, 151, 139, 126, 113, 100, 87, 74, 61, 48, 36, 23, 10, 0 };

struct Script {
    int pc = 0;
    int depth = 0;
    int stack[4] = {};
    int loops[4] = {};
    int value = 0;
};

struct Layer {
    bool enabled = false;
    Script s;
    int delay = 0;
    int gateTime = 0x80;
    int transposition = 0;
    int shortDelay = 0;
    int lastDelay = 0;
    float velocitySquare = 0.5f;
    int instOrWave = 0xFF; // 0xFF: the channel's
    int instrument = -1;
};

struct Channel {
    bool enabled = false;
    bool stopScript = false;
    Script s;
    int delay = 0;
    int transposition = 0;
    bool largeNotes = false;
    bool hasInstrument = false;
    int instOrWave = 0;
    int instrument = -1;
    int dynTable = -1;
    int unk22 = 0;
    int io[8];
    Layer layers[kLayers];
    // first unconditional backward jump, for songs that loop per channel
    int64_t loopFromTick = -1;
    int64_t loopToTick = -1;
    std::map<int, uint32_t> visited;
};

struct Event {
    uint32_t tick;
    int order; // note-offs first, then controllers, then note-ons
    std::vector<uint8_t> bytes;
};

class Player {
  public:
    Player(const std::vector<uint8_t>& seq, const std::vector<std::string>& names) : d(seq), mNames(names) {
        for (Channel& c : ch) {
            std::fill(std::begin(c.io), std::end(c.io), kIoNone);
        }
        std::fill(std::begin(io), std::end(io), kIoNone);
    }

    bool Run(Result& result, std::string* error);

  private:
    std::vector<uint8_t> d;
    const std::vector<std::string>& mNames;
    bool ok = true;
    std::string err;

    Script s;
    int delay = 0;
    int transposition = 0;
    int tempo = 120 * kTatumsPerBeat;
    int tempoChange = 0;
    int io[8];
    int shortVelocity = -1; // offsets into the script, -1 for the defaults
    int shortGate = -1;
    bool ended = false;
    Channel ch[kChannels];
    uint32_t tick = 0;
    std::map<int, uint32_t> visited; // sequence script command -> first tick it ran
    bool startedChannel = false;
    std::map<int, bool> startsSince; // per visited target: was a channel started after it
    int64_t loopStart = -1, loopEnd = -1;

    std::vector<Event> tracks[kChannels];
    std::vector<Event> tempoTrack;
    int lastProgram[kChannels];
    std::string trackNames[kChannels];
    int lastTempo = -1;

    uint8_t U8(Script& sc) {
        if (sc.pc < 0 || sc.pc >= (int)d.size()) {
            Fail("read past the end of the sequence");
            return 0xFF;
        }
        return d[sc.pc++];
    }
    int16_t S16(Script& sc) {
        int hi = U8(sc);
        int lo = U8(sc);
        return (int16_t)((hi << 8) | lo);
    }
    uint16_t Var(Script& sc) {
        uint16_t r = U8(sc);
        if (r & 0x80) {
            r = ((r << 8) & 0x7F00) | U8(sc);
        }
        return r;
    }
    uint8_t At(int offset) {
        return (offset >= 0 && offset < (int)d.size()) ? d[offset] : 0;
    }
    void Fail(const std::string& message) {
        if (ok) {
            err = message;
        }
        ok = false;
    }

    // Control flow shared by the three script levels (>= 0xF2). Returns a delay, 0 to go on, -1 at the end.
    int Flow(Script& sc, int cmd, int* jumpedBackTo) {
        int arg = 0;
        switch (cmd) {
            case 0xF2:
            case 0xF3:
            case 0xF4:
            case 0xF8:
                arg = U8(sc);
                break;
            case 0xF5:
            case 0xF9:
            case 0xFA:
            case 0xFB:
            case 0xFC:
                arg = (uint16_t)S16(sc);
                break;
        }
        int from = sc.pc;
        switch (cmd) {
            case 0xFF:
                if (sc.depth == 0) {
                    return -1;
                }
                sc.pc = sc.stack[--sc.depth];
                break;
            case 0xFD:
                return Var(sc);
            case 0xFE:
                return 1;
            case 0xFC:
                if (sc.depth >= 4) {
                    Fail("call stack overflow");
                    return -1;
                }
                sc.stack[sc.depth++] = sc.pc;
                sc.pc = arg;
                break;
            case 0xF8:
                if (sc.depth >= 4) {
                    Fail("loop stack overflow");
                    return -1;
                }
                sc.loops[sc.depth] = arg;
                sc.stack[sc.depth++] = sc.pc;
                break;
            case 0xF7:
                if (sc.depth == 0) {
                    break;
                }
                sc.loops[sc.depth - 1] = (sc.loops[sc.depth - 1] - 1) & 0xFF;
                if (sc.loops[sc.depth - 1] != 0) {
                    sc.pc = sc.stack[sc.depth - 1];
                } else {
                    sc.depth--;
                }
                break;
            case 0xF6:
                if (sc.depth > 0) {
                    sc.depth--;
                }
                break;
            case 0xF5:
            case 0xF9:
            case 0xFA:
            case 0xFB:
                if ((cmd == 0xFA && sc.value != 0) || (cmd == 0xF9 && sc.value >= 0) || (cmd == 0xF5 && sc.value < 0)) {
                    break;
                }
                sc.pc = arg;
                if (cmd == 0xFB && arg < from && jumpedBackTo != nullptr) {
                    *jumpedBackTo = arg;
                }
                break;
            case 0xF2:
            case 0xF3:
            case 0xF4:
                if ((cmd == 0xF3 && sc.value != 0) || (cmd == 0xF2 && sc.value >= 0)) {
                    break;
                }
                sc.pc += (int8_t)arg;
                if (cmd == 0xF4 && sc.pc < from && jumpedBackTo != nullptr) {
                    *jumpedBackTo = sc.pc;
                }
                break;
        }
        return 0;
    }

    void Emit(int track, uint32_t at, int order, std::vector<uint8_t> bytes) {
        tracks[track].push_back({ at, order, std::move(bytes) });
    }

    int MidiChannel(int channel, bool drums) {
        if (drums) {
            return 9;
        }
        return channel < 9 ? channel : std::min(channel + 1, 15);
    }

    void SetProgram(int c, int instrument) {
        if (instrument < 0 || instrument >= 0x7E || lastProgram[c] == instrument) {
            return;
        }
        lastProgram[c] = instrument;
        std::string name = instrument < (int)mNames.size() && !mNames[instrument].empty()
                               ? mNames[instrument]
                               : "Instrument " + std::to_string(instrument);
        if (trackNames[c].empty()) {
            trackNames[c] = name;
        }
        Emit(c, tick, 1, { (uint8_t)(0xC0 | MidiChannel(c, false)), (uint8_t)GeneralMidiProgram(name) });
        std::vector<uint8_t> text = { 0xFF, 0x01 };
        AppendVarLen(text, name.size());
        text.insert(text.end(), name.begin(), name.end());
        Emit(c, tick, 1, text);
    }

    void StartChannel(int c, int pc) {
        Channel& chn = ch[c];
        chn.enabled = true;
        chn.stopScript = false;
        chn.s.depth = 0;
        chn.s.pc = pc;
        chn.delay = 0;
        for (Layer& l : chn.layers) {
            l.enabled = false;
        }
        startedChannel = true;
    }

    void SetLayer(Channel& chn, int index, int pc) {
        Layer& l = chn.layers[index];
        float velocity = l.velocitySquare;
        l = Layer();
        l.velocitySquare = velocity; // the game keeps the last value
        l.enabled = true;
        l.s.pc = pc;
    }

    void RunPlayer();
    void RunChannel(int c);
    void RunLayer(int c, Layer& l);
    void Note(int c, Layer& l, int semitone, int noteDelay, int gateTime);

  public:
    static void AppendVarLen(std::vector<uint8_t>& out, uint32_t value) {
        uint8_t bytes[5];
        int n = 0;
        do {
            bytes[n++] = value & 0x7F;
            value >>= 7;
        } while (value != 0);
        while (n > 0) {
            out.push_back(bytes[--n] | (n > 0 ? 0x80 : 0));
        }
    }

    static int GeneralMidiProgram(const std::string& name) {
        std::string n;
        for (char c : name) {
            n += (char)std::tolower((unsigned char)c);
        }
        static const std::pair<const char*, int> table[] = {
            { "pizz", 45 },       { "string", 48 },     { "violin", 40 },    { "viola", 41 },
            { "cello", 42 },      { "contrabass", 43 }, { "harp", 46 },      { "timpani", 47 },
            { "choir", 52 },      { "voice", 53 },      { "aah", 52 },       { "ooh", 53 },
            { "trumpet", 56 },    { "trombone", 57 },   { "tuba", 58 },      { "horn", 60 },
            { "brass", 61 },      { "sax", 65 },        { "oboe", 68 },      { "bassoon", 70 },
            { "clarinet", 71 },   { "piccolo", 72 },    { "flute", 73 },     { "recorder", 74 },
            { "panflute", 75 },   { "pan flute", 75 },  { "ocarina", 79 },   { "whistle", 78 },
            { "celest", 8 },      { "glocken", 9 },     { "music box", 10 }, { "musicbox", 10 },
            { "vibraphone", 11 }, { "marimba", 12 },    { "xylophone", 13 }, { "bell", 14 },
            { "dulcimer", 15 },   { "organ", 19 },      { "accordion", 21 }, { "harmonica", 22 },
            { "guitar", 24 },     { "banjo", 105 },     { "sitar", 104 },    { "shamisen", 106 },
            { "koto", 107 },      { "kalimba", 108 },   { "bagpipe", 109 },  { "fiddle", 110 },
            { "shakuhachi", 77 }, { "bass", 32 },       { "piano", 0 },      { "harpsichord", 6 },
            { "clav", 7 },        { "synth", 80 },      { "pad", 88 },       { "steel drum", 114 },
            { "taiko", 116 },     { "drum", 118 },      { "tom", 117 },
        };
        for (const auto& [key, program] : table) {
            if (n.find(key) != std::string::npos) {
                return program;
            }
        }
        return 0;
    }

    bool looped() const {
        return loopStart >= 0;
    }
    int64_t LoopStart() const {
        return loopStart;
    }
    int64_t LoopEnd() const {
        return loopEnd;
    }
    void Write(Result& result);
    double Seconds(uint32_t atTick) const;

  private:
    std::vector<std::pair<uint32_t, int>> tempoChanges; // tick, tatums per minute
};

void Player::RunPlayer() {
    if (ended) {
        return;
    }
    if (delay > 1) {
        delay--;
        return;
    }
    for (int guard = 0; guard < 10000 && ok; guard++) {
        int at = s.pc;
        if (!visited.count(at)) {
            visited[at] = tick;
            startsSince[at] = false;
        }
        int cmd = U8(s);
        if (cmd >= 0xF2) {
            int back = -1;
            int result = Flow(s, cmd, &back);
            if (back >= 0 && visited.count(back) && startsSince[back] && tick > visited[back] + kTatumsPerBeat) {
                // The sequence jumps back over music it already played: its main loop.
                loopStart = visited[back];
                loopEnd = tick;
                ended = true;
                return;
            }
            if (result != 0) {
                if (result < 0) {
                    ended = true;
                } else {
                    delay = result;
                }
                return;
            }
            continue;
        }
        if (cmd >= 0xC0) {
            switch (cmd) {
                case 0xF1:
                    U8(s);
                    break;
                case 0xF0:
                    break;
                case 0xDF:
                    transposition = 0;
                    // fallthrough
                case 0xDE:
                    transposition += (int8_t)U8(s);
                    break;
                case 0xDD:
                    tempo = std::max(1, (int)U8(s) * kTatumsPerBeat);
                    break;
                case 0xDC:
                    tempoChange = (int8_t)U8(s) * kTatumsPerBeat;
                    break;
                case 0xDA:
                    U8(s);
                    S16(s);
                    break;
                case 0xDB:
                case 0xD9:
                case 0xD5:
                case 0xD3:
                case 0xD0:
                    U8(s);
                    break;
                case 0xD7:
                case 0xD6:
                    S16(s);
                    break;
                case 0xD4:
                    break;
                case 0xD1:
                case 0xD2: {
                    int offset = (uint16_t)S16(s);
                    (cmd == 0xD2 ? shortVelocity : shortGate) = offset;
                    break;
                }
                case 0xCE: {
                    U8(s);
                    s.value = 0; // "random": take the first choice
                    break;
                }
                case 0xCD: {
                    int table = (uint16_t)S16(s);
                    if (s.value != -1 && s.depth != 3) {
                        int entry = table + (s.value << 1);
                        s.stack[s.depth++] = s.pc;
                        s.pc = (At(entry) << 8) | At(entry + 1);
                    }
                    break;
                }
                case 0xCC:
                    s.value = U8(s);
                    break;
                case 0xC9:
                    s.value &= U8(s);
                    break;
                case 0xC8:
                    s.value -= U8(s);
                    break;
                case 0xC7: {
                    int add = U8(s);
                    int offset = (uint16_t)S16(s);
                    if (offset < (int)d.size()) {
                        d[offset] = (uint8_t)(s.value + add);
                    }
                    break;
                }
                case 0xC2: {
                    int table = (uint16_t)S16(s);
                    if (s.value != -1) {
                        int entry = table + (s.value << 1);
                        s.pc = (At(entry) << 8) | At(entry + 1);
                    }
                    break;
                }
                case 0xC6:
                    ended = true;
                    return;
                case 0xC5:
                    S16(s);
                    break;
                case 0xEF:
                    S16(s);
                    U8(s);
                    break;
                case 0xC4:
                    // starts another sequence; the song ends here
                    U8(s);
                    U8(s);
                    ended = true;
                    return;
                case 0xC3: {
                    int table = (uint16_t)S16(s);
                    if (s.value != -1) {
                        int entry = table + s.value * 2;
                        int bits = (At(entry) << 8) | At(entry + 1);
                        (void)bits; // channel mutes; every channel stays audible in the export
                    }
                    break;
                }
                default:
                    Fail("unknown sequence command");
                    return;
            }
            continue;
        }
        int low = cmd & 0xF;
        switch (cmd & 0xF0) {
            case 0x00:
                s.value = ch[low].enabled ? 0 : 1;
                break;
            case 0x50:
                s.value -= io[low & 7];
                break;
            case 0x70:
                io[low & 7] = s.value;
                break;
            case 0x80:
                s.value = io[low & 7];
                if (low < 2) {
                    io[low] = kIoNone;
                }
                break;
            case 0x40:
                ch[low].enabled = false;
                break;
            case 0x90:
                StartChannel(low, (uint16_t)S16(s));
                for (auto& [pos, started] : startsSince) {
                    started = true;
                }
                break;
            case 0xA0: {
                int rel = S16(s);
                StartChannel(low, s.pc + rel);
                for (auto& [pos, started] : startsSince) {
                    started = true;
                }
                break;
            }
            case 0xB0:
                U8(s);
                S16(s);
                break;
            case 0x60:
                U8(s);
                U8(s);
                break;
            default:
                Fail("unknown sequence command");
                return;
        }
    }
}

void Player::RunChannel(int c) {
    Channel& chn = ch[c];
    if (!chn.enabled) {
        return;
    }
    if (!chn.stopScript) {
        if (chn.delay >= 2) {
            chn.delay--;
        } else {
            Script& sc = chn.s;
            for (int guard = 0; guard < 10000 && ok; guard++) {
                int at = sc.pc;
                if (!chn.visited.count(at)) {
                    chn.visited[at] = tick;
                }
                int cmd = U8(sc);
                if (cmd >= 0xF2) {
                    int back = -1;
                    int result = Flow(sc, cmd, &back);
                    if (back >= 0 && chn.loopFromTick < 0 && chn.visited.count(back) &&
                        tick > chn.visited[back] + 2 * kTatumsPerBeat) {
                        chn.loopFromTick = chn.visited[back];
                        chn.loopToTick = tick;
                    }
                    if (result != 0) {
                        if (result < 0) {
                            chn.enabled = false;
                            for (Layer& l : chn.layers) {
                                l.enabled = false;
                            }
                            return;
                        }
                        chn.delay = result;
                        break;
                    }
                    continue;
                }
                if (cmd >= 0xA0) {
                    // Read the arguments the way the game does, from the shared table.
                    static const uint8_t kArgs[] = {
                        0x81, 0x00, 0x81, 0x00, 0x01, 0x00, 0x42, 0x01, 0xC2, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x81, 0x00, 0x81, 0x01, 0x00, 0x00, 0x00, 0x81, 0x01, 0x01, 0x01, 0x42,
                        0x81, 0x81, 0x01, 0x00, 0x00, 0x01, 0x81, 0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01,
                        0x01, 0x81, 0x01, 0x01, 0x81, 0x81, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                        0x01, 0x01, 0x81, 0x01, 0x01, 0x01, 0x81, 0x01, 0x01, 0x03, 0x03, 0x01, 0x00, 0x01,
                        0x01, 0x81, 0x03, 0x01, 0x00, 0x02, 0x00, 0x01, 0x01, 0x82, 0x00, 0x01,
                    };
                    uint8_t high = kArgs[cmd - 0xA0];
                    int count = high & 3;
                    int args[3] = {};
                    for (int i = 0; i < count; i++, high <<= 1) {
                        args[i] = (high & 0x80) ? (int)S16(sc) : (int)U8(sc);
                    }
                    switch (cmd) {
                        case 0xEA:
                            chn.stopScript = true;
                            guard = 10000;
                            break;
                        case 0xC2:
                            chn.dynTable = (uint16_t)args[0];
                            break;
                        case 0xC5:
                            if (sc.value != -1 && chn.dynTable >= 0) {
                                int entry = chn.dynTable + sc.value * 2;
                                sc.pc = (At(entry) << 8) | At(entry + 1);
                            }
                            break;
                        case 0xEB:
                            args[0] = args[1];
                            // fallthrough
                        case 0xC1: {
                            int inst = args[0] & 0xFF;
                            if (inst >= 0x80) {
                                chn.instOrWave = inst;
                                chn.instrument = -1;
                            } else if (inst == 0x7F) {
                                chn.instOrWave = 0;
                                chn.instrument = -1;
                            } else if (inst == 0x7E) {
                                chn.instOrWave = 1;
                                chn.instrument = -1;
                            } else {
                                chn.instOrWave = 2; // instrument
                                chn.instrument = inst;
                                SetProgram(c, inst);
                            }
                            chn.hasInstrument = true;
                            break;
                        }
                        case 0xC3:
                            chn.largeNotes = false;
                            break;
                        case 0xC4:
                            chn.largeNotes = true;
                            break;
                        case 0xDF:
                            Emit(c, tick, 1,
                                 { (uint8_t)(0xB0 | MidiChannel(c, chn.instOrWave == 0)), 7,
                                   (uint8_t)std::min(args[0] & 0xFF, 127) });
                            break;
                        case 0xDD:
                            Emit(c, tick, 1,
                                 { (uint8_t)(0xB0 | MidiChannel(c, chn.instOrWave == 0)), 10,
                                   (uint8_t)std::min(args[0] & 0xFF, 127) });
                            break;
                        case 0xDB:
                            chn.transposition = (int8_t)args[0];
                            break;
                        case 0xC7: {
                            int offset = (uint16_t)args[1];
                            if (offset < (int)d.size()) {
                                d[offset] = (uint8_t)(sc.value + args[0]);
                            }
                            break;
                        }
                        case 0xC8:
                            sc.value -= (int8_t)args[0];
                            break;
                        case 0xCC:
                            sc.value = (int8_t)args[0];
                            break;
                        case 0xC9:
                            sc.value &= (int8_t)args[0];
                            break;
                        case 0xCD:
                            ch[args[0] & 0xF].enabled = false;
                            break;
                        case 0xCB:
                            sc.value = At((uint16_t)args[0] + sc.value);
                            break;
                        case 0xCE:
                            chn.unk22 = (uint16_t)args[0];
                            break;
                        case 0xCF: {
                            int offset = (uint16_t)args[0];
                            if (offset + 1 < (int)d.size()) {
                                d[offset] = (chn.unk22 >> 8) & 0xFF;
                                d[offset + 1] = chn.unk22 & 0xFF;
                            }
                            break;
                        }
                        case 0xE7: {
                            int offset = (uint16_t)args[0];
                            chn.transposition = (int8_t)At(offset + 3);
                            break;
                        }
                        case 0xE8:
                            chn.transposition = (int8_t)U8(sc);
                            U8(sc);
                            U8(sc);
                            U8(sc);
                            U8(sc);
                            break;
                        case 0xE4:
                            if (sc.value != -1 && chn.dynTable >= 0 && sc.depth < 4) {
                                int entry = chn.dynTable + sc.value * 2;
                                sc.stack[sc.depth++] = sc.pc;
                                sc.pc = (At(entry) << 8) | At(entry + 1);
                            }
                            break;
                        case 0xB2:
                            chn.unk22 =
                                (At((uint16_t)args[0] + sc.value * 2) << 8) | At((uint16_t)args[0] + sc.value * 2 + 1);
                            break;
                        case 0xB4:
                            chn.dynTable = chn.unk22;
                            break;
                        case 0xB5:
                            if (chn.dynTable >= 0) {
                                int entry = chn.dynTable + sc.value * 2;
                                chn.unk22 = (At(entry) << 8) | At(entry + 1);
                            }
                            break;
                        case 0xB6:
                            if (chn.dynTable >= 0) {
                                sc.value = At(chn.dynTable + sc.value);
                            }
                            break;
                        case 0xB7:
                        case 0xA8:
                            chn.unk22 = 0;
                            break;
                        case 0xB8:
                            sc.value = 0;
                            break;
                        case 0xBC:
                            chn.unk22 += args[0];
                            break;
                        case 0xA0:
                        case 0xA1:
                            sc.value = 0; // sound effect state
                            break;
                        case 0xA5:
                            sc.value += c;
                            break;
                        case 0xA6: {
                            int offset = (uint16_t)args[1] + c;
                            if (offset < (int)d.size()) {
                                d[offset] = (uint8_t)(sc.value + args[0]);
                            }
                            break;
                        }
                        case 0xA7: {
                            uint8_t v = (uint8_t)sc.value;
                            uint8_t r =
                                (args[0] & 0x80) ? (uint8_t)(v >> (args[0] & 0xF)) : (uint8_t)(v << (args[0] & 0xF));
                            if (args[0] & 0x40) {
                                r = (r & 0x7F) | (v & 0x80);
                            }
                            sc.value = r;
                            break;
                        }
                        default:
                            break; // sound only
                    }
                    continue;
                }
                if (cmd >= 0x70) {
                    int low = cmd & 7;
                    if ((cmd & 0xF8) != 0x70 && low >= 4) {
                        low = 0;
                    }
                    switch (cmd & 0xF8) {
                        case 0x80:
                            sc.value = chn.layers[low].enabled ? 0 : 1;
                            break;
                        case 0x88:
                            SetLayer(chn, low, (uint16_t)S16(sc));
                            break;
                        case 0x90:
                            chn.layers[low].enabled = false;
                            break;
                        case 0x98:
                            if (sc.value != -1 && chn.dynTable >= 0) {
                                int entry = chn.dynTable + sc.value * 2;
                                SetLayer(chn, low, (At(entry) << 8) | At(entry + 1));
                            }
                            break;
                        case 0x70:
                            chn.io[low] = sc.value;
                            break;
                        case 0x78: {
                            int rel = S16(sc);
                            SetLayer(chn, low, sc.pc + rel);
                            break;
                        }
                    }
                    continue;
                }
                int low = cmd & 0xF;
                bool stop = false;
                switch (cmd & 0xF0) {
                    case 0x00:
                        chn.delay = low;
                        stop = low != 0;
                        break;
                    case 0x10:
                        chn.io[low & 7] = 0; // sample loads finish at once
                        break;
                    case 0x60:
                        sc.value = chn.io[low & 7];
                        if (low < 2) {
                            chn.io[low] = kIoNone;
                        }
                        break;
                    case 0x50:
                        sc.value -= chn.io[low & 7];
                        break;
                    case 0x20:
                        StartChannel(low, (uint16_t)S16(sc));
                        break;
                    case 0x30: {
                        int port = U8(sc);
                        ch[low].io[port & 7] = sc.value;
                        break;
                    }
                    case 0x40: {
                        int port = U8(sc);
                        sc.value = ch[low].io[port & 7];
                        break;
                    }
                }
                if (stop) {
                    break;
                }
            }
        }
    }
    for (Layer& l : chn.layers) {
        RunLayer(c, l);
    }
}

void Player::RunLayer(int c, Layer& l) {
    if (!l.enabled) {
        return;
    }
    if (l.delay > 1) {
        l.delay--;
        return;
    }
    Channel& chn = ch[c];
    Script& sc = l.s;
    for (int guard = 0; guard < 10000 && ok; guard++) {
        int cmd = U8(sc);
        if (cmd <= 0xC0) {
            if (cmd == 0xC0) {
                l.delay = Var(sc);
                if (l.delay != 0) {
                    return;
                }
                continue;
            }
            int noteDelay, gate;
            if (chn.largeNotes) {
                int velocity = 0x7F;
                switch (cmd & 0xC0) {
                    case 0x00:
                        noteDelay = Var(sc);
                        velocity = U8(sc);
                        gate = U8(sc);
                        l.lastDelay = noteDelay;
                        break;
                    case 0x40:
                        noteDelay = Var(sc);
                        velocity = U8(sc);
                        gate = 0;
                        l.lastDelay = noteDelay;
                        break;
                    default:
                        noteDelay = l.lastDelay;
                        velocity = U8(sc);
                        gate = U8(sc);
                        break;
                }
                if (velocity > 0x7F) {
                    velocity = 0x7F;
                }
                l.velocitySquare = (velocity * velocity) / (127.0f * 127.0f);
                l.gateTime = gate;
            } else {
                switch (cmd & 0xC0) {
                    case 0x00:
                        noteDelay = Var(sc);
                        l.lastDelay = noteDelay;
                        break;
                    case 0x40:
                        noteDelay = l.shortDelay;
                        break;
                    default:
                        noteDelay = l.lastDelay;
                        break;
                }
                gate = l.gateTime;
            }
            Note(c, l, cmd & 0x3F, noteDelay, gate);
            l.delay = noteDelay;
            if (l.delay != 0) {
                return;
            }
            continue;
        }
        if (cmd >= 0xF2) {
            int result = Flow(sc, cmd, nullptr);
            if (result == 0) {
                continue;
            }
            // A delay here ends the layer in the game too
            l.enabled = false;
            return;
        }
        switch (cmd) {
            case 0xC1: {
                int v = U8(sc);
                l.velocitySquare = (v * v) / (127.0f * 127.0f);
                break;
            }
            case 0xCA:
                U8(sc);
                break;
            case 0xC9:
                l.gateTime = U8(sc);
                break;
            case 0xC2:
                l.transposition = (int8_t)U8(sc);
                break;
            case 0xC4:
            case 0xC5:
                break;
            case 0xC3:
                l.shortDelay = Var(sc);
                break;
            case 0xC6: {
                int inst = U8(sc);
                if (inst == 0x7E) {
                    l.instOrWave = 1;
                } else if (inst == 0x7F) {
                    l.instOrWave = 0;
                } else if (inst >= 0x80) {
                    l.instOrWave = inst;
                } else {
                    l.instOrWave = 2;
                    l.instrument = inst;
                }
                break;
            }
            case 0xC7: {
                int mode = U8(sc);
                U8(sc);
                if (mode & 0x80) {
                    U8(sc);
                } else {
                    Var(sc);
                }
                break;
            }
            case 0xC8:
            case 0xCC:
                break;
            case 0xCB:
                S16(sc);
                U8(sc);
                break;
            case 0xCF:
            case 0xCD:
            case 0xCE:
            case 0xF1:
                U8(sc);
                break;
            case 0xF0:
                S16(sc);
                break;
            default:
                if ((cmd & 0xF0) == 0xD0) {
                    int v = shortVelocity >= 0 ? At(shortVelocity + (cmd & 0xF)) : kDefaultShortVelocity[cmd & 0xF];
                    l.velocitySquare = (v * v) / (127.0f * 127.0f);
                } else if ((cmd & 0xF0) == 0xE0) {
                    l.gateTime = shortGate >= 0 ? At(shortGate + (cmd & 0xF)) : kDefaultShortGate[cmd & 0xF];
                }
                break;
        }
    }
}

void Player::Note(int c, Layer& l, int semitone, int noteDelay, int gateTime) {
    Channel& chn = ch[c];
    int instOrWave = l.instOrWave;
    int instrument = l.instrument;
    if (instOrWave == 0xFF) {
        if (!chn.hasInstrument) {
            return;
        }
        instOrWave = chn.instOrWave;
        instrument = chn.instrument;
    }
    int pitch;
    bool drums = instOrWave == 0;
    if (instOrWave == 1) {
        return; // sound effect
    } else if (drums) {
        pitch = semitone + chn.transposition + l.transposition;
    } else {
        pitch = semitone + transposition + chn.transposition + l.transposition;
        if (pitch >= 0x80) {
            return;
        }
        if (instOrWave == 2) {
            SetProgram(c, instrument);
        }
    }
    int note = std::clamp(pitch + 21, 0, 127);
    int velocity = std::clamp((int)std::lround(std::sqrt(l.velocitySquare) * 127.0f), 1, 127);
    int length = noteDelay > 0 ? noteDelay - ((gateTime * noteDelay) >> 8) : kTatumsPerBeat;
    length = std::max(1, length);
    int midiChannel = MidiChannel(c, drums);
    if (drums && trackNames[c].empty()) {
        trackNames[c] = "Drums";
    }
    Emit(c, tick, 2, { (uint8_t)(0x90 | midiChannel), (uint8_t)note, (uint8_t)velocity });
    Emit(c, tick + length, 0, { (uint8_t)(0x80 | midiChannel), (uint8_t)note, 0 });
}

double Player::Seconds(uint32_t atTick) const {
    double seconds = 0.0;
    uint32_t from = 0;
    int current = 120 * kTatumsPerBeat;
    for (const auto& [changeTick, value] : tempoChanges) {
        if (changeTick >= atTick) {
            break;
        }
        seconds += (changeTick - from) * 60.0 / current;
        from = changeTick;
        current = value;
    }
    return seconds + (atTick - from) * 60.0 / current;
}

bool Player::Run(Result& result, std::string* error) {
    std::fill(std::begin(lastProgram), std::end(lastProgram), -1);
    for (tick = 0; tick < kMaxTicks && ok && !ended; tick++) {
        RunPlayer();
        if (ended) {
            break;
        }
        int effective = std::max(1, tempo + tempoChange);
        if (effective != lastTempo) {
            lastTempo = effective;
            tempoChanges.push_back({ tick, effective });
        }
        bool any = false;
        for (int c = 0; c < kChannels; c++) {
            RunChannel(c);
            any |= ch[c].enabled;
        }
        (void)any;
    }
    if (!ok) {
        if (error != nullptr) {
            *error = err;
        }
        return false;
    }

    // Songs whose sequence script never jumps back loop channel by channel: take the longest such loop.
    if (loopStart < 0) {
        for (const Channel& chn : ch) {
            if (chn.loopFromTick >= 0 && (loopStart < 0 || chn.loopToTick - chn.loopFromTick > loopEnd - loopStart)) {
                loopStart = chn.loopFromTick;
                loopEnd = chn.loopToTick;
            }
        }
    }
    uint32_t end = loopStart >= 0 ? (uint32_t)loopEnd : tick;
    result.looped = loopStart >= 0;
    result.loopStart = loopStart >= 0 ? Seconds((uint32_t)loopStart) : 0.0;
    result.loopEnd = Seconds(end);

    // ---- MIDI file
    auto be32 = [](std::vector<uint8_t>& out, uint32_t v) {
        for (int i = 3; i >= 0; i--) {
            out.push_back((v >> (8 * i)) & 0xFF);
        }
    };
    auto writeTrack = [&](std::vector<uint8_t>& file, std::vector<Event> events, const std::string& name) {
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });
        std::vector<uint8_t> data;
        if (!name.empty()) {
            data.push_back(0);
            data.push_back(0xFF);
            data.push_back(0x03);
            AppendVarLen(data, name.size());
            data.insert(data.end(), name.begin(), name.end());
        }
        uint32_t last = 0;
        for (Event& e : events) {
            uint32_t at = std::min(e.tick, end);
            if (e.tick > end && (e.bytes[0] & 0xF0) != 0x80) {
                continue; // past the loop; note-offs are kept, moved to the end
            }
            AppendVarLen(data, at - last);
            last = at;
            data.insert(data.end(), e.bytes.begin(), e.bytes.end());
        }
        AppendVarLen(data, end > last ? end - last : 0);
        data.insert(data.end(), { 0xFF, 0x2F, 0x00 });
        file.insert(file.end(), { 'M', 'T', 'r', 'k' });
        be32(file, (uint32_t)data.size());
        file.insert(file.end(), data.begin(), data.end());
    };

    std::vector<uint8_t>& file = result.midi;
    file.clear();
    int used = 0;
    for (int c = 0; c < kChannels; c++) {
        used += !tracks[c].empty();
    }
    file.insert(file.end(), { 'M', 'T', 'h', 'd' });
    be32(file, 6);
    file.insert(file.end(), { 0, 1, (uint8_t)((used + 1) >> 8), (uint8_t)((used + 1) & 0xFF), 0, kTatumsPerBeat });

    std::vector<Event> tempoEvents;
    for (const auto& [changeTick, value] : tempoChanges) {
        uint32_t usPerQuarter = (uint32_t)std::lround(60000000.0 * kTatumsPerBeat / value);
        tempoEvents.push_back({ changeTick,
                                0,
                                { 0xFF, 0x51, 0x03, (uint8_t)(usPerQuarter >> 16), (uint8_t)(usPerQuarter >> 8),
                                  (uint8_t)usPerQuarter } });
    }
    if (result.looped) {
        const std::string start = "loopStart", stop = "loopEnd";
        std::vector<uint8_t> a = { 0xFF, 0x06, (uint8_t)start.size() };
        a.insert(a.end(), start.begin(), start.end());
        std::vector<uint8_t> b = { 0xFF, 0x06, (uint8_t)stop.size() };
        b.insert(b.end(), stop.begin(), stop.end());
        tempoEvents.push_back({ (uint32_t)loopStart, 1, a });
        tempoEvents.push_back({ end, 1, b });
    }
    writeTrack(file, tempoEvents, "Tempo");
    for (int c = 0; c < kChannels; c++) {
        if (!tracks[c].empty()) {
            std::string name = "Canal " + std::to_string(c) + (trackNames[c].empty() ? "" : " - " + trackNames[c]);
            writeTrack(file, tracks[c], name);
        }
    }
    return true;
}

} // namespace

bool Convert(const std::vector<uint8_t>& seq, const std::vector<std::string>& instrumentNames, Result& result,
             std::string* error) {
    Player player(seq, instrumentNames);
    return player.Run(result, error);
}

// ------------------------------------------------------------------ export of the game's songs

namespace {

std::mutex sStatusMutex;
ExportStatus sStatus;

// Instrument names of a sound font, from its XML in the archive ("audio/samples/Strings_META" -> "Strings").
std::vector<std::string> InstrumentNames(int fontId) {
    std::vector<std::string> names;
    if (gFontMap == nullptr || fontId < 0 || (size_t)fontId >= gFontMapSize || gFontMap[fontId] == nullptr) {
        return names;
    }
    auto file = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->LoadFile(gFontMap[fontId]);
    if (file == nullptr || file->Buffer == nullptr || file->Buffer->empty() || file->Buffer->at(0) != '<') {
        return names;
    }
    tinyxml2::XMLDocument doc;
    if (doc.Parse(file->Buffer->data(), file->Buffer->size()) != tinyxml2::XML_SUCCESS) {
        return names;
    }
    auto* root = doc.FirstChildElement("SoundFont");
    auto* instruments = root != nullptr ? root->FirstChildElement("Instruments") : nullptr;
    for (auto* inst = instruments != nullptr ? instruments->FirstChildElement("Instrument") : nullptr; inst != nullptr;
         inst = inst->NextSiblingElement("Instrument")) {
        std::string name;
        for (const char* child : { "NormalNotesSound", "LowNotesSound", "HighNotesSound" }) {
            auto* sound = inst->FirstChildElement(child);
            const char* ref = sound != nullptr ? sound->Attribute("SampleRef") : nullptr;
            if (ref != nullptr && *ref != '\0') {
                name = ref;
                break;
            }
        }
        size_t slash = name.rfind('/');
        if (slash != std::string::npos) {
            name = name.substr(slash + 1);
        }
        if (name.size() > 5 && name.substr(name.size() - 5) == "_META") {
            name = name.substr(0, name.size() - 5);
        }
        names.push_back(name);
    }
    return names;
}

std::string FileName(const std::string& label) {
    std::string out;
    for (char c : label) {
        out += (c == '_' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                c == '>' || c == '|')
                   ? ' '
                   : c;
    }
    size_t first = out.find_first_not_of(' ');
    size_t last = out.find_last_not_of(' ');
    return first == std::string::npos ? "Song" : out.substr(first, last - first + 1);
}

void ExportWorker(std::string folder) {
    struct Song {
        uint16_t seqId;
        std::string name;
        bool fanfare;
    };
    std::vector<Song> songs;
    for (const auto& [seqId, info] : AudioCollection::Instance->GetAllSequences()) {
        if (seqId > AudioCollection::Instance->GetMaxOriginalSeqId() || !info.canBeReplaced ||
            !(info.category & (SEQ_BGM_CUSTOM | SEQ_BGM_CUSTOM_FANFARE)) || seqId >= gSequenceMapSize ||
            gSequenceMap[seqId] == nullptr) {
            continue;
        }
        songs.push_back({ seqId, FileName(info.label), (info.category & SEQ_BGM_CUSTOM_FANFARE) != 0 });
    }
    {
        std::lock_guard<std::mutex> lock(sStatusMutex);
        sStatus.total = (int)songs.size();
    }

    std::error_code ec;
    fs::create_directories(folder, ec);
    std::ofstream loops(fs::path(folder) / "loops.txt");
    loops << "# " << BenGui::Tr("Loop points of the game's songs, in seconds. Keep this file next to your recordings.")
          << "\n";
    int failed = 0;
    for (const Song& song : songs) {
        auto resource = std::static_pointer_cast<SOH::AudioSequence>(
            Ship::Context::GetInstance()->GetResourceManager()->LoadResource(gSequenceMap[song.seqId]));
        bool done = false;
        if (resource != nullptr && resource->sequence.seqData != nullptr) {
            const SOH::Sequence& seq = resource->sequence;
            std::vector<uint8_t> data(seq.seqData, seq.seqData + seq.seqDataSize);
            int fontId = seq.numFonts > 0 ? seq.fonts[seq.numFonts - 1] : -1;
            Result result;
            if (Convert(data, InstrumentNames(fontId), result, nullptr)) {
                std::ofstream out(fs::path(folder) / (song.name + ".mid"), std::ios::binary);
                out.write((const char*)result.midi.data(), result.midi.size());
                done = (bool)out;
                if (result.looped && !song.fanfare) {
                    char line[64];
                    snprintf(line, sizeof(line), " = %.3f - %.3f\n", result.loopStart, result.loopEnd);
                    loops << song.name << line;
                }
            }
        }
        failed += !done;
        std::lock_guard<std::mutex> lock(sStatusMutex);
        sStatus.done++;
    }

    std::lock_guard<std::mutex> lock(sStatusMutex);
    sStatus.running = false;
    sStatus.message = std::to_string(sStatus.done - failed) + " " + BenGui::Tr("songs exported to") + " " + folder;
    if (failed > 0) {
        sStatus.message += " (" + std::to_string(failed) + " " + BenGui::Tr("could not be read") + ")";
    }
}

} // namespace

std::string GetExportFolder() {
    return Ship::Context::GetPathRelativeToAppDirectory("musicas-midi", appShortName);
}

void StartExport(const std::string& folder) {
    {
        std::lock_guard<std::mutex> lock(sStatusMutex);
        if (sStatus.running || AudioCollection::Instance == nullptr || gSequenceMap == nullptr) {
            return;
        }
        sStatus = ExportStatus();
        sStatus.running = true;
    }
    std::thread(ExportWorker, folder).detach();
}

ExportStatus GetExportStatus() {
    std::lock_guard<std::mutex> lock(sStatusMutex);
    return sStatus;
}

} // namespace SequenceMidi
