// Offline analysis: transient detection (spectral flux), grid slicing, tempo estimate.
// Runs on the message thread (or in tests), never on the audio thread.
#pragma once

#include "Kit.h"

namespace cs
{
struct OnsetFunction
{
    int fftSize = 1024, hop = 256;
    std::vector<float> flux;    // one value per hop, normalised to a peak of 1
};

OnsetFunction computeOnsetFunction (const std::vector<float>& mono, double sampleRate);

// Onset sample positions (sorted, the first is always 0). sensitivity 0..1, higher finds more hits.
std::vector<int> detectTransients (const std::vector<float>& mono, double sampleRate, const OnsetFunction&,
                                   float sensitivity, float minSliceMs);

// Evenly spaced markers: divisionsPerBar = 8 (1/8 notes) or 16 (1/16 notes).
std::vector<int> gridMarkers (const std::vector<float>& mono, int bars, int divisionsPerBar);

// Move pos back to the nearest zero crossing at most maxBack samples earlier.
int nudgeToZeroCrossing (const std::vector<float>& mono, int pos, int maxBack);

// Tempo guess from the autocorrelation of the onset function, refined so the loop is a
// whole number of bars. Returns bpm and writes bars.
double estimateTempo (const OnsetFunction&, double sampleRate, int lengthSamples, int& bars);

// Markers -> slices (sorted, deduplicated, capped at kMaxSlices, last slice ends at the end).
std::vector<Slice> slicesFromMarkers (std::vector<int> markers, int length);

enum SliceModeId { ModeTransient = 0, ModeGrid8, ModeGrid16, ModeManual };

// Re-slice a kit in place with the given mode. Manual mode uses `manualMarkers`.
void sliceKit (Kit&, int mode, float sensitivity, float minSliceMs, const std::vector<int>& manualMarkers);

// Build a kit from audio: estimates tempo + bars (unless bpm > 0 is given) and slices it.
Kit makeKit (std::shared_ptr<const Sample>, int mode, float sensitivity, float minSliceMs, double bpm = 0, int bars = 0);
} // namespace cs
