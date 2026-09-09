#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>

// ---------------------------------------------------------------------------
//  Auto-updater
//
//  On a background thread it fetches a small JSON manifest, compares the
//  "version" field to this build, and (if newer) downloads + unzips the new
//  "MPE Bender.vst3" bundle into a staging folder. installStagedUpdate() then
//  launches a tiny detached script that copies the new bundle over the
//  installed one as soon as the DAW releases the lock (i.e. next time it's
//  closed), so friends running an older copy pick up changes automatically.
//
//  To wire it up: host `latest.json` + the zipped `.vst3` somewhere static
//  (GitHub Releases / any web host) and set MPE_BENDER_UPDATE_MANIFEST_URL.
//  See publish-update.ps1.
//
//  latest.json:
//    { "version": "0.3.1",
//      "download": "https://.../MPE Bender-0.3.1.vst3.zip",
//      "notes": "what changed" }
// ---------------------------------------------------------------------------

#ifndef MPE_BENDER_UPDATE_MANIFEST_URL
 #define MPE_BENDER_UPDATE_MANIFEST_URL "https://raw.githubusercontent.com/CHANGE-ME/mpe-bender/main/latest.json"
#endif

class UpdateChecker final : private juce::Thread,
                            private juce::AsyncUpdater
{
public:
    enum class State { idle, checking, upToDate, updateAvailable, downloading, readyToInstall, failed };

    UpdateChecker() : juce::Thread("MPE Bender update check") {}

    ~UpdateChecker() override
    {
        stopThread(4000);
    }

    std::function<void()> onChanged;

    bool isConfigured() const
    {
        return ! juce::String(MPE_BENDER_UPDATE_MANIFEST_URL).contains("CHANGE-ME");
    }

    juce::String currentVersion() const
    {
       #ifdef JucePlugin_VersionString
        return JucePlugin_VersionString;
       #else
        return "0.0.0";
       #endif
    }

    State getState() const noexcept        { return state.load(); }
    juce::String getStatusText() const     { const juce::ScopedLock sl(lock); return statusText; }
    juce::String getLatestVersion() const  { const juce::ScopedLock sl(lock); return latestVersion; }
    juce::String getReleaseNotes() const   { const juce::ScopedLock sl(lock); return releaseNotes; }

    // Auto-check at most once per 24h; manual check ignores the throttle.
    void checkOnStartup()
    {
        if (! isConfigured())
        {
            setState(State::idle, "Auto-update not configured");
            return;
        }
        const auto last = lastCheckFile().existsAsFile()
                              ? lastCheckFile().loadFileAsString().trim().getLargeIntValue() : 0;
        const auto now = juce::Time::currentTimeMillis() / 1000;
        if (now - last > 24 * 60 * 60)
            check(false);
        else
            setState(State::idle, "Up to date (v" + currentVersion() + ")");
    }

    void check(bool userInitiated)
    {
        if (isThreadRunning())
            return;
        if (! isConfigured())
        {
            setState(State::idle, "Auto-update not configured - set the manifest URL");
            return;
        }
        manualCheck = userInitiated;
        setState(State::checking, "Checking for updates...");
        startThread();
    }

    // Kick off the (detached) copy-when-unlocked installer.
    void installStagedUpdate()
    {
        auto bundle = stagedBundle();
        if (! bundle.isDirectory())
        {
            setState(State::failed, "No downloaded update to install");
            return;
        }
        auto target = installedBundle();
        if (target == juce::File())
        {
            setState(State::failed, "Couldn't locate the installed plugin");
            return;
        }
        if (launchDetachedInstaller(bundle, target))
            setState(State::readyToInstall,
                     "Update staged - it installs automatically once you close every DAW using MPE Bender, "
                     "then restart.");
        else
            setState(State::failed, "Couldn't start the installer. Update folder: "
                                    + bundle.getParentDirectory().getFullPathName());
    }

    juce::File updateFolder() const { return stagingDir(); }

private:
    // -- paths --
    static juce::File baseDir()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("MPE Bender");
    }
    static juce::File stagingDir()   { return baseDir().getChildFile("update"); }
    static juce::File lastCheckFile(){ return stagingDir().getChildFile("last_check.txt"); }
    static juce::File stagedBundle() { return stagingDir().getChildFile("MPE Bender.vst3"); }

    // The installed VST3 bundle folder that contains this running module.
    static juce::File installedBundle();

    static bool launchDetachedInstaller(const juce::File& stagedBundleDir, const juce::File& installedBundleDir);

    void setState(State s, juce::String text)
    {
        {
            const juce::ScopedLock sl(lock);
            statusText = std::move(text);
        }
        state.store(s);
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override { if (onChanged) onChanged(); }

    static int compareVersions(const juce::String& a, const juce::String& b)
    {
        auto pa = juce::StringArray::fromTokens(a, ".", "");
        auto pb = juce::StringArray::fromTokens(b, ".", "");
        for (int i = 0; i < juce::jmax(pa.size(), pb.size()); ++i)
        {
            const int x = i < pa.size() ? pa[i].getIntValue() : 0;
            const int y = i < pb.size() ? pb[i].getIntValue() : 0;
            if (x != y) return x < y ? -1 : 1;
        }
        return 0;
    }

    void run() override
    {
        stagingDir().createDirectory();
        lastCheckFile().replaceWithText(juce::String(juce::Time::currentTimeMillis() / 1000));

        juce::URL manifestUrl(MPE_BENDER_UPDATE_MANIFEST_URL);
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs(12000);
        std::unique_ptr<juce::InputStream> in(manifestUrl.createInputStream(opts));
        if (in == nullptr)
        {
            setState(State::failed, "Couldn't reach the update server");
            return;
        }

        auto json = juce::JSON::parse(in->readEntireStreamAsString());
        const auto newVersion = json.getProperty("version", "").toString().trim();
        const auto downloadUrl = json.getProperty("download", "").toString().trim();
        const auto notes = json.getProperty("notes", "").toString();

        {
            const juce::ScopedLock sl(lock);
            latestVersion = newVersion;
            releaseNotes = notes;
        }

        if (newVersion.isEmpty() || compareVersions(currentVersion(), newVersion) >= 0)
        {
            setState(State::upToDate, "Up to date (v" + currentVersion() + ")");
            return;
        }

        if (downloadUrl.isEmpty())
        {
            setState(State::updateAvailable, "v" + newVersion + " available (no download link in manifest)");
            return;
        }

        setState(State::downloading, "Downloading v" + newVersion + "...");

        auto zip = stagingDir().getChildFile("update.zip");
        zip.deleteFile();
        if (threadShouldExit())
            return;

        auto dlOpts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                          .withConnectionTimeoutMs(20000);
        std::unique_ptr<juce::InputStream> dl(juce::URL(downloadUrl).createInputStream(dlOpts));
        if (dl == nullptr)
        {
            setState(State::failed, "Download failed");
            return;
        }
        {
            juce::FileOutputStream out(zip);
            if (out.failedToOpen() || out.writeFromInputStream(*dl, -1) <= 0)
            {
                setState(State::failed, "Couldn't save the download");
                return;
            }
        }

        auto extractRoot = stagingDir();
        stagedBundle().deleteRecursively();
        juce::ZipFile archive(zip);
        auto result = archive.uncompressTo(extractRoot, true);
        zip.deleteFile();
        if (result.failed())
        {
            setState(State::failed, "Couldn't unpack the update");
            return;
        }

        // the zip may contain the bundle at its root or one level down
        if (! stagedBundle().isDirectory())
        {
            for (auto& f : extractRoot.findChildFiles(juce::File::findDirectories, true, "MPE Bender.vst3"))
            {
                f.moveFileTo(stagedBundle());
                break;
            }
        }

        if (stagedBundle().isDirectory())
            setState(State::readyToInstall, "v" + newVersion + " downloaded - click Install update");
        else
            setState(State::failed, "Update package didn't contain MPE Bender.vst3");
    }

    juce::CriticalSection lock;
    std::atomic<State> state { State::idle };
    juce::String statusText { "Idle" };
    juce::String latestVersion, releaseNotes;
    bool manualCheck = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateChecker)
};
