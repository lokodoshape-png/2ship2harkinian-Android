#include "MusicFolder.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#include <libultraship/libultraship.h>
#include <nlohmann/json.hpp>
#include <ship/resource/archive/Archive.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/utils/StringHelper.h>
#include <ship/utils/StrHash64.h>

#include <dr_flac.h>
#include <dr_mp3.h>
#include <dr_wav.h>
#include <ogg/ogg.h>
#include <opus/opus.h>
#include <opusfile.h>
#include <vorbis/vorbisfile.h>

#include "2s2h/BenGui/Translation.h"
#include "2s2h/BenPort.h"
#include "AudioCollection.h"

#ifdef __ANDROID__
#include <SDL2/SDL.h>
#include <jni.h>
#else
#include "2s2h/Extractor/portable-file-dialogs.h"
#endif

namespace fs = std::filesystem;
using BenGui::Tr;

namespace MusicFolder {

static constexpr uint32_t kRate = 48000; // Opus always decodes at 48 kHz
static constexpr int kBitrate = 96000; // per channel
static constexpr double kPi = 3.14159265358979323846;
static const char* kIndexName = "faixas.json";

std::string GetLibraryFolder() {
    return Ship::Context::GetPathRelativeToAppDirectory("musicas", appShortName);
}

// ------------------------------------------------------------------ names

static std::string Normalize(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

std::string FindOriginal(const std::string& songName, bool* once) {
    if (AudioCollection::Instance == nullptr) {
        return "";
    }
    const std::string wanted = Normalize(songName);
    for (const auto& [seqId, info] : AudioCollection::Instance->GetAllSequences()) {
        if (seqId > AudioCollection::Instance->GetMaxOriginalSeqId() || !info.canBeReplaced) {
            continue;
        }
        if (Normalize(info.label) == wanted || Normalize(info.sfxKey) == wanted) {
            if (once != nullptr) {
                *once = (info.category & SEQ_BGM_CUSTOM_FANFARE) != 0;
            }
            return info.label;
        }
    }
    return "";
}

// The audio collection takes a song's name from the text before the last '_' of its path, so names keep none.
static std::string SafeName(const std::string& name) {
    std::string out;
    for (char c : name) {
        out += (c == '_' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                c == '>' || c == '|')
                   ? ' '
                   : c;
    }
    size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "Song";
    }
    return out.substr(first, out.find_last_not_of(' ') - first + 1);
}

static std::string ChannelFile(const Track& track, uint32_t channel) {
    if (track.channels == 1) {
        return track.name + ".opus";
    }
    return track.name + (channel == 0 ? " L.opus" : " R.opus");
}

// ------------------------------------------------------------------ library index

static std::vector<Track> ReadIndex(const std::string& folder) {
    std::vector<Track> tracks;
    std::ifstream in(fs::path(folder) / kIndexName);
    if (!in) {
        return tracks;
    }
    try {
        nlohmann::json json = nlohmann::json::parse(in);
        for (const auto& t : json.at("tracks")) {
            Track track;
            track.name = t.at("name").get<std::string>();
            track.channels = t.at("channels").get<uint32_t>();
            track.frames = t.at("frames").get<uint32_t>();
            track.loopStart = t.value("loopStart", 0u);
            track.loopEnd = t.value("loopEnd", track.frames);
            track.once = t.value("once", false);
            track.replaces = t.value("replaces", std::string());
            if (track.frames > 0 && (track.channels == 1 || track.channels == 2)) {
                tracks.push_back(track);
            }
        }
    } catch (const std::exception& e) { SPDLOG_ERROR("Music folder: bad {}: {}", kIndexName, e.what()); }
    return tracks;
}

static void WriteIndex(const std::string& folder, const std::vector<Track>& tracks) {
    nlohmann::json list = nlohmann::json::array();
    for (const Track& t : tracks) {
        list.push_back({ { "name", t.name },
                         { "channels", t.channels },
                         { "frames", t.frames },
                         { "loopStart", t.loopStart },
                         { "loopEnd", t.loopEnd },
                         { "once", t.once },
                         { "replaces", t.replaces } });
    }
    std::ofstream out(fs::path(folder) / kIndexName);
    out << nlohmann::json({ { "version", 1 }, { "tracks", list } }).dump(1);
}

std::vector<Track> GetTracks() {
    return ReadIndex(GetLibraryFolder());
}

// ------------------------------------------------------------------ archive

// Serves the library as the files a streamed-audio mod carries (see ResourceFactoryXMLAudioSequenceV0 and
// louist103/future): custom/music, custom/fonts and custom/samples hold XML, custom/sampleData the Opus data.
class MusicLibraryArchive final : public Ship::Archive {
  public:
    MusicLibraryArchive(const std::string& folder) : Ship::Archive(folder), mFolder(folder) {
    }

