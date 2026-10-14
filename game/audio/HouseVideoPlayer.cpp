#include "game/audio/HouseVideoPlayer.h"

#include "engine/BgfxContext.h"
#include "game/render/TextureFiltering.h"

#include <bgfx/bgfx.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace OpenYAMM::Game
{
namespace
{
constexpr int HouseVideoAudioFrequency = 48000;
constexpr int HouseVideoAudioChannels = 2;
constexpr int HouseVideoAudioQueueTargetMilliseconds = 500;
constexpr double HouseVideoDecodeAheadSeconds = 1.0;
constexpr int HouseVideoAvioBufferSize = 64 * 1024;
constexpr const char *DefaultHouseVideoDirectory = "Videos/Houses";

bool canUseBgfxResources()
{
    return Engine::BgfxContext::isBgfxInitialized();
}

bool isAudioSubsystemInitialized()
{
    return (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0;
}

std::string makeClipKey(const std::string &videoDirectory, const std::string &videoStem)
{
    return videoDirectory + "/" + videoStem;
}

std::unique_ptr<Engine::AssetReadStream> openVideoClipStream(
    const Engine::AssetFileSystem &assetFileSystem,
    const std::string &videoStem,
    const std::string &videoDirectory)
{
    const bool allowLegacyFallbacks = videoDirectory == DefaultHouseVideoDirectory;
    const std::array<const char *, 3> candidateDirectories = {{
        videoDirectory.c_str(),
        "Anims/mightdod",
        "Videos/Transitions"
    }};
    const size_t candidateCount = allowLegacyFallbacks ? candidateDirectories.size() : 1;

    for (size_t index = 0; index < candidateCount; ++index)
    {
        const std::string virtualPath = std::string(candidateDirectories[index]) + "/" + videoStem + ".ogv";
        std::unique_ptr<Engine::AssetReadStream> pStream = assetFileSystem.openReadStream(virtualPath);

        if (pStream != nullptr && pStream->length() > 0)
        {
            return pStream;
        }
    }

    return nullptr;
}

std::string ffmpegErrorString(int errorCode)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer = {};
    av_strerror(errorCode, buffer.data(), buffer.size());
    return std::string(buffer.data());
}

struct AvFormatContextCloser
{
    void operator()(AVFormatContext *pFormatContext) const
    {
        if (pFormatContext != nullptr)
        {
            avformat_close_input(&pFormatContext);
        }
    }
};

struct AvCodecContextCloser
{
    void operator()(AVCodecContext *pCodecContext) const
    {
        if (pCodecContext != nullptr)
        {
            avcodec_free_context(&pCodecContext);
        }
    }
};

struct AvFrameCloser
{
    void operator()(AVFrame *pFrame) const
    {
        if (pFrame != nullptr)
        {
            av_frame_free(&pFrame);
        }
    }
};

struct AvPacketCloser
{
    void operator()(AVPacket *pPacket) const
    {
        if (pPacket != nullptr)
        {
            av_packet_free(&pPacket);
        }
    }
};

struct SwsContextCloser
{
    void operator()(SwsContext *pSwsContext) const
    {
        if (pSwsContext != nullptr)
        {
            sws_freeContext(pSwsContext);
        }
    }
};

struct SwrContextCloser
{
    void operator()(SwrContext *pSwrContext) const
    {
        if (pSwrContext != nullptr)
        {
            swr_free(&pSwrContext);
        }
    }
};

struct AvioContextCloser
{
    void operator()(AVIOContext *pAvioContext) const
    {
        if (pAvioContext != nullptr)
        {
            av_freep(&pAvioContext->buffer);
            avio_context_free(&pAvioContext);
        }
    }
};

using AvFormatContextPtr = std::unique_ptr<AVFormatContext, AvFormatContextCloser>;
using AvCodecContextPtr = std::unique_ptr<AVCodecContext, AvCodecContextCloser>;
using AvFramePtr = std::unique_ptr<AVFrame, AvFrameCloser>;
using AvPacketPtr = std::unique_ptr<AVPacket, AvPacketCloser>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextCloser>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextCloser>;
using AvioContextPtr = std::unique_ptr<AVIOContext, AvioContextCloser>;

AvCodecContextPtr createDecoderContext(const AVStream &stream)
{
    const AVCodec *pCodec = avcodec_find_decoder(stream.codecpar->codec_id);

    if (pCodec == nullptr)
    {
        return nullptr;
    }

    AvCodecContextPtr pCodecContext(avcodec_alloc_context3(pCodec));

    if (pCodecContext == nullptr)
    {
        return nullptr;
    }

    int result = avcodec_parameters_to_context(pCodecContext.get(), stream.codecpar);

    if (result < 0)
    {
        std::cerr << "HouseVideoPlayer: avcodec_parameters_to_context failed: "
                  << ffmpegErrorString(result) << '\n';
        return nullptr;
    }

    result = avcodec_open2(pCodecContext.get(), pCodec, nullptr);

    if (result < 0)
    {
        std::cerr << "HouseVideoPlayer: avcodec_open2 failed: " << ffmpegErrorString(result) << '\n';
        return nullptr;
    }

    return pCodecContext;
}

std::optional<std::vector<uint8_t>> convertVideoFrame(
    const AVFrame &frame,
    SwsContext &swsContext,
    int width,
    int height)
{
    if (frame.width != width || frame.height != height)
    {
        return std::nullopt;
    }

    const size_t frameSizeBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    std::vector<uint8_t> pixels(frameSizeBytes);
    uint8_t *pDestinationData[4] = {};
    int destinationLinesize[4] = {};
    const int fillResult = av_image_fill_arrays(
        pDestinationData,
        destinationLinesize,
        pixels.data(),
        AV_PIX_FMT_BGRA,
        width,
        height,
        1);

    if (fillResult < 0)
    {
        return std::nullopt;
    }

    sws_scale(
        &swsContext,
        frame.data,
        frame.linesize,
        0,
        height,
        pDestinationData,
        destinationLinesize);
    return pixels;
}

std::optional<std::vector<float>> convertAudioFrame(
    const AVFrame &frame,
    SwrContext &swrContext,
    int inputSampleRate)
{
    if (inputSampleRate <= 0)
    {
        return std::nullopt;
    }

    const int outputFrameCapacity = av_rescale_rnd(
        swr_get_delay(&swrContext, inputSampleRate) + frame.nb_samples,
        HouseVideoAudioFrequency,
        inputSampleRate,
        AV_ROUND_UP);

    if (outputFrameCapacity <= 0)
    {
        return std::vector<float>();
    }

    std::vector<float> samples(
        static_cast<size_t>(outputFrameCapacity) * static_cast<size_t>(HouseVideoAudioChannels));
    uint8_t *pOutputData[1] = {reinterpret_cast<uint8_t *>(samples.data())};
    const int convertedFrameCount = swr_convert(
        &swrContext,
        pOutputData,
        outputFrameCapacity,
        const_cast<const uint8_t **>(frame.extended_data),
        frame.nb_samples);

    if (convertedFrameCount < 0)
    {
        return std::nullopt;
    }

    samples.resize(static_cast<size_t>(convertedFrameCount) * static_cast<size_t>(HouseVideoAudioChannels));
    return samples;
}

struct StreamClipReader
{
    Engine::AssetReadStream *pStream = nullptr;
    std::atomic_bool *pStopRequested = nullptr;
};

int readStreamClip(void *pOpaque, uint8_t *pBuffer, int bufferSize)
{
    StreamClipReader *pReader = static_cast<StreamClipReader *>(pOpaque);

    if (pReader == nullptr || pReader->pStream == nullptr || pBuffer == nullptr || bufferSize <= 0)
    {
        return AVERROR(EINVAL);
    }

    if (pReader->pStopRequested != nullptr && pReader->pStopRequested->load())
    {
        return AVERROR_EXIT;
    }

    const int64_t bytesRead = pReader->pStream->read(pBuffer, static_cast<size_t>(bufferSize));

    if (bytesRead < 0)
    {
        return AVERROR(EIO);
    }

    return bytesRead == 0 ? AVERROR_EOF : static_cast<int>(bytesRead);
}

int64_t seekStreamClip(void *pOpaque, int64_t offset, int whence)
{
    StreamClipReader *pReader = static_cast<StreamClipReader *>(pOpaque);

    if (pReader == nullptr || pReader->pStream == nullptr)
    {
        return AVERROR(EINVAL);
    }

    const int64_t streamLength = pReader->pStream->length();

    if ((whence & AVSEEK_SIZE) != 0)
    {
        return streamLength >= 0 ? streamLength : AVERROR(EIO);
    }

    const int seekMode = whence & ~AVSEEK_FORCE;
    int64_t baseOffset = 0;

    switch (seekMode)
    {
        case SEEK_SET:
            break;

        case SEEK_CUR:
            baseOffset = pReader->pStream->tell();
            break;

        case SEEK_END:
            baseOffset = streamLength;
            break;

        default:
            return AVERROR(EINVAL);
    }

    if (baseOffset < 0
        || (offset > 0 && baseOffset > std::numeric_limits<int64_t>::max() - offset)
        || (offset < 0 && offset < -baseOffset))
    {
        return AVERROR(EINVAL);
    }

    const int64_t targetOffset = baseOffset + offset;

    if (targetOffset < 0 || streamLength < 0 || targetOffset > streamLength)
    {
        return AVERROR(EINVAL);
    }

    return pReader->pStream->seek(static_cast<uint64_t>(targetOffset)) ? targetOffset : AVERROR(EIO);
}

enum class DecoderDrainResult
{
    NeedsInput,
    EndOfStream,
    Failed,
    Stopped
};

template <typename FrameHandler>
DecoderDrainResult drainDecoder(
    AVCodecContext &codecContext,
    AVFrame &frame,
    FrameHandler &frameHandler,
    const std::atomic_bool &stopRequested,
    const std::string &virtualPath,
    bool *pProducedFrame = nullptr)
{
    while (!stopRequested.load())
    {
        const int result = avcodec_receive_frame(&codecContext, &frame);

        if (result == AVERROR(EAGAIN))
        {
            return DecoderDrainResult::NeedsInput;
        }

        if (result == AVERROR_EOF)
        {
            return DecoderDrainResult::EndOfStream;
        }

        if (result < 0)
        {
            std::cerr << "HouseVideoPlayer: avcodec_receive_frame failed for " << virtualPath
                      << ": " << ffmpegErrorString(result) << '\n';
            return DecoderDrainResult::Failed;
        }

        const bool handled = frameHandler(frame);
        av_frame_unref(&frame);

        if (pProducedFrame != nullptr)
        {
            *pProducedFrame = true;
        }

        if (!handled)
        {
            return stopRequested.load() ? DecoderDrainResult::Stopped : DecoderDrainResult::Failed;
        }
    }

    return DecoderDrainResult::Stopped;
}

template <typename FrameHandler>
bool submitDecoderPacket(
    AVCodecContext &codecContext,
    const AVPacket *pPacket,
    AVFrame &frame,
    FrameHandler &frameHandler,
    const std::atomic_bool &stopRequested,
    const std::string &virtualPath)
{
    if (pPacket != nullptr && pPacket->size <= 0)
    {
        return true;
    }

    while (!stopRequested.load())
    {
        const int sendResult = avcodec_send_packet(&codecContext, pPacket);

        if (sendResult == 0)
        {
            break;
        }

        if (sendResult == AVERROR_EOF && pPacket == nullptr)
        {
            return true;
        }

        if (sendResult != AVERROR(EAGAIN))
        {
            std::cerr << "HouseVideoPlayer: avcodec_send_packet failed for " << virtualPath
                      << ": " << ffmpegErrorString(sendResult) << '\n';
            return false;
        }

        bool producedFrame = false;
        const DecoderDrainResult drainResult = drainDecoder(
            codecContext,
            frame,
            frameHandler,
            stopRequested,
            virtualPath,
            &producedFrame);

        if (drainResult == DecoderDrainResult::Failed || drainResult == DecoderDrainResult::Stopped)
        {
            return false;
        }

        if (drainResult == DecoderDrainResult::NeedsInput && !producedFrame)
        {
            std::cerr << "HouseVideoPlayer: decoder made no progress for " << virtualPath << '\n';
            return false;
        }
    }

    if (stopRequested.load())
    {
        return false;
    }

    const DecoderDrainResult drainResult = drainDecoder(
        codecContext,
        frame,
        frameHandler,
        stopRequested,
        virtualPath);
    return drainResult != DecoderDrainResult::Failed && drainResult != DecoderDrainResult::Stopped;
}

struct DecodedVideoFrame
{
    double presentationSeconds = 0.0;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

struct DecodedAudioChunk
{
    std::vector<float> samples;
};
}

struct HouseVideoPlayer::StreamingSession
{
    StreamingSession(
        std::unique_ptr<Engine::AssetReadStream> pInputStreamValue,
        bool loopPlaybackValue)
        : virtualPath(pInputStreamValue != nullptr ? pInputStreamValue->virtualPath() : std::string())
        , pInputStream(std::move(pInputStreamValue))
        , loopPlayback(loopPlaybackValue)
    {
    }

    ~StreamingSession()
    {
        stop();
    }

    void start()
    {
        worker = std::thread([this]()
        {
            run();
        });
    }

    void stop()
    {
        // Under the queue's lock: the worker checks the flag under it just before it sleeps, and a flag set in
        // between (with its wake-up) would leave it asleep for good, and stop() waiting on it (skipping a cutscene
        // hung the game).
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            stopRequested.store(true);
        }

        if (worker.joinable())
        {
            queueChanged.notify_all();
            worker.join();
        }
    }

    bool enqueueVideoFrame(DecodedVideoFrame frame, double frameEndSeconds)
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        endTimeSeconds = std::max(endTimeSeconds, frameEndSeconds);
        decodedVideoEndSeconds = std::max(decodedVideoEndSeconds, frameEndSeconds);
        videoFrames.push_back(std::move(frame));
        queueChanged.notify_all();
        return !stopRequested.load();
    }

    bool enqueueAudioChunk(
        DecodedAudioChunk chunk,
        size_t frameCount,
        double chunkEndSeconds)
    {
        if (frameCount == 0)
        {
            return true;
        }

        std::lock_guard<std::mutex> lock(queueMutex);
        bufferedAudioFrames += frameCount;
        endTimeSeconds = std::max(endTimeSeconds, chunkEndSeconds);
        decodedAudioEndSeconds = std::max(decodedAudioEndSeconds, chunkEndSeconds);
        audioChunks.push_back(std::move(chunk));
        queueChanged.notify_all();
        return !stopRequested.load();
    }

    bool waitForDecodeWindow()
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueChanged.wait(lock, [this]()
        {
            if (stopRequested.load())
            {
                return true;
            }

            const double decodeLimitSeconds = playbackPositionSeconds + HouseVideoDecodeAheadSeconds;
            const bool videoBufferedAhead = decodedVideoEndSeconds >= decodeLimitSeconds;
            const bool audioBufferedAhead = !hasAudio || decodedAudioEndSeconds >= decodeLimitSeconds;
            return !videoBufferedAhead || !audioBufferedAhead;
        });
        return !stopRequested.load();
    }

    void setPlaybackPosition(double playbackSeconds)
    {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            playbackPositionSeconds = std::max(playbackPositionSeconds, playbackSeconds);
        }

        queueChanged.notify_all();
    }

    void fail()
    {
        if (stopRequested.load())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(queueMutex);
        failed = true;
        endOfStream = true;
        queueChanged.notify_all();
    }

    void run()
    {
        if (pInputStream == nullptr)
        {
            fail();
            return;
        }

        StreamClipReader streamReader = {pInputStream.get(), &stopRequested};
        uint8_t *pAvioBuffer = static_cast<uint8_t *>(av_malloc(HouseVideoAvioBufferSize));

        if (pAvioBuffer == nullptr)
        {
            std::cerr << "HouseVideoPlayer: av_malloc failed for " << virtualPath << '\n';
            fail();
            return;
        }

        AvioContextPtr pAvioContext(avio_alloc_context(
            pAvioBuffer,
            HouseVideoAvioBufferSize,
            0,
            &streamReader,
            &readStreamClip,
            nullptr,
            &seekStreamClip));

        if (pAvioContext == nullptr)
        {
            av_free(pAvioBuffer);
            std::cerr << "HouseVideoPlayer: avio_alloc_context failed for " << virtualPath << '\n';
            fail();
            return;
        }

        AVFormatContext *pRawFormatContext = avformat_alloc_context();

        if (pRawFormatContext == nullptr)
        {
            std::cerr << "HouseVideoPlayer: avformat_alloc_context failed for " << virtualPath << '\n';
            fail();
            return;
        }

        pRawFormatContext->pb = pAvioContext.get();
        pRawFormatContext->flags |= AVFMT_FLAG_CUSTOM_IO;
        pRawFormatContext->interrupt_callback.callback = [](void *pOpaque)
        {
            const StreamingSession *pSession = static_cast<const StreamingSession *>(pOpaque);
            return pSession != nullptr && pSession->stopRequested.load() ? 1 : 0;
        };
        pRawFormatContext->interrupt_callback.opaque = this;

        int result = avformat_open_input(&pRawFormatContext, virtualPath.c_str(), nullptr, nullptr);

        if (result < 0 || pRawFormatContext == nullptr)
        {
            if (pRawFormatContext != nullptr)
            {
                avformat_free_context(pRawFormatContext);
            }

            if (!stopRequested.load())
            {
                std::cerr << "HouseVideoPlayer: avformat_open_input failed for " << virtualPath
                          << ": " << ffmpegErrorString(result) << '\n';
                fail();
            }
            return;
        }

        AvFormatContextPtr pFormatContext(pRawFormatContext);
        result = avformat_find_stream_info(pFormatContext.get(), nullptr);

        if (result < 0)
        {
            if (!stopRequested.load())
            {
                std::cerr << "HouseVideoPlayer: avformat_find_stream_info failed for " << virtualPath
                          << ": " << ffmpegErrorString(result) << '\n';
                fail();
            }
            return;
        }

        const int videoStreamIndex = av_find_best_stream(
            pFormatContext.get(),
            AVMEDIA_TYPE_VIDEO,
            -1,
            -1,
            nullptr,
            0);

        if (videoStreamIndex < 0)
        {
            std::cerr << "HouseVideoPlayer: no video stream found for " << virtualPath << '\n';
            fail();
            return;
        }

        AVStream &videoStream = *pFormatContext->streams[videoStreamIndex];
        AvCodecContextPtr pVideoCodecContext = createDecoderContext(videoStream);

        if (pVideoCodecContext == nullptr || pVideoCodecContext->width <= 0 || pVideoCodecContext->height <= 0)
        {
            std::cerr << "HouseVideoPlayer: failed to create video decoder for " << virtualPath << '\n';
            fail();
            return;
        }

        const int videoWidth = pVideoCodecContext->width;
        const int videoHeight = pVideoCodecContext->height;
        SwsContextPtr pSwsContext(sws_getContext(
            videoWidth,
            videoHeight,
            pVideoCodecContext->pix_fmt,
            videoWidth,
            videoHeight,
            AV_PIX_FMT_BGRA,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr));

        if (pSwsContext == nullptr)
        {
            std::cerr << "HouseVideoPlayer: sws_getContext failed for " << virtualPath << '\n';
            fail();
            return;
        }

        const int audioStreamIndex = av_find_best_stream(
            pFormatContext.get(),
            AVMEDIA_TYPE_AUDIO,
            -1,
            -1,
            nullptr,
            0);
        AvCodecContextPtr pAudioCodecContext;
        SwrContextPtr pSwrContext;

        if (audioStreamIndex >= 0)
        {
            pAudioCodecContext = createDecoderContext(*pFormatContext->streams[audioStreamIndex]);

            if (pAudioCodecContext != nullptr && pAudioCodecContext->sample_rate > 0)
            {
                AVChannelLayout inputChannelLayout = {};

                if (av_channel_layout_check(&pAudioCodecContext->ch_layout) > 0)
                {
                    result = av_channel_layout_copy(&inputChannelLayout, &pAudioCodecContext->ch_layout);
                }
                else
                {
                    av_channel_layout_default(
                        &inputChannelLayout,
                        std::max(1, pAudioCodecContext->ch_layout.nb_channels));
                    result = 0;
                }

                if (result >= 0)
                {
                    AVChannelLayout outputChannelLayout = AV_CHANNEL_LAYOUT_STEREO;
                    SwrContext *pRawSwrContext = nullptr;
                    result = swr_alloc_set_opts2(
                        &pRawSwrContext,
                        &outputChannelLayout,
                        AV_SAMPLE_FMT_FLT,
                        HouseVideoAudioFrequency,
                        &inputChannelLayout,
                        pAudioCodecContext->sample_fmt,
                        pAudioCodecContext->sample_rate,
                        0,
                        nullptr);

                    if (result >= 0 && pRawSwrContext != nullptr)
                    {
                        result = swr_init(pRawSwrContext);

                        if (result >= 0)
                        {
                            pSwrContext.reset(pRawSwrContext);
                        }
                        else
                        {
                            swr_free(&pRawSwrContext);
                        }
                    }

                    av_channel_layout_uninit(&inputChannelLayout);
                }

                if (result < 0)
                {
                    std::cerr << "HouseVideoPlayer: audio resampler setup failed for " << virtualPath
                              << ": " << ffmpegErrorString(result) << '\n';
                    pAudioCodecContext.reset();
                    pSwrContext.reset();
                }
            }
            else
            {
                pAudioCodecContext.reset();
            }
        }

        const bool streamHasAudio = pAudioCodecContext != nullptr && pSwrContext != nullptr;
        double containerDurationSeconds = 0.0;

        if (pFormatContext->duration > 0)
        {
            containerDurationSeconds = static_cast<double>(pFormatContext->duration) / AV_TIME_BASE;
        }
        else if (videoStream.duration > 0)
        {
            containerDurationSeconds = videoStream.duration * av_q2d(videoStream.time_base);
        }

        AVRational guessedFrameRate = av_guess_frame_rate(pFormatContext.get(), &videoStream, nullptr);
        double fallbackFrameDuration = 1.0 / 15.0;

        if (guessedFrameRate.num > 0 && guessedFrameRate.den > 0)
        {
            fallbackFrameDuration = av_q2d(av_inv_q(guessedFrameRate));
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            hasAudio = streamHasAudio;
            endTimeSeconds = std::max(0.0, containerDurationSeconds);
            queueChanged.notify_all();
        }

        AvPacketPtr pPacket(av_packet_alloc());
        AvFramePtr pFrame(av_frame_alloc());

        if (pPacket == nullptr || pFrame == nullptr)
        {
            std::cerr << "HouseVideoPlayer: failed to allocate decode buffers for " << virtualPath << '\n';
            fail();
            return;
        }

        double loopOffsetSeconds = 0.0;
        std::optional<double> firstVideoTimestampSeconds;
        double nextFallbackVideoTimestampSeconds = 0.0;
        double iterationVideoEndSeconds = 0.0;
        size_t iterationAudioFrameCount = 0;

        while (!stopRequested.load())
        {
            const auto handleVideoFrame = [&](const AVFrame &frame)
            {
                std::optional<std::vector<uint8_t>> pixels = convertVideoFrame(
                    frame,
                    *pSwsContext,
                    videoWidth,
                    videoHeight);

                if (!pixels)
                {
                    std::cerr << "HouseVideoPlayer: failed to convert video frame for " << virtualPath << '\n';
                    return false;
                }

                double frameTimestampSeconds = nextFallbackVideoTimestampSeconds;

                if (frame.best_effort_timestamp != AV_NOPTS_VALUE)
                {
                    const double rawTimestampSeconds =
                        frame.best_effort_timestamp * av_q2d(videoStream.time_base);

                    if (!firstVideoTimestampSeconds)
                    {
                        firstVideoTimestampSeconds = rawTimestampSeconds;
                    }

                    frameTimestampSeconds = loopOffsetSeconds
                        + std::max(0.0, rawTimestampSeconds - *firstVideoTimestampSeconds);
                }

                double frameDurationSeconds = fallbackFrameDuration;

                if (frame.duration > 0)
                {
                    frameDurationSeconds = frame.duration * av_q2d(videoStream.time_base);
                }

                if (!std::isfinite(frameDurationSeconds) || frameDurationSeconds <= 0.0)
                {
                    frameDurationSeconds = fallbackFrameDuration;
                }

                nextFallbackVideoTimestampSeconds = frameTimestampSeconds + frameDurationSeconds;
                iterationVideoEndSeconds = std::max(
                    iterationVideoEndSeconds,
                    nextFallbackVideoTimestampSeconds);
                DecodedVideoFrame decodedFrame;
                decodedFrame.presentationSeconds = frameTimestampSeconds;
                decodedFrame.width = videoWidth;
                decodedFrame.height = videoHeight;
                decodedFrame.pixels = std::move(*pixels);
                return enqueueVideoFrame(
                    std::move(decodedFrame),
                    nextFallbackVideoTimestampSeconds);
            };

            const auto handleAudioFrame = [&](const AVFrame &frame)
            {
                std::optional<std::vector<float>> samples = convertAudioFrame(
                    frame,
                    *pSwrContext,
                    pAudioCodecContext->sample_rate);

                if (!samples)
                {
                    std::cerr << "HouseVideoPlayer: failed to convert audio frame for " << virtualPath << '\n';
                    return false;
                }

                const size_t convertedFrameCount = samples->size() / HouseVideoAudioChannels;
                iterationAudioFrameCount += convertedFrameCount;
                const double audioEndSeconds = loopOffsetSeconds
                    + static_cast<double>(iterationAudioFrameCount) / HouseVideoAudioFrequency;
                return enqueueAudioChunk(
                    DecodedAudioChunk{std::move(*samples)},
                    convertedFrameCount,
                    audioEndSeconds);
            };

            while (!stopRequested.load()
                && (result = av_read_frame(pFormatContext.get(), pPacket.get())) >= 0)
            {
                bool decoded = true;

                if (pPacket->stream_index == videoStreamIndex)
                {
                    decoded = submitDecoderPacket(
                        *pVideoCodecContext,
                        pPacket.get(),
                        *pFrame,
                        handleVideoFrame,
                        stopRequested,
                        virtualPath);
                }
                else if (streamHasAudio && pPacket->stream_index == audioStreamIndex)
                {
                    decoded = submitDecoderPacket(
                        *pAudioCodecContext,
                        pPacket.get(),
                        *pFrame,
                        handleAudioFrame,
                        stopRequested,
                        virtualPath);
                }

                av_packet_unref(pPacket.get());

                if (!decoded)
                {
                    if (!stopRequested.load())
                    {
                        fail();
                    }
                    return;
                }

                if (!waitForDecodeWindow())
                {
                    return;
                }
            }

            if (stopRequested.load())
            {
                return;
            }

            if (result != AVERROR_EOF)
            {
                std::cerr << "HouseVideoPlayer: av_read_frame failed for " << virtualPath
                          << ": " << ffmpegErrorString(result) << '\n';
                fail();
                return;
            }

            if (!submitDecoderPacket(
                    *pVideoCodecContext,
                    nullptr,
                    *pFrame,
                    handleVideoFrame,
                    stopRequested,
                    virtualPath))
            {
                if (!stopRequested.load())
                {
                    fail();
                }
                return;
            }

            if (streamHasAudio
                && !submitDecoderPacket(
                    *pAudioCodecContext,
                    nullptr,
                    *pFrame,
                    handleAudioFrame,
                    stopRequested,
                    virtualPath))
            {
                if (!stopRequested.load())
                {
                    fail();
                }
                return;
            }

            if (streamHasAudio)
            {
                while (!stopRequested.load())
                {
                    const int delayFrames = av_rescale_rnd(
                        swr_get_delay(pSwrContext.get(), pAudioCodecContext->sample_rate),
                        HouseVideoAudioFrequency,
                        pAudioCodecContext->sample_rate,
                        AV_ROUND_UP);

                    if (delayFrames <= 0)
                    {
                        break;
                    }

                    std::vector<float> samples(
                        static_cast<size_t>(delayFrames) * static_cast<size_t>(HouseVideoAudioChannels));
                    uint8_t *pOutputData[1] = {reinterpret_cast<uint8_t *>(samples.data())};
                    const int convertedFrameCount = swr_convert(
                        pSwrContext.get(),
                        pOutputData,
                        delayFrames,
                        nullptr,
                        0);

                    if (convertedFrameCount < 0)
                    {
                        std::cerr << "HouseVideoPlayer: audio resampler flush failed for " << virtualPath
                                  << ": " << ffmpegErrorString(convertedFrameCount) << '\n';
                        fail();
                        return;
                    }

                    if (convertedFrameCount == 0)
                    {
                        break;
                    }

                    samples.resize(
                        static_cast<size_t>(convertedFrameCount) * static_cast<size_t>(HouseVideoAudioChannels));
                    iterationAudioFrameCount += static_cast<size_t>(convertedFrameCount);
                    const double audioEndSeconds = loopOffsetSeconds
                        + static_cast<double>(iterationAudioFrameCount) / HouseVideoAudioFrequency;

                    if (!enqueueAudioChunk(
                            DecodedAudioChunk{std::move(samples)},
                            static_cast<size_t>(convertedFrameCount),
                            audioEndSeconds))
                    {
                        return;
                    }
                }
            }

            const double iterationAudioDuration =
                static_cast<double>(iterationAudioFrameCount) / HouseVideoAudioFrequency;
            const double iterationVideoDuration = std::max(0.0, iterationVideoEndSeconds - loopOffsetSeconds);
            const double iterationDuration = std::max({
                containerDurationSeconds,
                iterationAudioDuration,
                iterationVideoDuration,
                fallbackFrameDuration
            });

            {
                std::lock_guard<std::mutex> lock(queueMutex);
                endTimeSeconds = std::max(endTimeSeconds, loopOffsetSeconds + iterationDuration);

                if (!loopPlayback)
                {
                    endOfStream = true;
                    queueChanged.notify_all();
                    return;
                }
            }

            result = avformat_seek_file(
                pFormatContext.get(),
                -1,
                std::numeric_limits<int64_t>::min(),
                0,
                std::numeric_limits<int64_t>::max(),
                AVSEEK_FLAG_BACKWARD);

            if (result < 0)
            {
                result = av_seek_frame(pFormatContext.get(), -1, 0, AVSEEK_FLAG_BACKWARD);
            }

            if (result < 0)
            {
                std::cerr << "HouseVideoPlayer: loop seek failed for " << virtualPath
                          << ": " << ffmpegErrorString(result) << '\n';
                fail();
                return;
            }

            avcodec_flush_buffers(pVideoCodecContext.get());

            if (streamHasAudio)
            {
                avcodec_flush_buffers(pAudioCodecContext.get());
                swr_close(pSwrContext.get());
                result = swr_init(pSwrContext.get());

                if (result < 0)
                {
                    std::cerr << "HouseVideoPlayer: audio resampler reset failed for " << virtualPath
                              << ": " << ffmpegErrorString(result) << '\n';
                    fail();
                    return;
                }
            }

            loopOffsetSeconds += iterationDuration;
            firstVideoTimestampSeconds.reset();
            nextFallbackVideoTimestampSeconds = loopOffsetSeconds;
            iterationVideoEndSeconds = loopOffsetSeconds;
            iterationAudioFrameCount = 0;
        }
    }

    std::string virtualPath;
    std::unique_ptr<Engine::AssetReadStream> pInputStream;
    bool loopPlayback = true;
    std::mutex queueMutex;
    std::condition_variable queueChanged;
    std::deque<DecodedVideoFrame> videoFrames;
    std::deque<DecodedAudioChunk> audioChunks;
    size_t bufferedAudioFrames = 0;
    bool hasAudio = false;
    bool endOfStream = false;
    bool failed = false;
    double endTimeSeconds = 0.0;
    double decodedVideoEndSeconds = 0.0;
    double decodedAudioEndSeconds = 0.0;
    double playbackPositionSeconds = 0.0;
    std::atomic_bool stopRequested = false;
    std::thread worker;
};

