#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <module.h>
#include <core.h>
#include <gui/colormaps.h>
#include <gui/widgets/menu.h>
#include <gui/widgets/waterfall.h>
#include <gui/tuner.h>
#include <utils/opengl_include_code.h>
#include <signal_path/vfo_manager.h>
#include <dsp/sink/handler_sink.h>
#include <dsp/types.h>
#include <dsp/window/blackman.h>
#include <dsp/window/nuttall.h>
#include <fftw3.h>

SDRPP_EXPORT Menu menu;
SDRPP_EXPORT ImGui::WaterFall waterfall;

namespace sigpath {
    SDRPP_EXPORT VFOManager vfoManager;
}

namespace {
    constexpr const char* DEFAULT_VFO = "Radio";
    constexpr int DEFAULT_FFT_COLUMNS = 640;
    constexpr int DEFAULT_WATERFALL_ROWS = 160;
    constexpr int MAX_WATERFALL_ROWS = 1024;
    constexpr int DEFAULT_FFT_SIZE = 4096;
    constexpr float SPECTRUM_HEIGHT = 150.0f;
    constexpr float WATERFALL_HEIGHT = 210.0f;
    constexpr double SPAN_MULTIPLIER = 3.0;
    constexpr double BUFFER_WINDOW_MULTIPLIER = 3.0;
    constexpr double FFT_FRAME_RATE = 12.0;
    constexpr int WATERFALL_APPEND_EVERY = 2;
    constexpr int PALETTE_RESOLUTION = 512;
    constexpr int WATERFALL_TEXTURE_X_SCALE = 2;
    constexpr int WATERFALL_TEXTURE_Y_SCALE = 2;

    enum class WindowMode {
        Rectangular = 0,
        Blackman = 1,
        Nuttall = 2
    };

    struct PlotFrame {
        std::vector<float> bins;
        double leftHz = 0.0;
        double rightHz = 0.0;
        double focusCenterHz = 0.0;
        double focusBandwidthHz = 0.0;
        float fftMinDb = -140.0f;
        float fftMaxDb = 0.0f;
        bool valid = false;
    };

    static float clamp01(float value) {
        return std::clamp(value, 0.0f, 1.0f);
    }

    static float inverseLerp(float a, float b, float value) {
        if (std::abs(b - a) < std::numeric_limits<float>::epsilon()) {
            return 0.0f;
        }
        return (value - a) / (b - a);
    }

    class SpyGlassModule : public ModuleManager::Instance {
    public:
        explicit SpyGlassModule(std::string instanceName)
            : name(std::move(instanceName)),
              hiddenTapName(name + "_spyglass_tap") {
            rows_.assign(MAX_WATERFALL_ROWS, std::vector<float>(bufferedColumnCount_, -140.0f));
            rowCenterHz_.assign(MAX_WATERFALL_ROWS, 0.0);
            rowSpanHz_.assign(MAX_WATERFALL_ROWS, 0.0);
            sampleRing_.assign(DEFAULT_FFT_SIZE, dsp::complex_t{0.0f, 0.0f});
            fftDb_.assign(DEFAULT_FFT_SIZE, -140.0f);
            createFftResources();
            menu.registerEntry(name, drawMenu, this, NULL);
        }

        ~SpyGlassModule() override {
            destroyTap();
            destroyWaterfallTexture();
            destroyFftResources();
            menu.removeEntry(name);
        }

        void postInit() override {}

        void enable() override {
            enabled_ = true;
        }

        void disable() override {
            enabled_ = false;
        }

        bool isEnabled() override {
            return enabled_;
        }

    private:
        static void drawMenu(void* ctx) {
            auto* self = static_cast<SpyGlassModule*>(ctx);
            self->draw();
        }

        static void iqHandler(dsp::complex_t* data, int count, void* ctx) {
            auto* self = static_cast<SpyGlassModule*>(ctx);
            self->consumeIq(data, count);
        }

        void draw() {
            if (!enabled_) {
                return;
            }

            updateFrame();
            drawControls();
            ImGui::Spacing();
            drawSpectrum();
            ImGui::Spacing();
            drawWaterfall();
        }

