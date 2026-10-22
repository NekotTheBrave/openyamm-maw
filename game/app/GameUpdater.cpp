#include "game/app/GameUpdater.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_system.h>
#include <SDL3/SDL_timer.h>

#if defined(__ANDROID__)
#include <jni.h>

#include <sstream>
#include <vector>
#endif

namespace OpenYAMM::Game
{
namespace
{
// The updater of a versioned installation: this executable runs from app/<version>/.
class InstallationUpdater final : public GameUpdater
{
public:
    InstallationUpdater(Engine::AppInstallation installation, std::string version, const std::string &baseUrl,
                        std::unique_ptr<Engine::HttpTransport> transport)
        : m_installation(installation)
        , m_version(version)
        , m_updater(installation, version, Engine::releasePlatformName(), baseUrl, std::move(transport))
    {
    }

    std::string installedVersion() const override
    {
        return m_version;
    }

    void check() override
    {
        m_updater.check();
    }

    Engine::UpdateStatus status() const override
    {
        return m_updater.status();
    }

    void startUpdate() override
    {
        m_updater.download();
    }

    bool updatesInGame() const override
    {
        return true;
    }

    void cancel() override
    {
        m_updater.cancel();
    }

    bool restartIntoUpdate(std::string &error) override
    {
        if (!activate(error))
        {
            return false;
        }

        // The launcher starts the new version and counts its start for the rollback rule.
        const std::u8string launcherPath = m_installation.launcherPath().u8string();
        const std::string launcher(launcherPath.begin(), launcherPath.end());
        const char *arguments[] = {launcher.c_str(), nullptr};
        // No inherited standard handles: a Windows GUI program usually has none, and duplicating them fails.
        const SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, arguments);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
        SDL_Process *pProcess = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);

        if (pProcess == nullptr)
        {
            error = std::string("cannot start ") + launcher + ": " + SDL_GetError();
            return false;
        }

        SDL_DestroyProcess(pProcess);
        return true;
    }

    bool activate(std::string &error)
    {
        return m_updater.activate(error);
    }

    void wait()
    {
        m_updater.wait();
    }

    void markHealthy(std::ostream &log) override
    {
        Engine::finishHealthyStart(m_installation, m_version, log);
    }

    std::optional<std::string> takeRollbackNotice() override
    {
        return m_installation.takeRollbackNotice();
    }

private:
    Engine::AppInstallation m_installation;
    std::string m_version;
    Engine::AppUpdater m_updater;
};

std::unique_ptr<InstallationUpdater> createInstallationUpdater(const std::string &baseUrl)
{
    const char *pBasePath = SDL_GetBasePath();

    if (pBasePath == nullptr)
    {
        return nullptr;
    }

    std::filesystem::path executableDirectory = std::filesystem::path(pBasePath).lexically_normal();

    if (!executableDirectory.has_filename())
    {
        executableDirectory = executableDirectory.parent_path();
    }

    const std::optional<Engine::AppInstallation> installation =
        Engine::AppInstallation::containing(executableDirectory);
    std::unique_ptr<Engine::HttpTransport> transport = Engine::createPlatformHttpTransport();

    if (!installation || !transport)
    {
        return nullptr;
    }

    return std::make_unique<InstallationUpdater>(*installation, executableDirectory.filename().string(), baseUrl,
                                                 std::move(transport));
}

#if defined(__ANDROID__)
// The Android updater lives in Java (OpenYammActivity's updater* methods and OpenYammUpdateActivity): the APK and
// its content packs are installed outside the running game.
class AndroidUpdater final : public GameUpdater
{
public:
    explicit AndroidUpdater(std::string baseUrl)
        : m_baseUrl(std::move(baseUrl))
    {
    }

    std::string installedVersion() const override
    {
        return callString("updaterInstalledVersion", "()Ljava/lang/String;");
    }

    void check() override
    {
        callWithUrl("updaterStartCheck");
    }

    Engine::UpdateStatus status() const override
    {
        // "state|release|bytes|message", see OpenYammActivity.updaterCheckResult.
        std::vector<std::string> fields;
        std::stringstream stream(callString("updaterCheckResult", "()Ljava/lang/String;"));
        std::string field;

        while (std::getline(stream, field, '|'))
        {
            fields.push_back(field);
        }

        fields.resize(4);
        Engine::UpdateStatus status;
        status.release = fields[1];
        status.totalBytes = fields[2].empty() ? 0 : std::stoull(fields[2]);
        status.message = fields[3];
        // An available update's last field is its release notes.
        status.notes = fields[0] == "available" ? fields[3] : std::string();
        status.state = fields[0] == "checking"    ? Engine::UpdateState::Checking
            : fields[0] == "available"            ? Engine::UpdateState::Available
            : fields[0] == "uptodate"             ? Engine::UpdateState::UpToDate
            : fields[0] == "failed"               ? Engine::UpdateState::Failed
                                                  : Engine::UpdateState::Idle;
        return status;
    }