HouseVideoPlayer::HouseVideoPlayer()
    : m_isInitialized(false)
    , m_initializedAudioSubsystem(false)
    , m_videoTextureHandle(BGFX_INVALID_HANDLE)
    , m_videoTextureWidth(0)
    , m_videoTextureHeight(0)
    , m_pAudioStream(nullptr)
    , m_pStreamingSession(nullptr)
    , m_playbackSeconds(0.0f)
    , m_totalQueuedAudioFrames(0)
    , m_loopPlayback(true)
    , m_playbackStarted(false)
    , m_hasActiveFrame(false)
    , m_finishedPlayback(false)
    , m_presentedFrameSeconds(0.0)
    , m_presentedFrameSerial(0)
    , m_audioVolume(1.0f)
{
}

HouseVideoPlayer::~HouseVideoPlayer()
{
    shutdown();
}

bool HouseVideoPlayer::initialize()
{
    if (m_isInitialized)
    {
        return true;
    }

    if (!isAudioSubsystemInitialized())
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            std::cerr << "HouseVideoPlayer: SDL_InitSubSystem(SDL_INIT_AUDIO) failed: " << SDL_GetError() << '\n';
            m_isInitialized = true;
            return false;
        }

        m_initializedAudioSubsystem = true;
    }

    m_isInitialized = true;
    return true;
}