        void updateFrame() {
            frame_.valid = false;

            syncTrackedVfo();
            syncDisplayStyle();

            const double centerHz = waterfall.getCenterFrequency();
            const double focusOffsetHz = sigpath::vfoManager.getOffset(trackedVfoName_);
            const double focusBandwidthHz = std::max(1.0, sigpath::vfoManager.getBandwidth(trackedVfoName_));
            const double focusCenterHz = centerHz + focusOffsetHz;
            const double visibleSpanHz = focusBandwidthHz * SPAN_MULTIPLIER;
            const double tapSpanHz = visibleSpanHz * BUFFER_WINDOW_MULTIPLIER;

            ensureTap(focusOffsetHz, tapSpanHz);

            std::lock_guard<std::mutex> lck(dataMtx_);
            captureCenterHz_ = focusCenterHz;
            captureSpanHz_ = tapSpanHz;
            if (!spectrumValid_ || fftDb_.empty()) {
                return;
            }

            frame_.leftHz = focusCenterHz - (visibleSpanHz * 0.5);
            frame_.rightHz = focusCenterHz + (visibleSpanHz * 0.5);
            const auto bufferedBins = buildDisplayBins(fftDb_, bufferedColumnCount_, calibrationOffsetDb_);
            frame_.bins = extractVisibleBins(bufferedBins);
            frame_.focusCenterHz = focusCenterHz;
            frame_.focusBandwidthHz = focusBandwidthHz;
            frame_.fftMinDb = waterfall.getFFTMin();
            frame_.fftMaxDb = waterfall.getFFTMax();
            if (frame_.fftMaxDb < frame_.fftMinDb + 10.0f) {
                frame_.fftMinDb = displayMinDb_;
                frame_.fftMaxDb = displayMaxDb_;
            }
            updateCalibrationOffset(frame_.leftHz, frame_.rightHz, frame_.bins);
            if (std::abs(calibrationOffsetDb_) > 0.001f) {
                frame_.bins = extractVisibleBins(buildDisplayBins(fftDb_, bufferedColumnCount_, calibrationOffsetDb_));
            }
            frame_.valid = true;
        }

        void syncDisplayStyle() {
            bool nextSmoothingEnabled = false;
            float nextSmoothingAlpha = 0.15f;
            WindowMode nextWindowMode = WindowMode::Nuttall;

            displayMinDb_ = waterfall.getFFTMin();
            displayMaxDb_ = waterfall.getFFTMax();
            waterfallMinDb_ = waterfall.getWaterfallMin();
            waterfallMaxDb_ = waterfall.getWaterfallMax();

            if (displayMaxDb_ < displayMinDb_ + 10.0f) {
                displayMinDb_ = -140.0f;
                displayMaxDb_ = 0.0f;
            }
            if (waterfallMaxDb_ < waterfallMinDb_ + 1.0f) {
                waterfallMinDb_ = displayMinDb_;
                waterfallMaxDb_ = displayMaxDb_;
            }

            core::configManager.acquire();
            std::string colorMapName = core::configManager.conf.contains("colorMap")
                ? static_cast<std::string>(core::configManager.conf["colorMap"])
                : std::string("Classic");
            if (core::configManager.conf.contains("fftSmoothing")) {
                nextSmoothingEnabled = static_cast<bool>(core::configManager.conf["fftSmoothing"]);
            }
            if (core::configManager.conf.contains("fftSmoothingSpeed")) {
                nextSmoothingAlpha = std::clamp(static_cast<float>(core::configManager.conf["fftSmoothingSpeed"]), 0.001f, 1.0f);
            }
            if (core::configManager.conf.contains("fftWindow")) {
                nextWindowMode = static_cast<WindowMode>(std::clamp(static_cast<int>(core::configManager.conf["fftWindow"]), 0, 2));
            }
            core::configManager.release();

            smoothingEnabled_ = nextSmoothingEnabled;
            smoothingAlpha_ = nextSmoothingAlpha;
            smoothingBeta_ = 1.0f - smoothingAlpha_;
            if (windowMode_ != nextWindowMode) {
                windowMode_ = nextWindowMode;
                regenerateFftWindow();
            }

            if (colorMapName == activeColorMapName_ && !paletteCache_.empty()) {
                return;
            }

            activeColorMapName_ = colorMapName;
            paletteCache_.clear();

            auto it = colormaps::maps.find(colorMapName);
            if (it == colormaps::maps.end()) {
                it = colormaps::maps.find("Classic");
            }
            if (it == colormaps::maps.end() || it->second.entryCount <= 0 || it->second.map == nullptr) {
                return;
            }

            paletteCache_.resize(PALETTE_RESOLUTION);
            for (int i = 0; i < PALETTE_RESOLUTION; ++i) {
                const float pos = (static_cast<float>(i) / static_cast<float>(PALETTE_RESOLUTION)) * it->second.entryCount;
                const int lowerId = std::clamp(static_cast<int>(std::floor(pos)), 0, it->second.entryCount - 1);
                const int upperId = std::clamp(static_cast<int>(std::ceil(pos)), 0, it->second.entryCount - 1);
                const float ratio = pos - static_cast<float>(lowerId);
                const float r = (it->second.map[(lowerId * 3) + 0] * (1.0f - ratio)) + (it->second.map[(upperId * 3) + 0] * ratio);
                const float g = (it->second.map[(lowerId * 3) + 1] * (1.0f - ratio)) + (it->second.map[(upperId * 3) + 1] * ratio);
                const float b = (it->second.map[(lowerId * 3) + 2] * (1.0f - ratio)) + (it->second.map[(upperId * 3) + 2] * ratio);
                paletteCache_[i] = IM_COL32(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b), 255);
            }
        }

