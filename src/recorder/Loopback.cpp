#include "Loopback.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDeadlineTimer>
#include <cstdio>
#include <QtEndian>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <memory>

using Microsoft::WRL::ComPtr;

constexpr HRESULT pipeClosed = HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE);

namespace {
void check(HRESULT hr)
{
    if (FAILED(hr)) throw hr;
}

void writeBytes(const void *data, DWORD size)
{
    auto bytes = static_cast<const char *>(data);
    while (size) {
        DWORD written = 0;
        if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes, size, &written, nullptr))
            throw static_cast<HRESULT>(HRESULT_FROM_WIN32(GetLastError()));
        if (!written) throw pipeClosed;
        bytes += written;
        size -= written;
    }
}

void writeSize(quint32 value)
{
    const auto little = qToLittleEndian(value);
    writeBytes(&little, sizeof(little));
}

void streamDefaultOutput()
{
    ComPtr<IMMDeviceEnumerator> devices;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           IID_PPV_ARGS(devices.GetAddressOf())));
    ComPtr<IMMDevice> device;
    check(devices->GetDefaultAudioEndpoint(eRender, eConsole, device.GetAddressOf()));
    ComPtr<IAudioClient> input, clock;
    check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void **>(input.GetAddressOf())));
    WAVEFORMATEX *raw = nullptr;
    check(input->GetMixFormat(&raw));
    std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> format(raw, CoTaskMemFree);
    check(input->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                            1000000, 0, raw, nullptr));

    // Keep the endpoint clock running during quiet passages so silence has
    // duration too. Both clients use the endpoint's native mix format.
    check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void **>(clock.GetAddressOf())));
    check(clock->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, raw, nullptr));
    ComPtr<IAudioRenderClient> render;
    ComPtr<IAudioCaptureClient> capture;
    check(clock->GetService(IID_PPV_ARGS(render.GetAddressOf())));
    check(input->GetService(IID_PPV_ARGS(capture.GetAddressOf())));
    UINT32 capacity;
    check(clock->GetBufferSize(&capacity));
    auto fillSilence = [&]{
        UINT32 padding;
        check(clock->GetCurrentPadding(&padding));
        const UINT32 freeFrames = capacity - padding;
        if (freeFrames) {
            BYTE *unused;
            check(render->GetBuffer(freeFrames, &unused));
            check(render->ReleaseBuffer(freeFrames, AUDCLNT_BUFFERFLAGS_SILENT));
        }
    };
    fillSilence();
    writeBytes("RIFF", 4);
    writeSize(0xffffffff);
    writeBytes("WAVEfmt ", 8);
    const DWORD formatSize = sizeof(WAVEFORMATEX) + raw->cbSize;
    writeSize(formatSize);
    writeBytes(raw, formatSize);
    writeBytes("data", 4);
    writeSize(0xffffffff);

    check(input->Start());
    check(clock->Start());
    const char silence = raw->wBitsPerSample == 8 ? char(0x80) : char(0);
    QByteArray zeros;
    UINT64 expectedPosition = 0;
    bool havePosition = false;
    for (;;) {
        fillSilence();
        UINT32 available;
        check(capture->GetNextPacketSize(&available));
        if (!available) { Sleep(5); continue; }
        BYTE *samples;
        UINT32 frames;
        DWORD flags;
        UINT64 position;
        check(capture->GetBuffer(&samples, &frames, &flags, &position, nullptr));
        const DWORD size = frames * raw->nBlockAlign;
        if (havePosition && position > expectedPosition &&
            !(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)) {
            // Keep lost samples from moving all subsequent audio earlier.
            UINT64 missing = position - expectedPosition;
            zeros.fill(silence, raw->nBlockAlign * 4096);
            while (missing) {
                const DWORD count = DWORD(qMin<UINT64>(missing, 4096));
                writeBytes(zeros.constData(), count * raw->nBlockAlign);
                missing -= count;
            }
        }
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            zeros.fill(silence, size);
            writeBytes(zeros.constData(), size);
        } else {
            writeBytes(samples, size);
        }
        expectedPosition = position + frames;
        havePosition = !(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR);
        check(capture->ReleaseBuffer(frames));
    }
}
} // namespace
#endif

int streamLoopback()
{
#ifdef Q_OS_WIN
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 1;
    try {
        streamDefaultOutput();
    } catch (HRESULT error) {
        hr = error;
    }
    CoUninitialize();
    if (hr == pipeClosed || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA)) return 0;
    qWarning("Desktop audio capture failed (HRESULT 0x%08x).", unsigned(hr));
#endif
    return 1;
}

bool startCapture(QProcess &ffmpeg, const QStringList &args, QProcess *loopback)
{
    if (loopback) {
        loopback->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        loopback->setStandardOutputProcess(&ffmpeg);
        ffmpeg.setReadChannel(QProcess::StandardError);
    } else {
        ffmpeg.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    }

    ffmpeg.start("ffmpeg", args);
    if (!ffmpeg.waitForStarted()) {
        qWarning().noquote() << ffmpeg.errorString();
        return false;
    }
    if (!loopback) return true;

    // FFmpeg reports input 0 before trying to read the WAV header on stdin.
    // Handle split stderr lines, and bound the wait if that diagnostic changes.
    QDeadlineTimer deadline(10000);
    bool videoReady = false;
    while (!deadline.hasExpired() && !videoReady) {
        while (ffmpeg.canReadLine()) {
            const QByteArray line = ffmpeg.readLine();
            std::fwrite(line.constData(), 1, line.size(), stderr);
            if (line.startsWith("Input #0, lavfi,")) {
                videoReady = true;
                break;
            }
        }
        if (!videoReady && (ffmpeg.state() == QProcess::NotRunning ||
                            !ffmpeg.waitForReadyRead(int(deadline.remainingTime())))) break;
    }
    ffmpeg.setReadChannel(QProcess::StandardOutput);
    auto forwardErrors = [&ffmpeg] {
        const QByteArray bytes = ffmpeg.readAllStandardError();
        std::fwrite(bytes.constData(), 1, bytes.size(), stderr);
    };
    QObject::connect(&ffmpeg, &QProcess::readyReadStandardError, &ffmpeg, forwardErrors);
    forwardErrors();
    if (videoReady) {
        loopback->start(QCoreApplication::applicationFilePath(), {"--internal-loopback"});
        if (loopback->waitForStarted()) return true;
        qWarning().noquote() << loopback->errorString();
    } else {
        std::fputs("FFmpeg did not announce desktop video input 0; stopping capture.\n", stderr);
    }
    if (ffmpeg.state() != QProcess::NotRunning) {
        ffmpeg.kill();
        ffmpeg.waitForFinished();
    }
    return false;
}