    bool Open() override {
        for (const Track& track : ReadIndex(mFolder)) {
            const std::string fontPath = "custom/fonts/" + track.name + "_FONT.xml";
            std::string fontXml = "<SoundFont Version=\"0\" Num=\"0\" Medium=\"Cart\" CachePolicy=\"Either\" "
                                  "Data1=\"511\" Data2=\"3840\" Data3=\"0\">\n<Drums/>\n<Instruments>\n";
            for (uint32_t ch = 0; ch < track.channels; ch++) {
                const std::string file = ChannelFile(track, ch);
                const std::string dataPath = "custom/sampleData/" + file;
                const std::string samplePath = "custom/samples/" + file + "_SAMPLE.xml";
                mData[dataPath] = file;
                mXml[samplePath] =
                    "<Sample Version=\"0\" Codec=\"S16\" Medium=\"Cart\" bit26=\"0\" CustomFormat=\"ogg\" "
                    "Size=\"" +
                    std::to_string(track.frames * 2) + "\" Path=\"" + Escape(dataPath) + "\">\n<ADPCMLoop Start=\"" +
                    std::to_string(track.loopStart) + "\" End=\"" + std::to_string(track.loopEnd) + "\" Count=\"" +
                    (track.once ? "0" : "-1") + "\"/>\n</Sample>\n";
                fontXml += "<Instrument IsValid=\"true\" Loaded=\"false\" NormalRangeLo=\"0\" NormalRangeHi=\"127\" "
                           "ReleaseRate=\"239\">\n<Envelopes>\n<Envelope Delay=\"1\" Arg=\"32000\"/>\n"
                           "<Envelope Delay=\"1000\" Arg=\"32000\"/>\n<Envelope Delay=\"-1\" Arg=\"0\"/>\n"
                           "<Envelope Delay=\"0\" Arg=\"0\"/>\n</Envelopes>\n<NormalNotesSound SampleRef=\"" +
                           Escape(samplePath) + "\" Tuning=\"1.5\"/>\n</Instrument>\n";
                IndexFile(dataPath);
                IndexFile(samplePath);
            }
            fontXml += "</Instruments>\n</SoundFont>\n";
            mXml[fontPath] = fontXml;
            IndexFile(fontPath);

            // A streamed sequence names its font by the CRC64 of the font's path, stored byte by byte.
            uint64_t crc = CRC64(fontPath.c_str());
            std::string fonts;
            for (int i = 0; i < 8; i++) {
                fonts += "<FontIndex FontIdx=\"" + std::to_string(reinterpret_cast<uint8_t*>(&crc)[i]) + "\"/>\n";
            }
            const std::string seqPath = "custom/music/" + track.name + "_SEQ.xml" + (track.once ? "_fanfare" : "");
            mXml[seqPath] = "<Sequence Index=\"0\" Medium=\"Cart\" CachePolicy=\"Temporary\" Size=\"0\" "
                            "Streamed=\"true\" Length=\"" +
                            std::to_string((track.frames + kRate - 1) / kRate) + "\" Looped=\"" +
                            (track.once ? "false" : "true") + "\" Stereo=\"" +
                            (track.channels == 2 ? "true" : "false") + "\">\n<FontIndicies>\n" + fonts +
                            "</FontIndicies>\n</Sequence>\n";
            IndexFile(seqPath);
        }
        return !mXml.empty();
    }