    void startUpdate() override
    {
        callWithUrl("updaterOpen");
    }

    bool updatesInGame() const override
    {
        return false;
    }

    void cancel() override
    {
    }

    bool restartIntoUpdate(std::string &error) override
    {
        error = "Android installs updates in its update screen";
        return false;
    }

    void markHealthy(std::ostream &) override
    {
    }

    std::optional<std::string> takeRollbackNotice() override
    {
        return std::nullopt;
    }

private:
    template <typename Call>
    static void withActivity(const char *pName, const char *pSignature, Call call)
    {
        JNIEnv *pEnvironment = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
        jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());

        if (pEnvironment == nullptr || activity == nullptr)
        {
            return;
        }

        jclass activityClass = pEnvironment->GetObjectClass(activity);
        jmethodID method = pEnvironment->GetStaticMethodID(activityClass, pName, pSignature);

        if (method != nullptr)
        {
            call(pEnvironment, activityClass, method);
        }

        if (pEnvironment->ExceptionCheck())
        {
            pEnvironment->ExceptionClear();
        }

        pEnvironment->DeleteLocalRef(activityClass);
        pEnvironment->DeleteLocalRef(activity);
    }

    static std::string callString(const char *pName, const char *pSignature)
    {
        std::string result;
        withActivity(pName, pSignature,
                     [&result](JNIEnv *pEnvironment, jclass activityClass, jmethodID method)
                     {
                         jstring value = static_cast<jstring>(pEnvironment->CallStaticObjectMethod(activityClass, method));

                         if (value != nullptr)
                         {
                             const char *pChars = pEnvironment->GetStringUTFChars(value, nullptr);
                             result = pChars != nullptr ? pChars : "";
                             pEnvironment->ReleaseStringUTFChars(value, pChars);
                             pEnvironment->DeleteLocalRef(value);
                         }
                     });
        return result;
    }

    void callWithUrl(const char *pName) const
    {
        withActivity(pName, "(Ljava/lang/String;)V",
                     [this](JNIEnv *pEnvironment, jclass activityClass, jmethodID method)
                     {
                         jstring url = pEnvironment->NewStringUTF(m_baseUrl.c_str());
                         pEnvironment->CallStaticVoidMethod(activityClass, method, url);
                         pEnvironment->DeleteLocalRef(url);
                     });
    }

    std::string m_baseUrl;
};
#endif
}

std::unique_ptr<GameUpdater> GameUpdater::create(const std::string &baseUrl)
{
#if defined(__ANDROID__)
    // Development APKs bundle their content and take no updates.
    std::unique_ptr<GameUpdater> updater = std::make_unique<AndroidUpdater>(baseUrl);
    return updater->installedVersion().empty() ? nullptr : std::move(updater);
#else
    return createInstallationUpdater(baseUrl);
#endif
}

int runHeadlessUpdate(const std::string &baseUrl, std::ostream &log)
{
    std::unique_ptr<InstallationUpdater> updater = createInstallationUpdater(baseUrl);

    if (!updater)
    {
        log << "Update: this installation does not update itself\n";
        return 2;
    }

    log << "Update: OpenYAMM " << updater->installedVersion() << ", checking " << baseUrl << '\n';
    updater->check();
    updater->wait();
    Engine::UpdateStatus status = updater->status();
    log << "Update: " << status.message << '\n';

    if (status.state == Engine::UpdateState::UpToDate)
    {
        return 0;
    }

    if (status.state != Engine::UpdateState::Available)
    {
        return 1;
    }

    log << "Update: downloading " << status.packCount << " content packs" << (status.contentOnly ? "" : " and the app")
        << ", " << status.totalBytes << " bytes\n";
    updater->startUpdate();
    updater->wait();
    status = updater->status();
    log << "Update: " << status.message << " (" << status.doneBytes << " of " << status.totalBytes << " bytes)\n";
    std::string error;

    if (status.state != Engine::UpdateState::Ready || !updater->activate(error))
    {
        log << "Update: failed " << error << '\n';
        return 1;
    }

    log << (status.contentOnly ? "Update: the content of OpenYAMM " + status.release + " is installed\n"
                               : "Update: OpenYAMM " + status.release + " is now current\n");
    return 0;
}
}