void HouseVideoPlayer::shutdown()
{
    stop();

    if (m_pAudioStream != nullptr)
    {
        if (isAudioSubsystemInitialized())
        {
            SDL_DestroyAudioStream(m_pAudioStream);
        }

        m_pAudioStream = nullptr;
    }

    if (canUseBgfxResources() && bgfx::isValid(m_videoTextureHandle))
    {
        bgfx::destroy(m_videoTextureHandle);
    }

    m_videoTextureHandle = BGFX_INVALID_HANDLE;
    m_videoTextureWidth = 0;
    m_videoTextureHeight = 0;

    if (m_initializedAudioSubsystem)
    {
        if (isAudioSubsystemInitialized())
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }

        m_initializedAudioSubsystem = false;
    }

    m_isInitialized = false;
}

void HouseVideoPlayer::stop()
{
    if (m_pStreamingSession == nullptr && m_activeClipKey.empty() && !m_hasActiveFrame)
    {
        return;
    }

    if (m_pStreamingSession != nullptr)
    {
        m_pStreamingSession->stop();
        m_pStreamingSession.reset();
    }

    m_activeClipKey.clear();
    m_playbackSeconds = 0.0f;
    m_totalQueuedAudioFrames = 0;
    m_playbackStarted = false;
    m_hasActiveFrame = false;
    m_finishedPlayback = false;
    m_presentedFrameSeconds = 0.0;
    m_presentedFrameSerial = 0;

    if (m_pAudioStream != nullptr && isAudioSubsystemInitialized())
    {
        SDL_ClearAudioStream(m_pAudioStream);
    }

    if (canUseBgfxResources() && bgfx::isValid(m_videoTextureHandle))
    {
        bgfx::destroy(m_videoTextureHandle);
    }

    m_videoTextureHandle = BGFX_INVALID_HANDLE;
    m_videoTextureWidth = 0;
    m_videoTextureHeight = 0;
}