    bool Close() override {
        return true;
    }

    bool WriteFile(const std::string&, const std::vector<uint8_t>&) override {
        return false;
    }

    std::shared_ptr<Ship::File> LoadFile(const std::string& filePath) override {
        auto file = std::make_shared<Ship::File>();
        if (auto xml = mXml.find(filePath); xml != mXml.end()) {
            file->Buffer = std::make_shared<std::vector<char>>(xml->second.begin(), xml->second.end());
        } else if (auto data = mData.find(filePath); data != mData.end()) {
            std::ifstream in(fs::path(mFolder) / data->second, std::ios::binary);
            if (!in) {
                return nullptr;
            }
            file->Buffer = std::make_shared<std::vector<char>>(std::istreambuf_iterator<char>(in),
                                                               std::istreambuf_iterator<char>());
        } else {
            return nullptr;
        }
        file->IsLoaded = true;
        return file;
    }

    std::shared_ptr<Ship::File> LoadFile(uint64_t hash) override {
        const std::string* path =
            Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->HashToString(hash);
        return path != nullptr ? LoadFile(*path) : nullptr;
    }

  private:
    static std::string Escape(const std::string& text) {
        std::string out;
        for (char c : text) {
            switch (c) {
                case '&':
                    out += "&amp;";
                    break;
                case '"':
                    out += "&quot;";
                    break;
                case '<':
                    out += "&lt;";
                    break;
                case '>':
                    out += "&gt;";
                    break;
                default:
                    out += c;
            }
        }
        return out;
    }

