#pragma once
// Keep network activation independent of microphone readiness. UI labels must
// never disguise an initialization failure as an intentional microphone mute.
namespace FactoryXiaozhiState {
enum class Action { ConnectWifi, RetryService, CheckActivation, WaitMicrophone, Talk, Stop };
inline Action action(bool online, bool service, bool bound, bool microphone, bool busy) {
    if (busy) return Action::Stop;
    if (!online) return Action::ConnectWifi;
    if (!service) return Action::RetryService;
    if (!bound) return Action::CheckActivation;
    if (!microphone) return Action::WaitMicrophone;
    return Action::Talk;
}
inline const char *detail(const char *network, const char *audio, bool muted, bool binding, bool error) {
    if (binding || error) return network;
    if (audio && *audio) return audio;
    if (muted) return "麦克风已关闭，点击下方开启";
    return network;
}
}
