/*
 * SPDX-License-Identifier: CC0-1.0
 */
#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "boost/chrono.hpp"
#include "boost/thread.hpp"
#include "brookesia/hal_interface.hpp"
#include "brookesia/lib_utils/thread_config.hpp"
#include "brookesia/service_audio.hpp"
#include "brookesia/service_helper/media/audio.hpp"
#include "brookesia/system_core.hpp"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_log.h"

using namespace esp_brookesia;

namespace {

using AudioPlayback = service::helper::AudioPlayback;
constexpr std::string_view MUSIC_ID = "brookesia.example.music_player";
constexpr std::string_view SPECTRUM_ID = "brookesia.example.spectrum_analyser";
constexpr const char *MUSIC_FILE_PATH = "/littlefs/apps/brookesia.example.music_player/audio/example.mp3";
constexpr const char *TAG = "AudioExamples";

const char *music_gui = R"json({
  "version":"0.1.0","assets":[{
    "type":"viewScreen","id":"music","style":{"bgColor":"#101827","padding":"42dp"},
    "layout":{"type":"flex","flexFlow":"column","mainAlign":"center","crossAlign":"center","gap":"28dp"},
    "children":[
      {"type":"label","id":"title","labelProps":{"text":"Music Player"},"style":{"textColor":"#ffffff","fontSize":"42sp"},"placement":{"mode":"flow"}},
      {"type":"label","id":"track","labelProps":{"text":"Bundled example.mp3"},"style":{"textColor":"#a9b8ce","fontSize":"24sp"},"placement":{"mode":"flow"}},
      {"type":"label","id":"status","labelProps":{"text":"Ready"},"bindings":{"labelProps.text":"labelProps.text"},"style":{"textColor":"#55d6be","fontSize":"25sp"},"placement":{"mode":"flow"}},
      {"type":"container","id":"controls","style":{"bgOpacity":0,"borderWidth":"0dp"},"layout":{"type":"flex","flexFlow":"row","mainAlign":"center","crossAlign":"center","gap":"20dp"},"placement":{"mode":"flow","width":"match","height":"96dp"},"children":[
        {"type":"button","id":"play","events":[{"type":"clicked","action":"music.play"}],"style":{"bgColor":"#2563eb","radius":"18dp"},"placement":{"mode":"flow","width":"160dp","height":"72dp"},"children":[{"type":"label","id":"text","labelProps":{"text":"Play"},"style":{"textColor":"#ffffff","fontSize":"25sp"},"placement":{"mode":"relative","align":"center"}}]},
        {"type":"button","id":"pause","events":[{"type":"clicked","action":"music.pause"}],"style":{"bgColor":"#334155","radius":"18dp"},"placement":{"mode":"flow","width":"160dp","height":"72dp"},"children":[{"type":"label","id":"text","labelProps":{"text":"Pause"},"style":{"textColor":"#ffffff","fontSize":"25sp"},"placement":{"mode":"relative","align":"center"}}]},
        {"type":"button","id":"stop","events":[{"type":"clicked","action":"music.stop"}],"style":{"bgColor":"#334155","radius":"18dp"},"placement":{"mode":"flow","width":"160dp","height":"72dp"},"children":[{"type":"label","id":"text","labelProps":{"text":"Stop"},"style":{"textColor":"#ffffff","fontSize":"25sp"},"placement":{"mode":"relative","align":"center"}}]}
      ]},
      {"type":"container","id":"volume_controls","style":{"bgOpacity":0,"borderWidth":"0dp"},"layout":{"type":"flex","flexFlow":"row","mainAlign":"center","crossAlign":"center","gap":"16dp"},"placement":{"mode":"flow","width":"match","height":"78dp"},"children":[
        {"type":"button","id":"volume_down","events":[{"type":"clicked","action":"music.volume_down"}],"style":{"bgColor":"#334155","radius":"16dp"},"placement":{"mode":"flow","width":"130dp","height":"62dp"},"children":[{"type":"label","id":"text","labelProps":{"text":"Vol -"},"style":{"textColor":"#ffffff","fontSize":"22sp"},"placement":{"mode":"relative","align":"center"}}]},
        {"type":"label","id":"volume","labelProps":{"text":"70%"},"bindings":{"labelProps.text":"labelProps.text"},"style":{"textColor":"#ffffff","fontSize":"24sp"},"placement":{"mode":"flow","width":"90dp"}},
        {"type":"button","id":"volume_up","events":[{"type":"clicked","action":"music.volume_up"}],"style":{"bgColor":"#334155","radius":"16dp"},"placement":{"mode":"flow","width":"130dp","height":"62dp"},"children":[{"type":"label","id":"text","labelProps":{"text":"Vol +"},"style":{"textColor":"#ffffff","fontSize":"22sp"},"placement":{"mode":"relative","align":"center"}}]},
        {"type":"button","id":"mute","events":[{"type":"clicked","action":"music.mute"}],"style":{"bgColor":"#7c3aed","radius":"16dp"},"placement":{"mode":"flow","width":"130dp","height":"62dp"},"children":[{"type":"label","id":"text","labelProps":{"text":"Mute"},"bindings":{"labelProps.text":"labelProps.text"},"style":{"textColor":"#ffffff","fontSize":"22sp"},"placement":{"mode":"relative","align":"center"}}]}
      ]},
      {"type":"button","id":"back","events":[{"type":"clicked","action":"app.close"}],"style":{"bgColor":"#1e293b","radius":"16dp"},"placement":{"mode":"flow","width":"220dp","height":"64dp"},"children":[{"type":"label","id":"text","labelProps":{"text":"Back"},"style":{"textColor":"#ffffff","fontSize":"22sp"},"placement":{"mode":"relative","align":"center"}}]}
    ]
  },{
    "type":"screenFlow","id":"music","screens":["music"],"initial":"music","transitions":[]
  }]})json";