bool HouseVideoPlayer::play(const Engine::AssetFileSystem &assetFileSystem, const std::string &videoStem)
{
    return play(assetFileSystem, videoStem, DefaultHouseVideoDirectory);
}

bool HouseVideoPlayer::play(
    const Engine::AssetFileSystem &assetFileSystem,
    const std::string &videoStem,
    const std::string &videoDirectory)
{
    return play(assetFileSystem, videoStem, videoDirectory, true);
}

bool HouseVideoPlayer::play(
    const Engine::AssetFileSystem &assetFileSystem,
    const std::string &videoStem,
    const std::string &videoDirectory,
    bool loopPlayback)
{
    if (videoStem.empty())
    {
        stop();
        return false;
    }

    if (!m_isInitialized)
    {
        initialize();
    }

    const std::string clipKey = makeClipKey(videoDirectory, videoStem);

    if (m_pStreamingSession != nullptr
        && m_activeClipKey == clipKey
        && m_loopPlayback == loopPlayback)
    {
        std::lock_guard<std::mutex> lock(m_pStreamingSession->queueMutex);
        return !m_pStreamingSession->failed;
    }

    stop();
    std::unique_ptr<Engine::AssetReadStream> pStream = openVideoClipStream(
        assetFileSystem,
        videoStem,
        videoDirectory);

    if (pStream == nullptr)
    {
        std::cerr << "HouseVideoPlayer: missing clip for stem " << videoStem << '\n';
        return false;
    }

    m_activeClipKey = clipKey;
    m_loopPlayback = loopPlayback;
    m_pStreamingSession = std::make_unique<StreamingSession>(std::move(pStream), loopPlayback);
    m_pStreamingSession->start();
    return true;
}

