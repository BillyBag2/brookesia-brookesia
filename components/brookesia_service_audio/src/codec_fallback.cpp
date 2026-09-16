/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include <atomic>
#include <mutex>

#include "private/audio_impl.hpp"
#include "brookesia/hal_interface/interfaces/audio/codec_player.hpp"
#include "brookesia/hal_interface/interfaces/audio/codec_recorder.hpp"

namespace esp_brookesia::service {
namespace {

class CodecDecoderFallback final : public hal::audio::DecoderIface {
public:
    CodecDecoderFallback()
        : player_(hal::acquire_first_interface<hal::audio::CodecPlayerIface>())
    {}

    bool valid() const { return static_cast<bool>(player_); }

    bool start(const hal::audio::DecoderDynamicConfig &config) override
    {
        std::lock_guard lock(mutex_);
        if (!player_ || config.type != hal::audio::CodecFormat::PCM ||
                config.general.sample_bits != 16 || config.general.channels == 0 ||
                config.general.channels > 2 || config.general.sample_rate == 0) {
            return false;
        }
        if (started_) {
            player_->close();
            started_ = false;
        }
        started_ = player_->open({
            .bits = config.general.sample_bits,
            .channels = config.general.channels,
            .sample_rate = config.general.sample_rate,
        });
        if (started_ && player_->is_pa_on_off_supported()) {
            started_ = player_->set_pa_on_off(true);
            if (!started_) player_->close();
        }
        return started_;
    }

    void stop() override
    {
        std::lock_guard lock(mutex_);
        if (!started_) return;
        if (player_->is_pa_on_off_supported()) (void)player_->set_pa_on_off(false);
        player_->close();
        started_ = false;
    }

    bool is_started() const override { return started_.load(); }

    bool feed_data(const uint8_t *data, size_t size) override
    {
        std::lock_guard lock(mutex_);
        return started_ && data && size && player_->write_data(data, size);
    }

private:
    hal::InterfaceHandle<hal::audio::CodecPlayerIface> player_;
    mutable std::mutex mutex_;
    std::atomic_bool started_ = false;
};

class CodecEncoderFallback final : public hal::audio::EncoderIface {
public:
    CodecEncoderFallback()
        : recorder_(hal::acquire_first_interface<hal::audio::CodecRecorderIface>())
    {}

    bool valid() const { return static_cast<bool>(recorder_); }
    std::vector<std::string> get_afe_wake_words() override { return {}; }

    bool start(const hal::audio::EncoderDynamicConfig &config, Callbacks callbacks) override
    {
        std::lock_guard lock(mutex_);
        if (!recorder_ || config.type != hal::audio::CodecFormat::PCM || config.enable_afe) return false;
        const auto &info = recorder_->get_info();
        if (config.general.sample_bits != info.bits || config.general.channels != info.channels ||
                config.general.sample_rate != info.sample_rate) return false;
        pcm_frame_bytes_ = static_cast<size_t>(config.general.frame_duration) * info.sample_rate *
                           info.channels * info.bits / 8 / 1000;
        callbacks_ = std::move(callbacks);
        paused_ = false;
        started_ = recorder_->open();
        return started_;
    }

    int read_encoded_data(uint8_t *data, size_t size) override
    {
        std::lock_guard lock(mutex_);
        const size_t read_size = std::min(size, pcm_frame_bytes_);
        if (!started_ || paused_ || !data || !read_size) return 0;
        if (!recorder_->read_data(data, read_size)) return -1;
        if (callbacks_.recorder_data) callbacks_.recorder_data(data, read_size);
        return static_cast<int>(read_size);
    }

    void stop() override
    {
        std::lock_guard lock(mutex_);
        if (started_) recorder_->close();
        callbacks_ = {};
        paused_ = false;
        started_ = false;
    }
    void pause() override { paused_ = true; }
    void resume() override { paused_ = false; }
    bool is_started() const override { return started_.load(); }
    bool is_paused() const override { return paused_.load(); }

private:
    hal::InterfaceHandle<hal::audio::CodecRecorderIface> recorder_;
    mutable std::mutex mutex_;
    Callbacks callbacks_{};
    size_t pcm_frame_bytes_ = 0;
    std::atomic_bool started_ = false;
    std::atomic_bool paused_ = false;
};

} // namespace

std::shared_ptr<hal::audio::DecoderIface> make_codec_decoder_fallback()
{
    auto result = std::make_shared<CodecDecoderFallback>();
    return result->valid() ? result : nullptr;
}

std::shared_ptr<hal::audio::EncoderIface> make_codec_encoder_fallback()
{
    auto result = std::make_shared<CodecEncoderFallback>();
    return result->valid() ? result : nullptr;
}

} // namespace esp_brookesia::service
