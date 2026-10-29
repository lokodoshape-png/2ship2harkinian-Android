#ifndef SEQUENCE_MIDI_H
#define SEQUENCE_MIDI_H

#include <cstdint>
#include <string>
#include <vector>

// Converts the game's music sequences to standard MIDI files, so they can be arranged in a DAW and brought back
// through the music folder (Enhancements > Audio Editor > Music).
namespace SequenceMidi {

struct Result {
    std::vector<uint8_t> midi;
    bool looped = false;
    double loopStart = 0.0; // seconds
    double loopEnd = 0.0;   // seconds; the length of the song when it does not loop
};

// Plays `seq` (the sequence script, as stored in the game's archive) through once, up to the end of its first loop,
// and writes what it plays as a type 1 MIDI file with one track per sequence channel. `instrumentNames` names the
// instruments of the sequence's default sound font, by index; they become track names and pick a General MIDI
// program. Returns false if the script could not be read.
bool Convert(const std::vector<uint8_t>& seq, const std::vector<std::string>& instrumentNames, Result& result,
             std::string* error);

// Exports every music track of the game into `folder` as "<name>.mid", plus a loops.txt with the loop points in the
// format the music folder reads. Runs in the background; progress and the result come back through the callback
// strings below.
struct ExportStatus {
    bool running = false;
    int done = 0;
    int total = 0;
    std::string message;
};
void StartExport(const std::string& folder);
ExportStatus GetExportStatus();
std::string GetExportFolder();

} // namespace SequenceMidi

#endif // SEQUENCE_MIDI_H