void HouseVideoPlayer::setAudioVolume(float volume)
{
    m_audioVolume = std::clamp(volume, 0.0f, 1.0f);

    if (m_pAudioStream != nullptr && !SDL_SetAudioStreamGain(m_pAudioStream, m_audioVolume))
    {
        std::cerr << "HouseVideoPlayer: SDL_SetAudioStreamGain failed: " << SDL_GetError() << '\n';
    }
}

void HouseVideoPlayer::update(float deltaSeconds)
{
    if (m_pStreamingSession == nullptr)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_pStreamingSession->queueMutex);

        if (m_pStreamingSession->failed)
        {
            m_finishedPlayback = true;
            return;
        }
    }

    if (!m_playbackStarted)
    {
        if (!uploadReadyVideoFrame(true))
        {
            updateFinishedPlaybackState(deltaSeconds);
            return;
        }

        m_playbackStarted = true;
        m_playbackSeconds = 0.0f;
        updateAudioQueue();
        m_pStreamingSession->setPlaybackPosition(m_playbackSeconds);
        updateFinishedPlaybackState(0.0f);
        return;
    }

    updateAudioQueue();
    const std::optional<float> audioPlaybackSeconds = playbackSecondsFromAudioQueue();

    if (audioPlaybackSeconds)
    {
        m_playbackSeconds = std::max(m_playbackSeconds, *audioPlaybackSeconds);
    }
    else
    {
        m_playbackSeconds += std::max(0.0f, deltaSeconds);
    }

    m_pStreamingSession->setPlaybackPosition(m_playbackSeconds);
    uploadReadyVideoFrame(false);
    updateFinishedPlaybackState(deltaSeconds);
}