std::string make_spectrum_gui()
{
    std::string bars;
    for (int i = 0; i < 16; ++i) {
        if (!bars.empty()) bars += ',';
        bars += "{\"type\":\"progressBar\",\"id\":\"bar" + std::to_string(i) +
                "\",\"rangeProps\":{\"value\":0,\"min\":0,\"max\":100},\"bindings\":{\"rangeProps.value\":\"value\"},"
                "\"style\":{\"bgColor\":\"#1e293b\",\"radius\":\"5dp\"},\"partStyles\":{\"indicator\":{\"bgColor\":\"#22d3ee\",\"radius\":\"5dp\"}},"
                "\"placement\":{\"mode\":\"flow\",\"width\":\"28dp\",\"height\":\"320dp\"}}";
    }
    return "{\"version\":\"0.1.0\",\"assets\":[{\"type\":\"viewScreen\",\"id\":\"spectrum\","
           "\"style\":{\"bgColor\":\"#07111f\",\"padding\":\"36dp\"},\"layout\":{\"type\":\"flex\",\"flexFlow\":\"column\",\"crossAlign\":\"center\",\"gap\":\"20dp\"},\"children\":["
           "{\"type\":\"label\",\"id\":\"title\",\"labelProps\":{\"text\":\"Microphone Spectrum\"},\"style\":{\"textColor\":\"#ffffff\",\"fontSize\":\"38sp\"},\"placement\":{\"mode\":\"flow\"}},"
           "{\"type\":\"label\",\"id\":\"status\",\"labelProps\":{\"text\":\"Opening microphone...\"},\"bindings\":{\"labelProps.text\":\"labelProps.text\"},\"style\":{\"textColor\":\"#94a3b8\",\"fontSize\":\"22sp\"},\"placement\":{\"mode\":\"flow\"}},"
           "{\"type\":\"progressBar\",\"id\":\"peak\",\"rangeProps\":{\"value\":0,\"min\":0,\"max\":100},\"bindings\":{\"rangeProps.value\":\"value\"},\"style\":{\"bgColor\":\"#1e293b\",\"radius\":\"7dp\"},\"partStyles\":{\"indicator\":{\"bgColor\":\"#22c55e\",\"radius\":\"7dp\"}},\"placement\":{\"mode\":\"flow\",\"width\":\"520dp\",\"height\":\"22dp\"}},"
           "{\"type\":\"container\",\"id\":\"bars\",\"style\":{\"bgColor\":\"#0f172a\",\"radius\":\"18dp\",\"padding\":\"20dp\"},\"layout\":{\"type\":\"flex\",\"flexFlow\":\"row\",\"mainAlign\":\"spaceEvenly\",\"crossAlign\":\"end\",\"gap\":\"7dp\"},\"placement\":{\"mode\":\"flow\",\"width\":\"match\",\"height\":\"380dp\"},\"children\":[" + bars + "]},"
           "{\"type\":\"button\",\"id\":\"back\",\"events\":[{\"type\":\"clicked\",\"action\":\"app.close\"}],\"style\":{\"bgColor\":\"#1e293b\",\"radius\":\"16dp\"},\"placement\":{\"mode\":\"flow\",\"width\":\"220dp\",\"height\":\"64dp\"},\"children\":[{\"type\":\"label\",\"id\":\"text\",\"labelProps\":{\"text\":\"Back\"},\"style\":{\"textColor\":\"#ffffff\",\"fontSize\":\"22sp\"},\"placement\":{\"mode\":\"relative\",\"align\":\"center\"}}]}]},"
           "{\"type\":\"screenFlow\",\"id\":\"spectrum\",\"screens\":[\"spectrum\"],\"initial\":\"spectrum\",\"transitions\":[]}]}";
}

