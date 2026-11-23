#include "AppUpdate.h"

#include <Velopack.hpp>
#include <cstdio>

namespace {
Velopack::UpdateManager makeUpdateManager()
{
#ifdef KLIPPIT_LOCAL_UPDATE_TEST
    const QString base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty()) throw std::runtime_error("LOCALAPPDATA is unavailable.");
    return Velopack::UpdateManager((base + "/Klippit-update-test").toStdString());
#else
    // GitHubSource resolves release assets from the repository URL.
    return Velopack::UpdateManager(std::make_unique<Velopack::GithubSource>(
        "https://github.com/yahya-kousa/Klippit"));
#endif
}
}

int checkAppUpdate(bool download)
{
    try {
        auto manager = makeUpdateManager();
        auto pending = manager.UpdatePendingRestart();
        if (!pending) {
            const auto update = manager.CheckForUpdates();
            if (!update) return 0;
            if (!download) {
                std::puts(update->TargetFullRelease.Version.c_str());
                return 0;
            }
            manager.DownloadUpdates(*update);
            pending = manager.UpdatePendingRestart();
            if (!pending) throw std::runtime_error("Downloaded update is unavailable for restart.");
        }
        std::puts(pending->Version.c_str());
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

QString scheduleAppUpdate(const QString &expectedVersion)
{
    try {
        auto manager = makeUpdateManager();
        const auto pending = manager.UpdatePendingRestart();
        if (!pending || QString::fromStdString(pending->Version) != expectedVersion)
            return QStringLiteral("The downloaded update changed. Check for updates again.");
        manager.WaitExitThenApplyUpdates(*pending);
        return {};
    } catch (const std::exception &error) {
        return QString::fromUtf8(error.what());
    }
}