bool HouseVideoPlayer::hasActiveFrame() const
{
    return m_hasActiveFrame && bgfx::isValid(m_videoTextureHandle);
}

bool HouseVideoPlayer::hasFinishedPlayback() const
{
    return m_finishedPlayback;
}

bgfx::TextureHandle HouseVideoPlayer::textureHandle() const
{
    return m_videoTextureHandle;
}

int HouseVideoPlayer::videoTextureWidth() const
{
    return m_videoTextureWidth;
}

int HouseVideoPlayer::videoTextureHeight() const
{
    return m_videoTextureHeight;
}

bool HouseVideoPlayer::ensureAudioStream()
{
    if (m_pAudioStream != nullptr)
    {
        return true;
    }

    if (!isAudioSubsystemInitialized())
    {
        return false;
    }

    SDL_AudioSpec desiredSpec = {};
    desiredSpec.format = SDL_AUDIO_F32;
    desiredSpec.channels = HouseVideoAudioChannels;
    desiredSpec.freq = HouseVideoAudioFrequency;
    m_pAudioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desiredSpec, nullptr, nullptr);

    if (m_pAudioStream == nullptr)
    {
        std::cerr << "HouseVideoPlayer: SDL_OpenAudioDeviceStream failed: " << SDL_GetError() << '\n';
        return false;
    }

    if (!SDL_SetAudioStreamGain(m_pAudioStream, m_audioVolume))
    {
        std::cerr << "HouseVideoPlayer: SDL_SetAudioStreamGain failed: " << SDL_GetError() << '\n';
    }

    if (!SDL_ResumeAudioStreamDevice(m_pAudioStream))
    {
        std::cerr << "HouseVideoPlayer: SDL_ResumeAudioStreamDevice failed: " << SDL_GetError() << '\n';
        SDL_DestroyAudioStream(m_pAudioStream);
        m_pAudioStream = nullptr;
        return false;
    }

    return true;
}