class MusicPlayerApp final : public system::core::IApp {
public:
    ~MusicPlayerApp() override { stop_audio(); }
    system::core::AppManifest get_manifest() const override {
        system::core::AppManifest manifest{};
        manifest.id = MUSIC_ID;
        manifest.name = "Music Player";
        manifest.version = "0.1.0";
        manifest.kind = system::core::AppKind::Native;
        manifest.visible = true;
        return manifest;
    }
    system::core::AppGuiDescriptor get_gui_descriptor() const override {
        system::core::AppGuiDescriptor descriptor{};
        descriptor.root_kind = system::core::GuiRootKind::JsonString;
        descriptor.root = music_gui;
        descriptor.screen_flows = {{.screen_flow="music"}};
        return descriptor;
    }
    std::expected<void,std::string> on_start(system::core::AppContext &c) override {
        context_=&c;
        auto conns=c.gui().subscribe_actions({
            {.action="music.play",.handler=[this](const gui::Event &){ play(); }},
            {.action="music.pause",.handler=[this](const gui::Event &){ pause(); }},
            {.action="music.stop",.handler=[this](const gui::Event &){ stop(); }},
            {.action="music.volume_down",.handler=[this](const gui::Event &){ change_volume(-10); }},
            {.action="music.volume_up",.handler=[this](const gui::Event &){ change_volume(10); }},
            {.action="music.mute",.handler=[this](const gui::Event &){ toggle_mute(); }},
            {.action="app.close",.handler=[this](const gui::Event &){ if(context_) context_->system_service().request_close_app(context_->app_id()); }}
        });
        connections_=std::move(conns);
        if(auto timer=c.timer().start_periodic("music.refresh",250); timer) timer_=*timer;
        if(auto volume=AudioPlayback::call_function_sync<double>(AudioPlayback::FunctionId::GetVolume); volume){
            volume_=static_cast<int>(std::clamp(*volume,0.0,100.0));
            (void)c.gui().set_text("/music/volume_controls/volume",std::to_string(volume_.load())+"%");
        }
        if(auto mute=AudioPlayback::call_function_sync<bool>(AudioPlayback::FunctionId::GetMute); mute){
            muted_=*mute;
            (void)c.gui().set_text("/music/volume_controls/mute/text",muted_ ? "Unmute" : "Mute");
        }
        return {};
    }
    std::expected<void,std::string> on_timer(system::core::AppContext &c,system::core::TimerId,std::string_view) override {
        if(auto volume=AudioPlayback::call_function_sync<double>(AudioPlayback::FunctionId::GetVolume); volume){
            const int current=static_cast<int>(std::clamp(*volume,0.0,100.0));
            if(current!=volume_.exchange(current)) (void)c.gui().set_text("/music/volume_controls/volume",std::to_string(current)+"%");
        }
        if(auto mute=AudioPlayback::call_function_sync<bool>(AudioPlayback::FunctionId::GetMute); mute){
            if(*mute!=muted_.exchange(*mute)) (void)c.gui().set_text("/music/volume_controls/mute/text",*mute ? "Unmute" : "Mute");
        }
        const auto state=static_cast<PlaybackState>(playback_state_.load());
        if(state!=last_displayed_state_){
            last_displayed_state_=state;
            set_status(state_text(state));
        }
        return {};
    }
    std::expected<void,std::string> on_stop(system::core::AppContext &c) override {
        if(timer_) c.timer().stop(timer_);
        stop_audio(); connections_.clear(); context_=nullptr; return {};
    }
private:
    enum class PlaybackState { Ready, Loading, Playing, Paused, Stopped, FileError, DecoderError, FormatError, OutputError };