    std::string mFolder;
    std::map<std::string, std::string> mXml;
    std::map<std::string, std::string> mData; // archive path -> file name in the library folder
};

void Mount() {
    const std::string folder = GetLibraryFolder();
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (!fs::exists(fs::path(folder) / kIndexName, ec)) {
        return;
    }
    auto archive = std::make_shared<MusicLibraryArchive>(folder);
    archive->Load();
    if (archive->IsLoaded()) {
        Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->AddArchive(archive);
    }
}

// ------------------------------------------------------------------ decoding

struct Pcm {
    std::vector<int16_t> samples; // interleaved
    uint32_t channels = 0;
    uint32_t rate = 0;
    int64_t loopStart = -1; // from LOOPSTART/LOOPEND tags, in source frames
    int64_t loopEnd = -1;
};

struct MemoryReader {
    const std::vector<uint8_t>* data;
    size_t pos;
};

static size_t VorbisRead(void* ptr, size_t size, size_t count, void* source) {
    auto* r = static_cast<MemoryReader*>(source);
    size_t bytes = std::min(size * count, r->data->size() - r->pos);
    memcpy(ptr, r->data->data() + r->pos, bytes);
    r->pos += bytes;
    return size ? bytes / size : 0;
}

static int VorbisSeek(void* source, ogg_int64_t offset, int whence) {
    auto* r = static_cast<MemoryReader*>(source);
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (int64_t)r->pos : (int64_t)r->data->size();
    int64_t target = base + offset;
    if (target < 0 || target > (int64_t)r->data->size()) {
        return -1;
    }
    r->pos = (size_t)target;
    return 0;
}

static long VorbisTell(void* source) {
    return (long)static_cast<MemoryReader*>(source)->pos;
}

static void ReadLoopTag(const char* key, const char* value, Pcm& pcm, int64_t* length) {
    if (StringHelper::IEquals(key, "LOOPSTART")) {
        pcm.loopStart = atoll(value);
    } else if (StringHelper::IEquals(key, "LOOPEND")) {
        pcm.loopEnd = atoll(value);
    } else if (StringHelper::IEquals(key, "LOOPLENGTH")) {
        *length = atoll(value);
    }
}

static void ReadLoopComments(char** comments, int count, Pcm& pcm) {
    int64_t length = -1;
    for (int i = 0; i < count; i++) {
        std::string comment = comments[i];
        size_t eq = comment.find('=');
        if (eq != std::string::npos) {
            ReadLoopTag(comment.substr(0, eq).c_str(), comment.c_str() + eq + 1, pcm, &length);
        }
    }
    if (pcm.loopEnd < 0 && pcm.loopStart >= 0 && length > 0) {
        pcm.loopEnd = pcm.loopStart + length;
    }
}

static bool Decode(const std::vector<uint8_t>& data, Pcm& pcm) {
    if (data.size() < 4) {
        return false;
    }
    const uint8_t* d = data.data();
    if (memcmp(d, "RIFF", 4) == 0) {
        unsigned int channels, rate;
        drwav_uint64 frames;
        int16_t* s = drwav_open_memory_and_read_pcm_frames_s16(d, data.size(), &channels, &rate, &frames, nullptr);
        if (s == nullptr) {
            return false;
        }
        pcm.samples.assign(s, s + frames * channels);
        drwav_free(s, nullptr);
        pcm.channels = channels;
        pcm.rate = rate;
        return true;
    }
    if (memcmp(d, "fLaC", 4) == 0) {
        unsigned int channels, rate;
        drflac_uint64 frames;
        int16_t* s = drflac_open_memory_and_read_pcm_frames_s16(d, data.size(), &channels, &rate, &frames, nullptr);
        if (s == nullptr) {
            return false;
        }
        pcm.samples.assign(s, s + frames * channels);
        drflac_free(s, nullptr);
        pcm.channels = channels;
        pcm.rate = rate;
        return true;
    }
    if (memcmp(d, "OggS", 4) == 0) {
        int error = 0;
        OggOpusFile* opus = op_open_memory(d, data.size(), &error);
        if (opus != nullptr) {
            pcm.channels = (uint32_t)op_channel_count(opus, -1);
            pcm.rate = kRate;
            const OpusTags* tags = op_tags(opus, -1);
            if (tags != nullptr) {
                ReadLoopComments(tags->user_comments, tags->comments, pcm);
            }
            std::vector<int16_t> buffer(5760 * pcm.channels);
            int read;
            while ((read = op_read(opus, buffer.data(), (int)buffer.size(), nullptr)) > 0) {
                pcm.samples.insert(pcm.samples.end(), buffer.begin(), buffer.begin() + read * pcm.channels);
            }
            op_free(opus);
            return !pcm.samples.empty();
        }
        MemoryReader reader = { &data, 0 };
        OggVorbis_File vf;
        ov_callbacks callbacks = { VorbisRead, VorbisSeek, nullptr, VorbisTell };
        if (ov_open_callbacks(&reader, &vf, nullptr, 0, callbacks) != 0) {
            return false;
        }
        vorbis_info* info = ov_info(&vf, -1);
        pcm.channels = (uint32_t)info->channels;
        pcm.rate = (uint32_t)info->rate;
        vorbis_comment* comment = ov_comment(&vf, -1);
        if (comment != nullptr) {
            ReadLoopComments(comment->user_comments, comment->comments, pcm);
        }
        char buffer[16384];
        int bitstream = 0;
        long read;
        while ((read = ov_read(&vf, buffer, sizeof(buffer), 0, 2, 1, &bitstream)) > 0) {
            const int16_t* s = reinterpret_cast<const int16_t*>(buffer);
            pcm.samples.insert(pcm.samples.end(), s, s + read / 2);
        }
        ov_clear(&vf);
        return !pcm.samples.empty();
    }
    // Anything else is tried as MP3 (files often start with an ID3 tag or straight with a frame header).
    drmp3_config config;
    drmp3_uint64 frames;
    int16_t* s = drmp3_open_memory_and_read_pcm_frames_s16(d, data.size(), &config, &frames, nullptr);
    if (s == nullptr || frames == 0) {
        return false;
    }
    pcm.samples.assign(s, s + frames * config.channels);
    drmp3_free(s, nullptr);
    pcm.channels = config.channels;
    pcm.rate = config.sampleRate;
    return true;
}

// ------------------------------------------------------------------ resampling

// Windowed-sinc resampler (Blackman window, 32 taps, 512 phases) from the source rate to 48 kHz.
static std::vector<float> Resample(const std::vector<float>& in, uint32_t rate) {
    if (rate == kRate) {
        return in;
    }
    constexpr int kTaps = 32;
    constexpr int kPhases = 512;
    const double ratio = (double)kRate / rate;
    const double cutoff = std::min(1.0, ratio) * 0.97;
    static thread_local std::vector<float> table;
    static thread_local double tableCutoff = -1.0;
    if (tableCutoff != cutoff) {
        table.assign((kPhases + 1) * kTaps, 0.0f);
        for (int p = 0; p <= kPhases; p++) {
            double frac = (double)p / kPhases;
            double sum = 0.0;
            for (int k = 0; k < kTaps; k++) {
                double x = (k - kTaps / 2 + 1) - frac;
                double sinc = x == 0.0 ? 1.0 : std::sin(kPi * x * cutoff) / (kPi * x * cutoff);
                double w = 0.42 + 0.5 * std::cos(kPi * x / (kTaps / 2)) + 0.08 * std::cos(2 * kPi * x / (kTaps / 2));
                if (std::abs(x) >= kTaps / 2) {
                    w = 0.0;
                }
                table[p * kTaps + k] = (float)(sinc * w);
                sum += sinc * w;
            }
            for (int k = 0; k < kTaps; k++) {
                table[p * kTaps + k] /= (float)sum;
            }
        }
        tableCutoff = cutoff;
    }

    const size_t outLength = (size_t)std::floor(in.size() * ratio);
    std::vector<float> out(outLength);
    const int64_t n = (int64_t)in.size();
    for (size_t i = 0; i < outLength; i++) {
        double t = i / ratio;
        int64_t base = (int64_t)std::floor(t);
        int phase = (int)std::lround((t - base) * kPhases);
        const float* h = &table[phase * kTaps];
        float acc = 0.0f;
        for (int k = 0; k < kTaps; k++) {
            int64_t idx = base + k - kTaps / 2 + 1;
            if (idx >= 0 && idx < n) {
                acc += in[(size_t)idx] * h[k];
            }
        }
        out[i] = acc;
    }
    return out;
}

// ------------------------------------------------------------------ Opus encoding

static void WritePages(ogg_stream_state& os, std::ofstream& out, bool flush) {
    ogg_page page;
    while (flush ? ogg_stream_flush(&os, &page) : ogg_stream_pageout(&os, &page)) {
        out.write(reinterpret_cast<char*>(page.header), page.header_len);
        out.write(reinterpret_cast<char*>(page.body), page.body_len);
    }
}

// Encodes 48 kHz mono samples as an Ogg Opus file whose decoded length is exactly samples.size().
static bool EncodeOpus(const std::vector<float>& samples, const fs::path& path) {
    int error = 0;
    OpusEncoder* enc = opus_encoder_create(kRate, 1, OPUS_APPLICATION_AUDIO, &error);
    if (enc == nullptr) {
        return false;
    }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(kBitrate));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(8));
    opus_int32 lookahead = 0;
    opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&lookahead));

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        opus_encoder_destroy(enc);
        return false;
    }
    ogg_stream_state os;
    ogg_stream_init(&os, (int)std::hash<std::string>()(path.string()));

    uint8_t head[19] = { 'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 1 };
    head[10] = lookahead & 0xFF;
    head[11] = (lookahead >> 8) & 0xFF;
    head[12] = kRate & 0xFF;
    head[13] = (kRate >> 8) & 0xFF;
    head[14] = (kRate >> 16) & 0xFF;
    head[15] = (kRate >> 24) & 0xFF;
    ogg_packet packet = {};
    packet.packet = head;
    packet.bytes = sizeof(head);
    packet.b_o_s = 1;
    ogg_stream_packetin(&os, &packet);
    WritePages(os, out, true);

    static const char vendor[] = "2Ship MM3D";
    std::vector<uint8_t> tags = { 'O', 'p', 'u', 's', 'T', 'a', 'g', 's' };
    uint32_t vendorLength = sizeof(vendor) - 1;
    for (int i = 0; i < 4; i++) {
        tags.push_back((vendorLength >> (8 * i)) & 0xFF);
    }
    tags.insert(tags.end(), vendor, vendor + vendorLength);
    tags.insert(tags.end(), { 0, 0, 0, 0 });
    packet = {};
    packet.packet = tags.data();
    packet.bytes = (long)tags.size();
    packet.packetno = 1;
    ogg_stream_packetin(&os, &packet);
    WritePages(os, out, true);

    // The encoder delays its output by `lookahead` samples, which the decoder skips (pre-skip), so feed that much
    // extra silence at the end to get every sample out.
    constexpr int kFrame = 960;
    const size_t total = samples.size() + lookahead;
    std::vector<float> frame(kFrame);
    uint8_t data[4000];
    int64_t packetNo = 2;
    for (size_t pos = 0; pos < total; pos += kFrame) {
        for (int i = 0; i < kFrame; i++) {
            frame[i] = pos + i < samples.size() ? samples[pos + i] : 0.0f;
        }
        opus_int32 bytes = opus_encode_float(enc, frame.data(), kFrame, data, sizeof(data));
        if (bytes < 0) {
            ogg_stream_clear(&os);
            opus_encoder_destroy(enc);
            return false;
        }
        bool last = pos + kFrame >= total;
        packet = {};
        packet.packet = data;
        packet.bytes = bytes;
        packet.packetno = packetNo++;
        packet.e_o_s = last ? 1 : 0;
        packet.granulepos = last ? (ogg_int64_t)total : (ogg_int64_t)(pos + kFrame);
        ogg_stream_packetin(&os, &packet);
        WritePages(os, out, last);
    }
    ogg_stream_clear(&os);
    opus_encoder_destroy(enc);
    return out.good();
}