void HouseVideoPlayer::ensureVideoTexture(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    if (!canUseBgfxResources())
    {
        m_videoTextureHandle = BGFX_INVALID_HANDLE;
        m_videoTextureWidth = 0;
        m_videoTextureHeight = 0;
        return;
    }

    if (bgfx::isValid(m_videoTextureHandle) && m_videoTextureWidth == width && m_videoTextureHeight == height)
    {
        return;
    }

    if (bgfx::isValid(m_videoTextureHandle))
    {
        bgfx::destroy(m_videoTextureHandle);
        m_videoTextureHandle = BGFX_INVALID_HANDLE;
    }

    m_videoTextureHandle = createEmptyBgraTexture2D(
        static_cast<uint16_t>(width),
        static_cast<uint16_t>(height),
        TextureFilterProfile::Ui,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_TEXTURE_BLIT_DST);
    m_videoTextureWidth = width;
    m_videoTextureHeight = height;
}

bool HouseVideoPlayer::uploadReadyVideoFrame(bool firstFrame)
{
    if (m_pStreamingSession == nullptr)
    {
        return false;
    }

    std::optional<DecodedVideoFrame> selectedFrame;

    {
        std::lock_guard<std::mutex> lock(m_pStreamingSession->queueMutex);

        if (firstFrame && !m_pStreamingSession->videoFrames.empty())
        {
            selectedFrame = std::move(m_pStreamingSession->videoFrames.front());
            m_pStreamingSession->videoFrames.pop_front();
        }
        else
        {
            while (!m_pStreamingSession->videoFrames.empty()
                && m_pStreamingSession->videoFrames.front().presentationSeconds
                    <= static_cast<double>(m_playbackSeconds) + 0.001)
            {
                selectedFrame = std::move(m_pStreamingSession->videoFrames.front());
                m_pStreamingSession->videoFrames.pop_front();
            }
        }
    }

    if (!selectedFrame)
    {
        return false;
    }

    m_presentedFrameSeconds = selectedFrame->presentationSeconds;
    ++m_presentedFrameSerial;
    m_pStreamingSession->queueChanged.notify_all();
    ensureVideoTexture(selectedFrame->width, selectedFrame->height);

    if (bgfx::isValid(m_videoTextureHandle))
    {
        const bgfx::Memory *pMemory = copyBgraTextureUploadMemory(
            selectedFrame->pixels.data(),
            static_cast<uint32_t>(selectedFrame->pixels.size()));
        bgfx::updateTexture2D(
            m_videoTextureHandle,
            0,
            0,
            0,
            0,
            static_cast<uint16_t>(selectedFrame->width),
            static_cast<uint16_t>(selectedFrame->height),
            pMemory);
        m_hasActiveFrame = true;
    }

    return true;
}