    static std::string_view state_text(PlaybackState state){
        switch(state){
            case PlaybackState::Ready: return "Ready";
            case PlaybackState::Loading: return "Loading example.mp3...";
            case PlaybackState::Playing: return "Playing example.mp3";
            case PlaybackState::Paused: return "Paused";
            case PlaybackState::Stopped: return "Stopped";
            case PlaybackState::FileError: return "Cannot open example.mp3";
            case PlaybackState::DecoderError: return "MP3 decoder failed";
            case PlaybackState::FormatError: return "Unsupported MP3 format";
            case PlaybackState::OutputError: return "Speaker output failed";
        }
        return "Audio error";
    }
    void set_status(std::string_view s){ if(context_) (void)context_->gui().set_text("/music/status",s); }
    void play(){
        stop_audio();
        player_=hal::acquire_first_interface<hal::audio::CodecPlayerIface>();
        if(!player_){ set_status("Speaker unavailable"); return; }
        stop_requested_=false; paused_=false; running_=true;
        playback_state_=static_cast<int>(PlaybackState::Loading);
        last_displayed_state_=PlaybackState::Loading;
        set_status(state_text(PlaybackState::Loading));
        BROOKESIA_THREAD_CONFIG_GUARD({.stack_size=28*1024});
        worker_=boost::thread([this]{ playback_loop(); });
    }
    void pause(){
        if(running_){
            const bool target=!paused_.load(); paused_=target;
            playback_state_=static_cast<int>(target ? PlaybackState::Paused : PlaybackState::Playing);
            set_status(state_text(target ? PlaybackState::Paused : PlaybackState::Playing));
        }
    }
    void change_volume(int delta){
        const int target=std::clamp(volume_.load()+delta,0,100);
        auto result=AudioPlayback::call_function_sync(AudioPlayback::FunctionId::SetVolume,static_cast<double>(target));
        if(result){
            volume_=target;
            if(context_) (void)context_->gui().set_text("/music/volume_controls/volume",std::to_string(target)+"%");
        } else set_status("Volume control failed");
    }
    void toggle_mute(){
        const bool target=!muted_.load();
        auto result=AudioPlayback::call_function_sync(AudioPlayback::FunctionId::SetMute,target);
        if(result){
            muted_=target;
            if(context_) (void)context_->gui().set_text("/music/volume_controls/mute/text",target ? "Unmute" : "Mute");
        } else set_status("Mute control failed");
    }
    void stop(){ stop_audio(); playback_state_=static_cast<int>(PlaybackState::Stopped); if(context_) set_status("Stopped"); }
    void stop_audio(){
        stop_requested_=true; paused_=false;
        if(worker_.joinable()) worker_.join();
        if(player_ && player_open_.exchange(false)){
            if(player_->is_pa_on_off_supported()) (void)player_->set_pa_on_off(false);
            player_->close();
        }
        player_.reset(); running_=false;
    }

    bool write_fade(std::vector<int16_t> &last_samples,uint32_t sample_rate,uint8_t channels){
        if(!player_ || last_samples.size()!=channels || channels==0) return false;
        const size_t frames=std::max<size_t>(sample_rate/100,1); // 10 ms
        std::vector<int16_t> fade(frames*channels);
        for(size_t n=0;n<frames;++n){
            const float gain=1.0f-static_cast<float>(n+1)/static_cast<float>(frames);
            for(size_t ch=0;ch<channels;++ch) fade[n*channels+ch]=static_cast<int16_t>(last_samples[ch]*gain);
        }
        std::fill(last_samples.begin(),last_samples.end(),0);
        return player_->write_data(reinterpret_cast<const uint8_t*>(fade.data()),fade.size()*sizeof(int16_t));
    }

