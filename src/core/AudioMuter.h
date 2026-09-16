// WASAPI audio session muting.
//
// Flow (design document section 5.4):
//   MMDeviceEnumerator -> default render endpoint -> IAudioSessionManager2
//   -> IAudioSessionEnumerator -> IAudioSessionControl2::GetProcessId
//   -> ISimpleAudioVolume::SetMute
// plus IAudioSessionNotification to keep newly created sessions muted.
#pragma once

#include "core/Types.h"

#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <mutex>
#include <vector>

namespace hideutil {
namespace core {

class AudioSessionNotifier;

class AudioMuter {
public:
    AudioMuter();
    ~AudioMuter();

    AudioMuter(const AudioMuter&) = delete;
    AudioMuter& operator=(const AudioMuter&) = delete;

    bool Initialize();
    void Uninitialize();
    bool IsAvailable() const { return sessionManager_ != nullptr; }

    std::vector<AudioSnapshot> SnapshotSessions(DWORD pid);
    bool MuteSessions(DWORD pid);
    bool RestoreSessions(const std::vector<AudioSnapshot>& snapshots);

    // Keeps newly created sessions of the listed PIDs muted.
    bool RegisterSessionNotification(const std::vector<DWORD>& pids);
    void UnregisterSessionNotification();

    // Called from the session-notification callback.
    void MuteSessionControl(IAudioSessionControl* control);

private:
    bool EnsureSessionManager();

    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioSessionManager2* sessionManager_ = nullptr;
    AudioSessionNotifier* notifier_ = nullptr;

    std::mutex mutex_;
    std::vector<DWORD> targetPids_;
};

}  // namespace core
}  // namespace hideutil
