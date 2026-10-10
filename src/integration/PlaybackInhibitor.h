#pragma once

#include <memory>

// Owns the desktop's inhibition requests; never changes the user's power settings.
class PlaybackInhibitor
{
public:
    PlaybackInhibitor();
    ~PlaybackInhibitor();
    PlaybackInhibitor(const PlaybackInhibitor &) = delete;
    PlaybackInhibitor &operator=(const PlaybackInhibitor &) = delete;

    void setPlaying(bool playing);

private:
    struct State;
    std::shared_ptr<State> m_state;
};