    void playback_loop(){
        static std::once_flag codec_registration;
        std::call_once(codec_registration,[]{
            (void)esp_audio_dec_register_default();
            (void)esp_audio_simple_dec_register_default();
        });

        FILE *file=std::fopen(MUSIC_FILE_PATH,"rb");
        if(!file){
            ESP_LOGE(TAG,"Failed to open %s",MUSIC_FILE_PATH);
            playback_state_=static_cast<int>(PlaybackState::FileError); running_=false; return;
        }
        esp_audio_simple_dec_handle_t decoder=nullptr;
        esp_audio_simple_dec_cfg_t config{
            .dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
            .dec_cfg=nullptr,
            .cfg_size=0,
            .use_frame_dec=false,
        };
        if(esp_audio_simple_dec_open(&config,&decoder)!=ESP_AUDIO_ERR_OK){
            std::fclose(file); playback_state_=static_cast<int>(PlaybackState::DecoderError); running_=false; return;
        }

        constexpr size_t input_size=1024;
        std::array<uint8_t,input_size> input{};
        std::vector<uint8_t> output(8192);
        std::vector<int16_t> last_samples;
        esp_audio_simple_dec_info_t info{};
        bool output_open=false,was_paused=false,failed=false;
        float gain=0.0f;

        while(!stop_requested_ && !failed){
            if(paused_ && output_open){
                if(!was_paused && !write_fade(last_samples,info.sample_rate,info.channel)){ failed=true; break; }
                was_paused=true;
                const size_t silence_frames=std::max<size_t>(info.sample_rate/100,1);
                std::vector<int16_t> silence(silence_frames*info.channel,0);
                if(!player_->write_data(reinterpret_cast<const uint8_t*>(silence.data()),silence.size()*sizeof(int16_t))){ failed=true; break; }
                continue;
            }
            if(was_paused){ gain=0.0f; was_paused=false; }

            const size_t count=std::fread(input.data(),1,input.size(),file);
            if(count==0){
                if(std::ferror(file)){ playback_state_=static_cast<int>(PlaybackState::FileError); failed=true; break; }
                std::rewind(file);
                if(esp_audio_simple_dec_reset(decoder)!=ESP_AUDIO_ERR_OK){ failed=true; break; }
                continue;
            }
            esp_audio_simple_dec_raw_t raw{
                .buffer=input.data(),
                .len=static_cast<uint32_t>(count),
                .eos=count<input.size(),
                .consumed=0,
                .frame_recover=ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE,
            };
            while(raw.len && !stop_requested_){
                esp_audio_simple_dec_out_t frame{
                    .buffer=output.data(),
                    .len=static_cast<uint32_t>(output.size()),
                    .needed_size=0,
                    .decoded_size=0,
                };
                auto result=esp_audio_simple_dec_process(decoder,&raw,&frame);
                if(result==ESP_AUDIO_ERR_BUFF_NOT_ENOUGH){ output.resize(frame.needed_size); continue; }
                if(result!=ESP_AUDIO_ERR_OK || (raw.consumed==0 && frame.decoded_size==0)){
                    ESP_LOGE(TAG,"MP3 decode failed: %d",static_cast<int>(result));
                    playback_state_=static_cast<int>(PlaybackState::DecoderError); failed=true; break;
                }
                raw.buffer+=raw.consumed; raw.len-=raw.consumed;
                if(frame.decoded_size==0) continue;

                if(!output_open){
                    if(esp_audio_simple_dec_get_info(decoder,&info)!=ESP_AUDIO_ERR_OK || info.bits_per_sample!=16 ||
                            info.channel==0 || info.channel>2 || info.sample_rate==0){
                        playback_state_=static_cast<int>(PlaybackState::FormatError); failed=true; break;
                    }
                    if(!player_->open({.bits=info.bits_per_sample,.channels=info.channel,.sample_rate=info.sample_rate})){
                        playback_state_=static_cast<int>(PlaybackState::OutputError); failed=true; break;
                    }
                    if(player_->is_pa_on_off_supported()) (void)player_->set_pa_on_off(true);
                    output_open=true; player_open_=true; last_samples.assign(info.channel,0);
                    playback_state_=static_cast<int>(PlaybackState::Playing);
                    ESP_LOGI(TAG,"Playing MP3: %" PRIu32 " Hz, %u channel(s), %u-bit",info.sample_rate,info.channel,info.bits_per_sample);
                }

                auto *pcm=reinterpret_cast<int16_t*>(frame.buffer);
                const size_t samples=frame.decoded_size/sizeof(int16_t);
                const float gain_step=1.0f/static_cast<float>(std::max<uint32_t>(info.sample_rate/50,1)); // 20 ms
                for(size_t i=0;i<samples;++i){
                    gain=std::min(1.0f,gain+gain_step);
                    pcm[i]=static_cast<int16_t>(static_cast<float>(pcm[i])*gain);
                    last_samples[i%info.channel]=pcm[i];
                }
                if(!player_->write_data(frame.buffer,frame.decoded_size)){
                    playback_state_=static_cast<int>(PlaybackState::OutputError); failed=true; break;
                }
            }
        }
        if(output_open && !was_paused) (void)write_fade(last_samples,info.sample_rate,info.channel);
        esp_audio_simple_dec_close(decoder); std::fclose(file);
        if(failed && playback_state_.load()==static_cast<int>(PlaybackState::Playing)) playback_state_=static_cast<int>(PlaybackState::OutputError);
        running_=false;
    }
    system::core::AppContext *context_=nullptr; system::core::TimerId timer_=0;
    std::atomic_bool running_=false,paused_=false,stop_requested_=true,muted_=false,player_open_=false;
    std::atomic_int volume_=70;
    std::atomic_int playback_state_=static_cast<int>(PlaybackState::Ready);
    PlaybackState last_displayed_state_=PlaybackState::Ready;
    hal::InterfaceHandle<hal::audio::CodecPlayerIface> player_; boost::thread worker_;
    std::vector<gui::ScopedConnection> connections_;
};

