// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "core/AudioMuter.h"

#include <windows.h>
#include <endpointvolume.h>

#include <algorithm>

namespace hideutil {
namespace core {

namespace {

void ReleaseSessionEnumerator(IAudioSessionEnumerator* enumerator) {
    if (enumerator != nullptr) enumerator->Release();
}

// Read one session's mute state / volume into a snapshot.
bool ReadSessionState(IAudioSessionControl* control, AudioSnapshot& out) {
    ISimpleAudioVolume* volume = nullptr;
    if (FAILED(control->QueryInterface(IID_PPV_ARGS(&volume))) || volume == nullptr) {
        return false;
    }
    BOOL muted = FALSE;
    float level = 1.0f;
    volume->GetMute(&muted);
    volume->GetMasterVolume(&level);
    out.wasMuted = muted;
    out.prevVolume = level;
    volume->Release();
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// IAudioSessionNotification implementation.
// ---------------------------------------------------------------------------

class AudioSessionNotifier : public IAudioSessionNotification {
public:
    explicit AudioSessionNotifier(AudioMuter* owner) : owner_(owner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (ppvObject == nullptr) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioSessionNotification)) {
            *ppvObject = static_cast<IAudioSessionNotification*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refCount_));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG count = InterlockedDecrement(&refCount_);
        if (count == 0) {
            delete this;
        }
        return static_cast<ULONG>(count);
    }

    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl* newSession) override {
        if (owner_ != nullptr && newSession != nullptr) {
            owner_->MuteSessionControl(newSession);
        }
        return S_OK;
    }

private:
    ~AudioSessionNotifier() = default;
    LONG refCount_ = 1;
    AudioMuter* owner_;
};

// ---------------------------------------------------------------------------
// AudioMuter.
// ---------------------------------------------------------------------------

AudioMuter::AudioMuter() = default;

AudioMuter::~AudioMuter() {
    Uninitialize();
}

bool AudioMuter::Initialize() {
    return EnsureSessionManager();
}

bool AudioMuter::EnsureSessionManager() {
    if (sessionManager_ != nullptr) {
        return true;
    }
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator_));
    if (FAILED(hr) || enumerator_ == nullptr) {
        return false;
    }
    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (FAILED(hr) || device_ == nullptr) {
        return false;
    }
    hr = device_->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&sessionManager_));
    if (FAILED(hr) || sessionManager_ == nullptr) {
        return false;
    }
    return true;
}

void AudioMuter::Uninitialize() {
    UnregisterSessionNotification();
    if (sessionManager_ != nullptr) {
        sessionManager_->Release();
        sessionManager_ = nullptr;
    }
    if (device_ != nullptr) {
        device_->Release();
        device_ = nullptr;
    }
    if (enumerator_ != nullptr) {
        enumerator_->Release();
        enumerator_ = nullptr;
    }
}

std::vector<AudioSnapshot> AudioMuter::SnapshotSessions(DWORD pid) {
    std::vector<AudioSnapshot> result;
    if (!EnsureSessionManager()) {
        return result;
    }
    IAudioSessionEnumerator* enumerator = nullptr;
    if (FAILED(sessionManager_->GetSessionEnumerator(&enumerator)) || enumerator == nullptr) {
        return result;
    }
    int count = 0;
    if (SUCCEEDED(enumerator->GetCount(&count))) {
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* control = nullptr;
            if (FAILED(enumerator->GetSession(i, &control)) || control == nullptr) {
                continue;
            }
            IAudioSessionControl2* control2 = nullptr;
            if (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&control2))) && control2 != nullptr) {
                DWORD sessionPid = 0;
                if (SUCCEEDED(control2->GetProcessId(&sessionPid)) && sessionPid == pid) {
                    AudioSnapshot snapshot;
                    snapshot.pid = sessionPid;
                    LPWSTR id = nullptr;
                    if (SUCCEEDED(control2->GetSessionIdentifier(&id)) && id != nullptr) {
                        snapshot.sessionId.assign(id);
                        CoTaskMemFree(id);
                    }
                    if (ReadSessionState(control, snapshot)) {
                        result.push_back(std::move(snapshot));
                    }
                }
                control2->Release();
            }
            control->Release();
        }
    }
    ReleaseSessionEnumerator(enumerator);
    return result;
}