        void updateCalibrationOffset(double leftHz, double rightHz, const std::vector<float>& currentBins) {
            int mainWidth = 0;
            float* mainFft = waterfall.acquireLatestFFT(mainWidth);
            if (mainFft == nullptr || mainWidth <= 8 || currentBins.empty()) {
                if (mainFft != nullptr) {
                    waterfall.releaseLatestFFT();
                }
                return;
            }

            const double mainCenterHz = waterfall.getCenterFrequency() + waterfall.getViewOffset();
            const double mainSpanHz = waterfall.getViewBandwidth();
            const double mainLeftHz = mainCenterHz - (mainSpanHz * 0.5);
            const double mainRightHz = mainCenterHz + (mainSpanHz * 0.5);

            std::vector<float> diffs;
            diffs.reserve(currentBins.size());

            for (int i = 0; i < static_cast<int>(currentBins.size()); ++i) {
                const double binT = (static_cast<double>(i) + 0.5) / static_cast<double>(currentBins.size());
                const double freqHz = leftHz + ((rightHz - leftHz) * binT);
                if (freqHz < mainLeftHz || freqHz > mainRightHz) {
                    continue;
                }

                const double mainT = (freqHz - mainLeftHz) / (mainRightHz - mainLeftHz);
                const double mainX = mainT * static_cast<double>(mainWidth - 1);
                const int x0 = std::clamp(static_cast<int>(std::floor(mainX)), 0, mainWidth - 1);
                const int x1 = std::clamp(x0 + 1, 0, mainWidth - 1);
                const float frac = static_cast<float>(mainX - static_cast<double>(x0));
                const float mainDb = (mainFft[x0] * (1.0f - frac)) + (mainFft[x1] * frac);
                diffs.push_back(mainDb - currentBins[i]);
            }

            waterfall.releaseLatestFFT();

            if (diffs.size() < 16) {
                return;
            }

            const auto mid = diffs.begin() + (diffs.size() / 2);
            std::nth_element(diffs.begin(), mid, diffs.end());
            const float medianDiff = std::clamp(*mid, -60.0f, 60.0f);
            calibrationOffsetDb_ = (calibrationOffsetDb_ * 0.85f) + (medianDiff * 0.15f);
        }

        ImU32 paletteColor(float t) const {
            if (paletteCache_.empty()) {
                const int v = static_cast<int>(255.0f * clamp01(t));
                return IM_COL32(v, v, v, 255);
            }

            const int idx = std::clamp(static_cast<int>(std::round(clamp01(t) * static_cast<float>(paletteCache_.size() - 1))), 0, static_cast<int>(paletteCache_.size() - 1));
            return paletteCache_[idx];
        }

        void ensureTap(double focusOffsetHz, double spanHz) {
            const bool needCreate = (spyglassVfo_ == nullptr);
            const bool spanChanged = std::abs(spanHz - tapSpanHz_) > std::max(10.0, tapSpanHz_ * 0.01);

            if (needCreate || spanChanged) {
                recreateTap(focusOffsetHz, spanHz);
                return;
            }

            if (spyglassVfo_ != nullptr && std::abs(focusOffsetHz - tapOffsetHz_) > 1.0) {
                spyglassVfo_->setOffset(focusOffsetHz);
                tapOffsetHz_ = focusOffsetHz;
            }
        }