void HouseVideoPlayer::updateAudioQueue()
{
    if (m_pStreamingSession == nullptr)
    {
        return;
    }

    bool hasAudio = false;

    {
        std::lock_guard<std::mutex> lock(m_pStreamingSession->queueMutex);
        hasAudio = m_pStreamingSession->hasAudio;
    }

    if (!hasAudio)
    {
        return;
    }

    if (!ensureAudioStream())
    {
        std::lock_guard<std::mutex> lock(m_pStreamingSession->queueMutex);
        m_pStreamingSession->audioChunks.clear();
        m_pStreamingSession->bufferedAudioFrames = 0;
        m_pStreamingSession->queueChanged.notify_all();
        return;
    }

    constexpr int BytesPerAudioFrame = HouseVideoAudioChannels * static_cast<int>(sizeof(float));
    constexpr int TargetQueuedBytes =
        (HouseVideoAudioFrequency * BytesPerAudioFrame * HouseVideoAudioQueueTargetMilliseconds) / 1000;
    int queuedBytes = SDL_GetAudioStreamQueued(m_pAudioStream);

    while (queuedBytes >= 0 && queuedBytes < TargetQueuedBytes)
    {
        DecodedAudioChunk chunk;
        size_t chunkFrameCount = 0;

        {
            std::lock_guard<std::mutex> lock(m_pStreamingSession->queueMutex);

            if (m_pStreamingSession->audioChunks.empty())
            {
                break;
            }

            chunk = std::move(m_pStreamingSession->audioChunks.front());
            m_pStreamingSession->audioChunks.pop_front();
            chunkFrameCount = chunk.samples.size() / HouseVideoAudioChannels;
            m_pStreamingSession->bufferedAudioFrames -= std::min(
                m_pStreamingSession->bufferedAudioFrames,
                chunkFrameCount);
        }

        m_pStreamingSession->queueChanged.notify_all();
        const size_t chunkBytes = chunk.samples.size() * sizeof(float);

        if (!SDL_PutAudioStreamData(m_pAudioStream, chunk.samples.data(), static_cast<int>(chunkBytes)))
        {
            std::cerr << "HouseVideoPlayer: SDL_PutAudioStreamData failed: " << SDL_GetError() << '\n';
            break;
        }

        queuedBytes += static_cast<int>(chunkBytes);
        m_totalQueuedAudioFrames += chunkFrameCount;
    }
}

std::optional<float> HouseVideoPlayer::playbackSecondsFromAudioQueue() const
{
    if (m_pAudioStream == nullptr || m_totalQueuedAudioFrames == 0)
    {
        return std::nullopt;
    }

    const int queuedBytes = SDL_GetAudioStreamQueued(m_pAudioStream);

    if (queuedBytes <= 0)
    {
        return std::nullopt;
    }

    constexpr uint64_t BytesPerAudioFrame = HouseVideoAudioChannels * sizeof(float);
    const uint64_t queuedAudioFrames = static_cast<uint64_t>(queuedBytes) / BytesPerAudioFrame;
    const uint64_t playedAudioFrames = m_totalQueuedAudioFrames > queuedAudioFrames
        ? m_totalQueuedAudioFrames - queuedAudioFrames
        : 0;
    return static_cast<float>(playedAudioFrames) / HouseVideoAudioFrequency;
}

void HouseVideoPlayer::updateFinishedPlaybackState(float deltaSeconds)
{
    if (m_pStreamingSession == nullptr || m_loopPlayback)
    {
        return;
    }

    bool failed = false;
    bool endOfStream = false;
    bool queuesEmpty = false;
    bool hasAudio = false;
    double endTimeSeconds = 0.0;

    {
        std::lock_guard<std::mutex> lock(m_pStreamingSession->queueMutex);
        failed = m_pStreamingSession->failed;
        endOfStream = m_pStreamingSession->endOfStream;
        queuesEmpty = m_pStreamingSession->videoFrames.empty()
            && m_pStreamingSession->audioChunks.empty();
        hasAudio = m_pStreamingSession->hasAudio;
        endTimeSeconds = m_pStreamingSession->endTimeSeconds;
    }

    if (failed)
    {
        m_finishedPlayback = true;
        return;
    }

    if (!endOfStream || !queuesEmpty)
    {
        return;
    }

    int queuedAudioBytes = 0;

    if (hasAudio && m_pAudioStream != nullptr)
    {
        queuedAudioBytes = std::max(0, SDL_GetAudioStreamQueued(m_pAudioStream));
    }

    if (queuedAudioBytes > 0)
    {
        return;
    }

    if (m_playbackStarted && m_playbackSeconds < endTimeSeconds)
    {
        m_playbackSeconds += std::max(0.0f, deltaSeconds);
    }

    m_finishedPlayback = !m_playbackStarted
        || m_playbackSeconds + 0.001 >= endTimeSeconds;
}
}
