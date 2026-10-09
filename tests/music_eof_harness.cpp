#include <soloud_wavstream.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Exercise the actual WavStream implementation with the original game files.
// The mono case uses the first channel of the stereo resource, so both getAudio
// branches are covered without modifying or committing any game assets.
int main(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    const unsigned int channels = std::atoi(argv[2]);
    const bool looping = std::atoi(argv[3]) != 0;
    if (channels != 1 && channels != 2)
        return 3;

    SoLoud::WavStream music;
    const SoLoud::result result = music.load(argv[1]);
    if (result != SoLoud::SO_NO_ERROR) {
        std::printf("LOAD_ERROR result=%u\n", result);
        return 4;
    }
    if (music.mSampleCount <= 1105 || music.mChannels != 2)
        return 5;
    music.mChannels = channels;
    if (looping)
        music.mFlags |= SoLoud::AudioSource::SHOULD_LOOP;
    else
        music.mFlags &= ~SoLoud::AudioSource::SHOULD_LOOP;

    SoLoud::AudioSourceInstance *voice = music.createInstance();
    if (!voice)
        return 6;
    voice->init(music, 1);
    const unsigned int block = 512;
    const unsigned long long target =
        (looping ? 2ull : 1ull) * music.mSampleCount + block;
    const unsigned long long calls = (target + block - 1) / block;
    std::vector<float> output(block * channels);
    std::printf("BEGIN declared=%u rate=%.0f channels=%u loop=%u calls=%llu\n",
                music.mSampleCount, music.mBaseSamplerate, channels,
                static_cast<unsigned int>(looping), calls);
    std::fflush(stdout);

    bool reachedEofBoundary = false;
    unsigned long long completed = 0;
    for (; completed < calls; ++completed) {
        // The seven original assets have a 1105-sample declaration gap. A
        // negative timeout counts only after reaching the known EOF boundary,
        // so a slow host cannot pass merely by timing out during normal decode.
        if (!reachedEofBoundary &&
            (completed + 1) * block > music.mSampleCount - 1105) {
            std::printf("EOF_BOUNDARY call=%llu\n", completed);
            std::fflush(stdout);
            reachedEofBoundary = true;
        }
        voice->getAudio(output.data(), block);
        if (!looping && voice->hasEnded())
            break;
    }
    const bool ended = voice->hasEnded();
    const unsigned int loops = voice->mLoopCount;
    const bool valid = looping ? loops == 2 && !ended : loops == 0 && ended;
    std::printf("COMPLETE calls=%llu loops=%u ended=%u\n", completed, loops,
                static_cast<unsigned int>(ended));
    std::fflush(stdout);
    delete voice;
    return valid ? 0 : 7;
}