        void recreateTap(double focusOffsetHz, double spanHz) {
            destroyTap();

            spyglassVfo_ = sigpath::vfoManager.createVFO(
                hiddenTapName,
                ImGui::WaterfallVFO::REF_CENTER,
                focusOffsetHz,
                spanHz,
                spanHz,
                spanHz,
                spanHz,
                true
            );

            if (spyglassVfo_ == nullptr) {
                return;
            }

            spyglassVfo_->setColor(IM_COL32(0, 0, 0, 0));
            spyglassVfo_->wtfVFO->lineVisible = false;
            spyglassVfo_->wtfVFO->notchVisible = false;
            spyglassVfo_->wtfVFO->bandwidth = 1.0;
            spyglassVfo_->wtfVFO->minBandwidth = 1.0;
            spyglassVfo_->wtfVFO->maxBandwidth = 1.0;
            spyglassVfo_->wtfVFO->bandwidthLocked = true;
            spyglassVfo_->setBandwidthLimits(spanHz, spanHz, true);
            spyglassVfo_->setSampleRate(spanHz, spanHz);
            spyglassVfo_->setOffset(focusOffsetHz);

            tapSpanHz_ = spanHz;
            tapOffsetHz_ = focusOffsetHz;
            hopSize_ = std::max(1, static_cast<int>(std::round(spanHz / FFT_FRAME_RATE)));

            resetProcessingState();

            if (!tapInitialized_) {
                iqTap_.init(spyglassVfo_->output, iqHandler, this);
                tapInitialized_ = true;
            }
            else {
                iqTap_.setInput(spyglassVfo_->output);
            }
            iqTap_.start();
            tapRunning_ = true;
        }

        void destroyTap() {
            if (tapRunning_) {
                iqTap_.stop();
                tapRunning_ = false;
            }

            if (spyglassVfo_ != nullptr) {
                sigpath::vfoManager.deleteVFO(spyglassVfo_);
                spyglassVfo_ = nullptr;
            }

            tapSpanHz_ = 0.0;
            tapOffsetHz_ = 0.0;
            resetProcessingState();
        }

        void resetProcessingState() {
            std::lock_guard<std::mutex> lck(dataMtx_);
            sampleWrite_ = 0;
            sampleFill_ = 0;
            samplesSinceFft_ = 0;
            waterfallAppendCounter_ = 0;
            spectrumValid_ = false;
            std::fill(sampleRing_.begin(), sampleRing_.end(), dsp::complex_t{0.0f, 0.0f});
            std::fill(fftDb_.begin(), fftDb_.end(), -140.0f);
            std::fill(smoothingBuf_.begin(), smoothingBuf_.end(), -140.0f);
            displayMinDb_ = -140.0f;
            displayMaxDb_ = -20.0f;
            calibrationOffsetDb_ = 0.0f;
            for (auto& row : rows_) {
                std::fill(row.begin(), row.end(), -140.0f);
            }
            std::fill(rowCenterHz_.begin(), rowCenterHz_.end(), 0.0);
            std::fill(rowSpanHz_.begin(), rowSpanHz_.end(), 0.0);
            writeRow_ = 0;
            historyCount_ = 0;
        }

