/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "reone/audio/clip.h"
#include "reone/audio/mixer.h"

using namespace reone::audio;

TEST(AudioMixer, missing_or_empty_clip_is_not_started) {
    AudioOptions options;
    AudioMixer mixer(options);

    EXPECT_FALSE(mixer.play(nullptr, AudioType::Sound));
    EXPECT_FALSE(mixer.play(
        std::make_shared<AudioClip>(),
        AudioType::Sound));
}

// Upstream #356: the movie hold.

#include "reone/audio/source.h"

TEST(AudioSource, a_source_created_held_counts_as_playing_and_a_stopped_one_does_not_resume) {
    auto clip = std::make_shared<AudioClip>();
    AudioClip::Frame frame;
    frame.sampleRate = 100;
    frame.samples.resize(100);
    clip->add(std::move(frame));

    AudioSource held(clip);
    held.startPaused();
    EXPECT_TRUE(held.isPlaying()) << "the mixer keeps a held source until the hold ends";
    held.resume();
    EXPECT_TRUE(held.isPlaying());

    AudioSource ended(clip);
    ended.startPaused();
    ended.stop();
    ended.resume();
    EXPECT_FALSE(ended.isPlaying()) << "a source that ended before the resume does not start again";
}

TEST(AudioMixer, holding_for_a_movie_is_a_state_that_toggles) {
    AudioOptions options;
    AudioMixer mixer(options);
    EXPECT_FALSE(mixer.soundsHeldForMovie());
    mixer.setSoundsHeldForMovie(true);
    EXPECT_TRUE(mixer.soundsHeldForMovie());
    mixer.setSoundsHeldForMovie(false);
    EXPECT_FALSE(mixer.soundsHeldForMovie());
}