class SpectrumApp final : public system::core::IApp {
public:
    ~SpectrumApp() override { stop_capture(); }
    system::core::AppManifest get_manifest() const override {
        system::core::AppManifest manifest{};
        manifest.id = SPECTRUM_ID;
        manifest.name = "Spectrum Analyser";
        manifest.version = "0.1.0";
        manifest.kind = system::core::AppKind::Native;
        manifest.visible = true;
        return manifest;
    }
    system::core::AppGuiDescriptor get_gui_descriptor() const override {
        system::core::AppGuiDescriptor descriptor{};
        descriptor.root_kind = system::core::GuiRootKind::JsonString;
        descriptor.root = make_spectrum_gui();
        descriptor.screen_flows = {{.screen_flow="spectrum"}};
        return descriptor;
    }
    std::expected<void,std::string> on_start(system::core::AppContext &c) override {
        context_=&c;
        connections_=c.gui().subscribe_actions({{.action="app.close",.handler=[this](const gui::Event &){ if(context_) context_->system_service().request_close_app(context_->app_id()); }}});
        auto timer=c.timer().start_periodic("spectrum.refresh",80); if(timer) timer_=*timer;
        recorder_=hal::acquire_first_interface<hal::audio::CodecRecorderIface>();
        if(!recorder_ || !recorder_->open()){
            capture_state_=-1; (void)c.gui().set_text("/spectrum/status","Microphone unavailable"); return {};
        }
        const auto info=recorder_->get_info(); channels_=std::max<int>(info.channels,1); sample_rate_=info.sample_rate;
        capture_state_=1; running_=true;
        BROOKESIA_THREAD_CONFIG_GUARD({.stack_size=20*1024});
        worker_=boost::thread([this]{ capture_loop(); }); return {};
    }
    std::expected<void,std::string> on_timer(system::core::AppContext &c,system::core::TimerId,std::string_view) override {
        for(size_t i=0;i<levels_.size();++i) (void)c.gui().set_value("/spectrum/bars/bar"+std::to_string(i),levels_[i].load());
        (void)c.gui().set_value("/spectrum/peak",peak_meter_.load());
        if(capture_state_.load()<0){
            (void)c.gui().set_text("/spectrum/status",capture_state_.load()==-1 ? "Microphone unavailable" : "Microphone read failed");
        } else {
            const int db10=peak_db_tenths_.load();
            const std::string db=(db10<=-960) ? "silence" : std::to_string(db10/10)+"."+std::to_string(std::abs(db10%10))+" dBFS";
            (void)c.gui().set_text("/spectrum/status","Mic "+std::to_string(channels_.load())+"ch @ "+
                std::to_string(sample_rate_.load())+" Hz | strongest "+std::to_string(active_channel_.load()+1)+" | "+db);
        }
        return {};
    }
    std::expected<void,std::string> on_stop(system::core::AppContext &c) override { if(timer_) c.timer().stop(timer_); stop_capture(); connections_.clear(); context_=nullptr; return {}; }
private:
    void stop_capture(){ running_=false; if(worker_.joinable()) worker_.join(); if(recorder_) recorder_->close(); recorder_.reset(); }
    void capture_loop(){
        constexpr size_t frames=512;
        constexpr float display_floor_db=-75.0f;
        constexpr float display_ceiling_db=-15.0f;
        constexpr float display_range_db=display_ceiling_db-display_floor_db;
        const auto info=recorder_->get_info(); const size_t channels=std::max<size_t>(info.channels,1);
        const float sample_rate=static_cast<float>(info.sample_rate ? info.sample_rate : 48000);
        std::vector<int16_t> pcm(frames*channels);
        constexpr float pi=3.14159265358979323846f;
        while(running_){
            if(!recorder_->read_data(reinterpret_cast<uint8_t*>(pcm.data()),pcm.size()*sizeof(int16_t))){ capture_state_=-2; break; }
            size_t best_channel=0; uint64_t best_energy=0; int32_t best_mean=0;
            for(size_t ch=0;ch<channels;++ch){
                int64_t sum=0; for(size_t n=0;n<frames;++n) sum+=pcm[n*channels+ch];
                const int32_t mean=static_cast<int32_t>(sum/static_cast<int64_t>(frames));
                uint64_t energy=0;
                for(size_t n=0;n<frames;++n){ const int64_t s=static_cast<int32_t>(pcm[n*channels+ch])-mean; energy+=static_cast<uint64_t>(s*s); }
                if(energy>best_energy){ best_energy=energy; best_channel=ch; best_mean=mean; }
            }
            active_channel_=static_cast<int>(best_channel);
            int32_t peak=0;
            for(size_t n=0;n<frames;++n) peak=std::max(peak,std::abs(static_cast<int32_t>(pcm[n*channels+best_channel])-best_mean));
            const float peak_db=peak>0 ? 20.0f*std::log10(static_cast<float>(peak)/32768.0f) : -96.0f;
            peak_db_tenths_=static_cast<int>(std::round(std::max(peak_db,-96.0f)*10.0f));
            const int meter=std::clamp(static_cast<int>((peak_db-display_floor_db)*100.0f/display_range_db),0,100);
            peak_meter_=(peak_meter_.load()*3+meter)/4;
            for(size_t band=0;band<levels_.size();++band){
                const float freq=125.0f*std::pow(1.32f,static_cast<float>(band));
                const float coefficient=2.0f*std::cos(2.0f*pi*freq/sample_rate);
                float s1=0.0f,s2=0.0f;
                for(size_t n=0;n<frames;++n){
                    const float sample=(static_cast<int32_t>(pcm[n*channels+best_channel])-best_mean)/32768.0f;
                    const float s0=sample+coefficient*s1-s2; s2=s1; s1=s0;
                }
                const float power=std::max(s1*s1+s2*s2-coefficient*s1*s2,0.0f);
                // Goertzel yields one side of the spectrum for real PCM. Double it
                // to report the equivalent sinusoidal amplitude.
                const float magnitude=2.0f*std::sqrt(power)/static_cast<float>(frames);
                const float db=20.0f*std::log10(magnitude+0.000001f);
                const int value=std::clamp(static_cast<int>((db-display_floor_db)*100.0f/display_range_db),0,100);
                levels_[band]=(levels_[band].load()*2+value)/3;
            }
        }
        running_=false;
    }
    system::core::AppContext *context_=nullptr; system::core::TimerId timer_=0; std::atomic_bool running_=false;
    hal::InterfaceHandle<hal::audio::CodecRecorderIface> recorder_; boost::thread worker_;
    std::array<std::atomic_int,16> levels_{};
    std::atomic_int capture_state_=0,peak_meter_=0,peak_db_tenths_=-960,active_channel_=0,channels_=0;
    std::atomic_uint32_t sample_rate_=0;
    std::vector<gui::ScopedConnection> connections_;
};

template<class App> class Provider final : public system::core::IAppProvider {
public: system::core::AppManifest get_manifest() const override { return App().get_manifest(); }
std::shared_ptr<system::core::IApp> create_app() override { return std::make_shared<App>(); }};
using MusicProvider=Provider<MusicPlayerApp>; using SpectrumProvider=Provider<SpectrumApp>;

BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(MusicProvider,"brookesia.example.music_player",tab5_music_player_provider_symbol);
BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(SpectrumProvider,"brookesia.example.spectrum_analyser",tab5_spectrum_analyser_provider_symbol);

} // namespace