        void createFftResources() {
            const int fftSize = DEFAULT_FFT_SIZE;
            fftWindow_.assign(fftSize, 0.0f);
            smoothingBuf_.assign(fftSize, -140.0f);
            regenerateFftWindow();
            ensureWaterfallTexture(columnCount_, visibleRows_);

            fftIn_ = reinterpret_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * fftSize));
            fftOut_ = reinterpret_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * fftSize));
            fftPlan_ = fftwf_plan_dft_1d(fftSize, fftIn_, fftOut_, FFTW_FORWARD, FFTW_ESTIMATE);
        }

        void ensureWaterfallTexture(int width, int height) {
            width = std::max(1, width * WATERFALL_TEXTURE_X_SCALE);
            height = std::max(1, height * WATERFALL_TEXTURE_Y_SCALE);

            if (waterfallTextureId_ != 0 && waterfallTextureWidth_ == width && waterfallTextureHeight_ == height) {
                return;
            }

            destroyWaterfallTexture();

            waterfallTextureWidth_ = width;
            waterfallTextureHeight_ = height;
            waterfallTexturePixels_.assign(width * height, IM_COL32(0, 0, 0, 255));

            glGenTextures(1, &waterfallTextureId_);
            glBindTexture(GL_TEXTURE_2D, waterfallTextureId_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, waterfallTextureWidth_, waterfallTextureHeight_, 0, GL_RGBA, GL_UNSIGNED_BYTE, waterfallTexturePixels_.data());
        }

        void destroyWaterfallTexture() {
            if (waterfallTextureId_ != 0) {
                glDeleteTextures(1, &waterfallTextureId_);
                waterfallTextureId_ = 0;
            }
            waterfallTextureWidth_ = 0;
            waterfallTextureHeight_ = 0;
            waterfallTexturePixels_.clear();
        }

        void updateWaterfallTextureLocked() {
            ensureWaterfallTexture(columnCount_, visibleRows_);
            std::fill(waterfallTexturePixels_.begin(), waterfallTexturePixels_.end(), IM_COL32(0, 0, 0, 255));

            const int rows = std::min(visibleRows_, historyCount_);
            if (rows <= 0) {
                return;
            }

            const float wfMin = waterfallMinDb_;
            const float wfMax = waterfallMaxDb_;
            const double visibleSpanHz = frame_.rightHz - frame_.leftHz;
            const int texWidth = waterfallTextureWidth_;
            const int texHeight = waterfallTextureHeight_;
            const int visibleRowsForTexture = std::max(1, visibleRows_);

            for (int ty = 0; ty < texHeight; ++ty) {
                const double visibleRowPos = (static_cast<double>(ty) + 0.5) / static_cast<double>(WATERFALL_TEXTURE_Y_SCALE);
                const int visualRow = static_cast<int>(std::floor(visibleRowPos));
                if (visualRow >= rows) {
                    continue;
                }

                const int sourceRow = (writeRow_ - 1 - visualRow + static_cast<int>(rows_.size())) % static_cast<int>(rows_.size());
                const double rowCenter = rowCenterHz_[sourceRow];
                const double rowSpan = rowSpanHz_[sourceRow];
                const double rowLeft = rowCenter - (rowSpan * 0.5);

                for (int tx = 0; tx < texWidth; ++tx) {
                    const double freqHz = frame_.leftHz + ((static_cast<double>(tx) + 0.5) / static_cast<double>(texWidth)) * visibleSpanHz;
                    const double rowT = rowSpan > 0.0 ? ((freqHz - rowLeft) / rowSpan) : -1.0;
                    float sample = displayMinDb_;
                    if (rowT >= 0.0 && rowT <= 1.0) {
                        const double srcX = rowT * static_cast<double>(bufferedColumnCount_ - 1);
                        const int sx0 = std::clamp(static_cast<int>(std::floor(srcX)), 0, bufferedColumnCount_ - 1);
                        const int sx1 = std::clamp(sx0 + 1, 0, bufferedColumnCount_ - 1);
                        const float frac = static_cast<float>(srcX - static_cast<double>(sx0));
                        sample = (rows_[sourceRow][sx0] * (1.0f - frac)) + (rows_[sourceRow][sx1] * frac);
                    }

                    const float t = clamp01(inverseLerp(wfMin, wfMax, sample));
                    waterfallTexturePixels_[(ty * texWidth) + tx] = paletteColor(t);
                }
            }

            glBindTexture(GL_TEXTURE_2D, waterfallTextureId_);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, waterfallTextureWidth_, waterfallTextureHeight_, GL_RGBA, GL_UNSIGNED_BYTE, waterfallTexturePixels_.data());
        }

        void regenerateFftWindow() {
            const int fftSize = DEFAULT_FFT_SIZE;
            if (fftWindow_.size() != fftSize) {
                fftWindow_.assign(fftSize, 0.0f);
            }

            for (int i = 0; i < fftSize; ++i) {
                float w = 1.0f;
                if (windowMode_ == WindowMode::Blackman) {
                    w = static_cast<float>(dsp::window::blackman(i, fftSize));
                }
                else if (windowMode_ == WindowMode::Nuttall) {
                    w = static_cast<float>(dsp::window::nuttall(i, fftSize));
                }
                fftWindow_[i] = w * ((i & 1) ? -1.0f : 1.0f);
            }
        }

        void destroyFftResources() {
            if (fftPlan_ != nullptr) {
                fftwf_destroy_plan(fftPlan_);
                fftPlan_ = nullptr;
            }
            if (fftIn_ != nullptr) {
                fftwf_free(fftIn_);
                fftIn_ = nullptr;
            }
            if (fftOut_ != nullptr) {
                fftwf_free(fftOut_);
                fftOut_ = nullptr;
            }
        }

        void consumeIq(dsp::complex_t* data, int count) {
            if (count <= 0 || fftPlan_ == nullptr) {
                return;
            }

            std::lock_guard<std::mutex> lck(dataMtx_);

            for (int i = 0; i < count; ++i) {
                sampleRing_[sampleWrite_] = data[i];
                sampleWrite_ = (sampleWrite_ + 1) % DEFAULT_FFT_SIZE;
                sampleFill_ = std::min(sampleFill_ + 1, DEFAULT_FFT_SIZE);
            }

            samplesSinceFft_ += count;
            while (sampleFill_ == DEFAULT_FFT_SIZE && samplesSinceFft_ >= hopSize_) {
                computeSpectrumFrame();
                samplesSinceFft_ -= hopSize_;
            }
        }

        void computeSpectrumFrame() {
            const int fftSize = DEFAULT_FFT_SIZE;

            for (int i = 0; i < fftSize; ++i) {
                const int srcIndex = (sampleWrite_ + i) % fftSize;
                fftIn_[i][0] = sampleRing_[srcIndex].re * fftWindow_[i];
                fftIn_[i][1] = sampleRing_[srcIndex].im * fftWindow_[i];
            }

            fftwf_execute(fftPlan_);

            float localMin = std::numeric_limits<float>::max();
            float localMax = -std::numeric_limits<float>::max();
            for (int i = 0; i < fftSize; ++i) {
                const float re = fftOut_[i][0];
                const float im = fftOut_[i][1];
                const float power = ((re * re) + (im * im)) / static_cast<float>(fftSize * fftSize);
                float db = 10.0f * std::log10(std::max(power, 1e-20f));
                if (smoothingEnabled_) {
                    smoothingBuf_[i] = (smoothingBuf_[i] * smoothingBeta_) + (db * smoothingAlpha_);
                    db = smoothingBuf_[i];
                }
                fftDb_[i] = db;
                localMin = std::min(localMin, db);
                localMax = std::max(localMax, db);
            }

            spectrumValid_ = true;

            if (!freezeWaterfall_ && (++waterfallAppendCounter_ % WATERFALL_APPEND_EVERY) == 0) {
                appendWaterfallRow(buildDisplayBins(fftDb_, bufferedColumnCount_, calibrationOffsetDb_));
            }
        }

        static std::vector<float> buildDisplayBins(const std::vector<float>& source, int columns, float offsetDb = 0.0f) {
            std::vector<float> out(columns, -140.0f);
            if (source.empty() || columns <= 0) {
                return out;
            }

            for (int x = 0; x < columns; ++x) {
                const int start = static_cast<int>((static_cast<long long>(x) * source.size()) / columns);
                const int end = std::max(start + 1, static_cast<int>((static_cast<long long>(x + 1) * source.size()) / columns));
                float peak = source[start];
                for (int i = start + 1; i < std::min(end, static_cast<int>(source.size())); ++i) {
                    peak = std::max(peak, source[i]);
                }
                out[x] = peak + offsetDb;
            }

            return out;
        }

        std::vector<float> extractVisibleBins(const std::vector<float>& bufferedBins) const {
            if (bufferedBins.empty()) {
                return std::vector<float>(columnCount_, displayMinDb_);
            }

            const int visibleCols = std::min(columnCount_, static_cast<int>(bufferedBins.size()));
            const int start = std::max(0, (static_cast<int>(bufferedBins.size()) - visibleCols) / 2);
            return std::vector<float>(bufferedBins.begin() + start, bufferedBins.begin() + start + visibleCols);
        }

        void appendWaterfallRow(const std::vector<float>& row) {
            if (rows_.empty() || row.size() != rows_.front().size()) {
                return;
            }

            rows_[writeRow_] = row;
            rowCenterHz_[writeRow_] = captureCenterHz_;
            rowSpanHz_[writeRow_] = captureSpanHz_;
            writeRow_ = (writeRow_ + 1) % rows_.size();
            historyCount_ = std::min(historyCount_ + 1, static_cast<int>(rows_.size()));
        }

        void drawControls() {
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

            if (!frame_.valid) {
                int rows = visibleRows_;
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Waterfall History");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::SliderInt("##spyglass_history_rows", &rows, 100, 200);
                ImGui::Checkbox("Freeze Waterfall", &freezeWaterfall_);
                ImGui::PopItemWidth();
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "No independent SpyGlass FFT frame available yet.");
                return;
            }

            ImGui::Text("Focus bandwidth: %.0f kHz", frame_.focusBandwidthHz / 1e3);
            ImGui::Text("SpyGlass span: %.0f kHz", (frame_.rightHz - frame_.leftHz) / 1e3);

            int rows = visibleRows_;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Waterfall History");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::SliderInt("##spyglass_history_rows", &rows, 100, 200)) {
                visibleRows_ = rows;
            }

            ImGui::Checkbox("Freeze Waterfall", &freezeWaterfall_);
            ImGui::PopItemWidth();
        }

        void drawSpectrum() {
            if (!frame_.valid) {
                return;
            }

            const ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, SPECTRUM_HEIGHT);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 end(origin.x + canvasSize.x, origin.y + canvasSize.y);
            auto* drawList = ImGui::GetWindowDrawList();

            drawList->AddRectFilled(origin, end, IM_COL32(10, 14, 18, 255), 6.0f);
            drawList->AddRect(origin, end, IM_COL32(65, 78, 92, 255), 6.0f);

            for (int i = 1; i < 4; ++i) {
                const float y = origin.y + ((canvasSize.y / 4.0f) * static_cast<float>(i));
                drawList->AddLine(ImVec2(origin.x, y), ImVec2(end.x, y), IM_COL32(40, 48, 56, 255));
            }

            const double spanHz = frame_.rightHz - frame_.leftHz;
            const float dbMin = frame_.fftMinDb;
            const float dbMax = std::max(frame_.fftMaxDb, dbMin + 1.0f);

            std::vector<ImVec2> points;
            points.reserve(frame_.bins.size());

            for (int i = 0; i < static_cast<int>(frame_.bins.size()); ++i) {
                const float xT = static_cast<float>(i) / static_cast<float>(std::max(1, static_cast<int>(frame_.bins.size()) - 1));
                const float yT = clamp01(1.0f - inverseLerp(dbMin, dbMax, frame_.bins[i]));
                points.emplace_back(origin.x + (canvasSize.x * xT), origin.y + (canvasSize.y * yT));
            }

            if (!points.empty()) {
                drawList->AddPolyline(points.data(), static_cast<int>(points.size()), IM_COL32(210, 230, 255, 255), 0, 1.6f);
            }

            const float leftMarkerT = static_cast<float>((frame_.focusCenterHz - (frame_.focusBandwidthHz * 0.5) - frame_.leftHz) / spanHz);
            const float rightMarkerT = static_cast<float>((frame_.focusCenterHz + (frame_.focusBandwidthHz * 0.5) - frame_.leftHz) / spanHz);
            const float centerMarkerT = static_cast<float>((frame_.focusCenterHz - frame_.leftHz) / spanHz);

            drawVerticalMarker(drawList, origin, end, centerMarkerT, IM_COL32(255, 242, 122, 220), 2.0f);
            drawVerticalMarker(drawList, origin, end, leftMarkerT, IM_COL32(255, 90, 90, 255), 2.5f);
            drawVerticalMarker(drawList, origin, end, rightMarkerT, IM_COL32(255, 90, 90, 255), 2.5f);

            ImGui::InvisibleButton("##spyglass-spectrum", canvasSize);
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                tuneToMouse(origin, canvasSize.x);
            }
        }

        void drawWaterfall() {
            if (!frame_.valid || rows_.empty() || historyCount_ <= 0) {
                return;
            }

            const ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, WATERFALL_HEIGHT);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 end(origin.x + canvasSize.x, origin.y + canvasSize.y);
            auto* drawList = ImGui::GetWindowDrawList();

            drawList->AddRectFilled(origin, end, IM_COL32(8, 9, 14, 255), 6.0f);
            drawList->AddRect(origin, end, IM_COL32(65, 78, 92, 255), 6.0f);

            {
                std::lock_guard<std::mutex> lck(dataMtx_);
                updateWaterfallTextureLocked();
            }

            drawList->AddImage(reinterpret_cast<void*>(static_cast<intptr_t>(waterfallTextureId_)), origin, end);

            const double spanHz = frame_.rightHz - frame_.leftHz;
            const float leftMarkerT = static_cast<float>((frame_.focusCenterHz - (frame_.focusBandwidthHz * 0.5) - frame_.leftHz) / spanHz);
            const float rightMarkerT = static_cast<float>((frame_.focusCenterHz + (frame_.focusBandwidthHz * 0.5) - frame_.leftHz) / spanHz);
            const float centerMarkerT = static_cast<float>((frame_.focusCenterHz - frame_.leftHz) / spanHz);
            drawVerticalMarker(drawList, origin, end, centerMarkerT, IM_COL32(255, 242, 122, 220), 2.0f);
            drawVerticalMarker(drawList, origin, end, leftMarkerT, IM_COL32(255, 90, 90, 255), 2.5f);
            drawVerticalMarker(drawList, origin, end, rightMarkerT, IM_COL32(255, 90, 90, 255), 2.5f);

            ImGui::InvisibleButton("##spyglass-waterfall", canvasSize);
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                tuneToMouse(origin, canvasSize.x);
            }
        }

        static void drawVerticalMarker(ImDrawList* drawList, const ImVec2& origin, const ImVec2& end, float t, ImU32 color, float thickness) {
            if (t < 0.0f || t > 1.0f) {
                return;
            }

            const float x = origin.x + ((end.x - origin.x) * t);
            drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), color, thickness);
        }

        void tuneToMouse(const ImVec2& origin, float width) {
            if (!frame_.valid || width <= 0.0f) {
                return;
            }

            const float t = clamp01((ImGui::GetIO().MousePos.x - origin.x) / width);
            const double freqHz = frame_.leftHz + ((frame_.rightHz - frame_.leftHz) * static_cast<double>(t));
            tuner::normalTuning(trackedVfoName_, freqHz);
        }

        void syncTrackedVfo() {
            if (waterfall.selectedVFO == hiddenTapName) {
                waterfall.selectedVFO = trackedVfoName_;
                waterfall.selectedVFOChanged = true;
            }

            if (!waterfall.selectedVFO.empty() && waterfall.selectedVFO != hiddenTapName && waterfall.selectedVFO != trackedVfoName_) {
                trackedVfoName_ = waterfall.selectedVFO;
                return;
            }

            if (trackedVfoName_.empty()) {
                trackedVfoName_ = DEFAULT_VFO;
            }
        }

        std::string name;
        std::string hiddenTapName;
        bool enabled_ = true;
        bool freezeWaterfall_ = false;
        bool tapInitialized_ = false;
        bool tapRunning_ = false;
        bool spectrumValid_ = false;
        std::string trackedVfoName_ = DEFAULT_VFO;
        int columnCount_ = DEFAULT_FFT_COLUMNS;
        int bufferedColumnCount_ = DEFAULT_FFT_COLUMNS * static_cast<int>(BUFFER_WINDOW_MULTIPLIER);
        int visibleRows_ = DEFAULT_WATERFALL_ROWS;
        int historyCount_ = 0;
        int writeRow_ = 0;
        int hopSize_ = 1;
        int sampleWrite_ = 0;
        int sampleFill_ = 0;
        int samplesSinceFft_ = 0;
        int waterfallAppendCounter_ = 0;
        double tapSpanHz_ = 0.0;
        double tapOffsetHz_ = 0.0;
        float displayMinDb_ = -140.0f;
        float displayMaxDb_ = -20.0f;
        float waterfallMinDb_ = -70.0f;
        float waterfallMaxDb_ = 0.0f;
        float calibrationOffsetDb_ = 0.0f;
        bool smoothingEnabled_ = false;
        float smoothingAlpha_ = 0.15f;
        float smoothingBeta_ = 0.85f;
        WindowMode windowMode_ = WindowMode::Nuttall;
        PlotFrame frame_;
        std::string activeColorMapName_ = "Classic";
        VFOManager::VFO* spyglassVfo_ = nullptr;
        dsp::sink::Handler<dsp::complex_t> iqTap_;
        std::vector<dsp::complex_t> sampleRing_;
        std::vector<float> fftWindow_;
        std::vector<float> fftDb_;
        std::vector<float> smoothingBuf_;
        std::vector<std::vector<float>> rows_;
        std::vector<double> rowCenterHz_;
        std::vector<double> rowSpanHz_;
        std::vector<ImU32> paletteCache_;
        std::vector<ImU32> waterfallTexturePixels_;
        double captureCenterHz_ = 0.0;
        double captureSpanHz_ = 0.0;
        GLuint waterfallTextureId_ = 0;
        int waterfallTextureWidth_ = 0;
        int waterfallTextureHeight_ = 0;
        fftwf_complex* fftIn_ = nullptr;
        fftwf_complex* fftOut_ = nullptr;
        fftwf_plan fftPlan_ = nullptr;
        std::mutex dataMtx_;
    };
}

SDRPP_MOD_INFO{
    /* Name:            */ "spyglass",
    /* Description:     */ "Zoomed FFT/waterfall inspector for SDR++",
    /* Author:          */ "OpenAI Codex",
    /* Version:         */ 0, 2, 0,
    /* Max instances    */ 1
};

MOD_EXPORT void _INIT_() {
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new SpyGlassModule(std::move(name));
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (SpyGlassModule*)instance;
}

MOD_EXPORT void _END_() {
}