// ------------------------------------------------------------------ import

static std::mutex sStatusMutex;
static ImportStatus sStatus;
static std::thread sImportThread;

ImportStatus GetImportStatus() {
    std::lock_guard<std::mutex> lock(sStatusMutex);
    return sStatus;
}

static bool IsAudioFile(const fs::path& path) {
    static const char* extensions[] = { ".wav", ".flac", ".mp3", ".ogg", ".opus", ".oga" };
    const std::string ext = path.extension().string();
    return std::any_of(std::begin(extensions), std::end(extensions),
                       [&](const char* e) { return StringHelper::IEquals(ext, e); });
}

// loops.txt: "Song name = start" or "Song name = start - end", in seconds.
static std::map<std::string, std::pair<double, double>> ReadLoopsFile(const fs::path& path) {
    std::map<std::string, std::pair<double, double>> loops;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));
        size_t eq = line.rfind('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string value = line.substr(eq + 1);
        size_t dash = value.find('-');
        double start = atof(value.substr(0, dash).c_str());
        double end = dash != std::string::npos ? atof(value.substr(dash + 1).c_str()) : -1.0;
        loops[Normalize(line.substr(0, eq))] = { start, end };
    }
    return loops;
}

static bool ImportTrack(const fs::path& source, const fs::path& outFolder,
                        const std::map<std::string, std::pair<double, double>>& loops, Track& track,
                        std::string& error) {
    std::ifstream in(source, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Pcm pcm;
    if (!Decode(data, pcm) || pcm.channels == 0 || pcm.rate == 0) {
        error = Tr("could not read this file");
        return false;
    }
    data.clear();
    data.shrink_to_fit();

    std::string name = source.stem().string();
    bool once = false;
    if (name.size() > strlen("_fanfare") &&
        StringHelper::IEquals(name.substr(name.size() - strlen("_fanfare")), "_fanfare")) {
        name = name.substr(0, name.size() - strlen("_fanfare"));
        once = true;
    }
    track.name = SafeName(name);
    bool originalOnce = false;
    track.replaces = FindOriginal(name, &originalOnce);
    track.once = once || originalOnce;
    track.channels = std::min<uint32_t>(pcm.channels, 2);

    const size_t sourceFrames = pcm.samples.size() / pcm.channels;
    std::vector<std::vector<float>> channels(track.channels);
    for (uint32_t ch = 0; ch < track.channels; ch++) {
        std::vector<float> mono(sourceFrames);
        for (size_t i = 0; i < sourceFrames; i++) {
            if (pcm.channels > 2 && ch == 0) {
                // More than two channels: fold everything into left/right by parity.
                float sum = 0.0f;
                for (uint32_t c = 0; c < pcm.channels; c += 2) {
                    sum += pcm.samples[i * pcm.channels + c];
                }
                mono[i] = sum / 32768.0f / ((pcm.channels + 1) / 2);
            } else if (pcm.channels > 2) {
                float sum = 0.0f;
                for (uint32_t c = 1; c < pcm.channels; c += 2) {
                    sum += pcm.samples[i * pcm.channels + c];
                }
                mono[i] = sum / 32768.0f / (pcm.channels / 2);
            } else {
                mono[i] = pcm.samples[i * pcm.channels + ch] / 32768.0f;
            }
        }
        channels[ch] = Resample(mono, pcm.rate);
    }
    pcm.samples.clear();
    pcm.samples.shrink_to_fit();

    track.frames = (uint32_t)channels[0].size();
    if (track.frames == 0) {
        error = Tr("the file is empty");
        return false;
    }
    const double scale = (double)kRate / pcm.rate;
    int64_t loopStart = 0, loopEnd = track.frames;
    if (auto loop = loops.find(Normalize(name)); loop != loops.end()) {
        loopStart = (int64_t)(loop->second.first * kRate);
        if (loop->second.second > 0) {
            loopEnd = (int64_t)(loop->second.second * kRate);
        }
    } else if (pcm.loopStart >= 0) {
        loopStart = (int64_t)(pcm.loopStart * scale);
        if (pcm.loopEnd > 0) {
            loopEnd = (int64_t)(pcm.loopEnd * scale);
        }
    }
    loopEnd = std::clamp<int64_t>(loopEnd, 1, track.frames);
    track.loopEnd = (uint32_t)loopEnd;
    track.loopStart = (loopStart >= 0 && loopStart < loopEnd) ? (uint32_t)loopStart : 0;

    for (uint32_t ch = 0; ch < track.channels; ch++) {
        if (!EncodeOpus(channels[ch], outFolder / ChannelFile(track, ch))) {
            error = Tr("could not convert this file");
            return false;
        }
    }
    return true;
}

static void RunImport(std::string sourceFolder, bool deleteSource) {
    const fs::path library = GetLibraryFolder();
    const fs::path staging = library / ".novo";
    std::error_code ec;
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);

    std::vector<fs::path> files;
    fs::path loopsPath;
    for (const auto& entry : fs::directory_iterator(sourceFolder, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        if (StringHelper::IEquals(entry.path().filename().string(), "loops.txt")) {
            loopsPath = entry.path();
        } else if (IsAudioFile(entry.path())) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    const auto loops = ReadLoopsFile(loopsPath);

    {
        std::lock_guard<std::mutex> lock(sStatusMutex);
        sStatus.total = (int)files.size();
    }

    std::vector<Track> tracks(files.size());
    std::vector<std::string> errors(files.size());
    std::vector<char> ok(files.size(), 0); // not vector<bool>: workers write neighbouring entries
    std::atomic<size_t> next = 0;
#ifdef __ANDROID__
    const unsigned workers = 2; // keeps memory use down on phones
#else
    const unsigned workers = std::max(1u, std::min(4u, std::thread::hardware_concurrency()));
#endif
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < workers; w++) {
        pool.emplace_back([&]() {
            for (size_t i = next++; i < files.size(); i = next++) {
                {
                    std::lock_guard<std::mutex> lock(sStatusMutex);
                    sStatus.current = files[i].filename().string();
                }
                ok[i] = ImportTrack(files[i], staging, loops, tracks[i], errors[i]);
                std::lock_guard<std::mutex> lock(sStatusMutex);
                sStatus.done++;
            }
        });
    }
    for (auto& t : pool) {
        t.join();
    }

    // Keep the first song for each name; a name used twice would point two sequences at the same files.
    std::vector<Track> imported;
    std::string failed, unmatched;
    for (size_t i = 0; i < files.size(); i++) {
        if (!ok[i]) {
            failed += "\n" + files[i].filename().string() + " (" + errors[i] + ")";
            continue;
        }
        bool duplicate =
            std::any_of(imported.begin(), imported.end(), [&](const Track& t) { return t.name == tracks[i].name; });
        if (duplicate) {
            failed += "\n" + files[i].filename().string() + " (" + Tr("another file has the same name") + ")";
            continue;
        }
        if (tracks[i].replaces.empty()) {
            unmatched += "\n" + tracks[i].name;
        }
        imported.push_back(tracks[i]);
    }

    // Swap the new library in.
    for (const auto& entry : fs::directory_iterator(library, ec)) {
        if (entry.is_regular_file(ec) && (StringHelper::IEquals(entry.path().extension().string(), ".opus") ||
                                          entry.path().filename() == kIndexName)) {
            fs::remove(entry.path(), ec);
        }
    }
    for (const auto& entry : fs::directory_iterator(staging, ec)) {
        fs::rename(entry.path(), library / entry.path().filename(), ec);
    }
    fs::remove_all(staging, ec);
    if (!imported.empty()) {
        WriteIndex(library.string(), imported);
    }
    if (deleteSource) {
        fs::remove_all(sourceFolder, ec);
    }

    std::string message;
    if (files.empty()) {
        message = Tr("No songs found in that folder (wav, flac, mp3, ogg or opus).");
    } else {
        message = std::to_string(imported.size()) + " " + Tr("songs ready. Close and reopen the game to hear them.");
        if (!unmatched.empty()) {
            message += "\n\n" +
                       std::string(Tr("These names match no game song; pick where they play in the Audio "
                                      "Editor:")) +
                       unmatched;
        }
        if (!failed.empty()) {
            message += "\n\n" + std::string(Tr("Could not convert:")) + failed;
        }
    }
    std::lock_guard<std::mutex> lock(sStatusMutex);
    sStatus.running = false;
    sStatus.current.clear();
    sStatus.message = message;
}

void StartImport(const std::string& sourceFolder, bool deleteSource) {
    {
        std::lock_guard<std::mutex> lock(sStatusMutex);
        if (sStatus.running) {
            return;
        }
        sStatus = ImportStatus();
        sStatus.running = true;
    }
    if (sImportThread.joinable()) {
        sImportThread.join();
    }
    sImportThread = std::thread(RunImport, sourceFolder, deleteSource);
}

void Clear() {
    const fs::path library = GetLibraryFolder();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(library, ec)) {
        if (entry.is_regular_file(ec) && (StringHelper::IEquals(entry.path().extension().string(), ".opus") ||
                                          entry.path().filename() == kIndexName)) {
            fs::remove(entry.path(), ec);
        }
    }
    std::lock_guard<std::mutex> lock(sStatusMutex);
    sStatus.message = Tr("Music removed. Close and reopen the game to apply.");
}

#ifdef __ANDROID__
void PickFolder() {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(activityClass, "openMusicFolderPicker", "()V");
    if (method != nullptr) {
        env->CallVoidMethod(activity, method);
    }
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
}

// The app copied the songs of the picked folder to `path`; convert them from there.
extern "C" JNIEXPORT void JNICALL Java_com_dishii_mm_MainActivity_nativeMusicFolderCopied(JNIEnv* env, jobject,
                                                                                          jstring path) {
    const char* chars = env->GetStringUTFChars(path, nullptr);
    std::string folder = chars;
    env->ReleaseStringUTFChars(path, chars);
    StartImport(folder, true);
}
#else
void PickFolder() {
    std::string folder = pfd::select_folder(Tr("Choose the folder with the songs")).result();
    if (!folder.empty()) {
        StartImport(folder, false);
    }
}
#endif

} // namespace MusicFolder