bool AudioMuter::MuteSessions(DWORD pid) {
    if (!EnsureSessionManager()) {
        return false;
    }
    bool mutedAny = false;
    IAudioSessionEnumerator* enumerator = nullptr;
    if (FAILED(sessionManager_->GetSessionEnumerator(&enumerator)) || enumerator == nullptr) {
        return false;
    }
    int count = 0;
    if (SUCCEEDED(enumerator->GetCount(&count))) {
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* control = nullptr;
            if (FAILED(enumerator->GetSession(i, &control)) || control == nullptr) {
                continue;
            }
            IAudioSessionControl2* control2 = nullptr;
            if (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&control2))) && control2 != nullptr) {
                DWORD sessionPid = 0;
                if (SUCCEEDED(control2->GetProcessId(&sessionPid)) && sessionPid == pid) {
                    ISimpleAudioVolume* volume = nullptr;
                    if (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&volume))) && volume != nullptr) {
                        if (SUCCEEDED(volume->SetMute(TRUE, nullptr))) {
                            mutedAny = true;
                        }
                        volume->Release();
                    }
                }
                control2->Release();
            }
            control->Release();
        }
    }
    ReleaseSessionEnumerator(enumerator);
    return mutedAny;
}

bool AudioMuter::RestoreSessions(const std::vector<AudioSnapshot>& snapshots) {
    if (snapshots.empty() || !EnsureSessionManager()) {
        return false;
    }
    bool restoredAny = false;
    IAudioSessionEnumerator* enumerator = nullptr;
    if (FAILED(sessionManager_->GetSessionEnumerator(&enumerator)) || enumerator == nullptr) {
        return false;
    }
    int count = 0;
    if (SUCCEEDED(enumerator->GetCount(&count))) {
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* control = nullptr;
            if (FAILED(enumerator->GetSession(i, &control)) || control == nullptr) {
                continue;
            }
            IAudioSessionControl2* control2 = nullptr;
            if (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&control2))) && control2 != nullptr) {
                LPWSTR id = nullptr;
                if (SUCCEEDED(control2->GetSessionIdentifier(&id)) && id != nullptr) {
                    const std::wstring sessionId(id);
                    CoTaskMemFree(id);
                    auto it = std::find_if(snapshots.begin(), snapshots.end(),
                                           [&](const AudioSnapshot& s) {
                                               return s.sessionId == sessionId;
                                           });
                    if (it != snapshots.end()) {
                        ISimpleAudioVolume* volume = nullptr;
                        if (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&volume))) && volume != nullptr) {
                            volume->SetMute(it->wasMuted, nullptr);
                            volume->SetMasterVolume(it->prevVolume, nullptr);
                            restoredAny = true;
                            volume->Release();
                        }
                    }
                }
                control2->Release();
            }
            control->Release();
        }
    }
    ReleaseSessionEnumerator(enumerator);
    return restoredAny;
}

bool AudioMuter::RegisterSessionNotification(const std::vector<DWORD>& pids) {
    if (!EnsureSessionManager()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        targetPids_ = pids;
    }
    UnregisterSessionNotification();
    notifier_ = new AudioSessionNotifier(this);
    const HRESULT hr = sessionManager_->RegisterSessionNotification(notifier_);
    if (FAILED(hr)) {
        notifier_->Release();
        notifier_ = nullptr;
        return false;
    }
    return true;
}

void AudioMuter::UnregisterSessionNotification() {
    if (notifier_ != nullptr) {
        if (sessionManager_ != nullptr) {
            sessionManager_->UnregisterSessionNotification(notifier_);
        }
        notifier_->Release();
        notifier_ = nullptr;
    }
}

void AudioMuter::MuteSessionControl(IAudioSessionControl* control) {
    if (control == nullptr) {
        return;
    }
    IAudioSessionControl2* control2 = nullptr;
    if (FAILED(control->QueryInterface(IID_PPV_ARGS(&control2))) || control2 == nullptr) {
        return;
    }
    DWORD sessionPid = 0;
    if (SUCCEEDED(control2->GetProcessId(&sessionPid))) {
        bool isTarget = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            isTarget = std::find(targetPids_.begin(), targetPids_.end(), sessionPid) != targetPids_.end();
        }
        if (isTarget) {
            ISimpleAudioVolume* volume = nullptr;
            if (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&volume))) && volume != nullptr) {
                volume->SetMute(TRUE, nullptr);
                volume->Release();
            }
        }
    }
    control2->Release();
}

}  // namespace core
}  // namespace hideutil
