#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../MarvelRivalsUTOCSignatureBypass/GameHooks.h"
#include "../MarvelRivalsUTOCSignatureBypass/IntroSkip.h"
#include "Test.h"

using namespace bypass;
namespace fs = std::filesystem;

namespace
{
    constexpr const wchar_t* LogoVideos = L"Marvel/Content/Marvel/MoviesBink/Movies/MarvelLogo/";

    // The hooks stay for the rest of the run; they only affect MarvelLogo paths, which no other test touches.
    void InstallHooksOnce()
    {
        static const bool installed = InstallIntroSkip();
        CHECK(installed);
    }

    bool ExistsUnhooked(const fs::path& path)
    {
        WIN32_FIND_DATAW found{};
        const HANDLE search = FindFirstFileW(path.c_str(), &found);
        if (search == INVALID_HANDLE_VALUE) return false;
        FindClose(search);
        return true;
    }

    // A real logo video and a real lobby video on disk, so a refusal can only come from the hook.
    class VideoFolder
    {
    public:
        VideoFolder()
            : root_(fs::temp_directory_path() / ("mrusb-IntroSkip-" + std::to_string(_getpid())))
        {
            RemoveLogoVideo();
            fs::remove_all(root_);
            fs::create_directories(LogoVideo().parent_path().parent_path());
            fs::create_directories(LobbyVideo().parent_path());
            std::ofstream(LobbyVideo(), std::ios::binary) << "BK2";

            // Written under a neutral name and moved in, because the hook may already refuse to create it in place.
            const fs::path staged = root_ / L"staged.bk2";
            std::ofstream(staged, std::ios::binary) << "BK2";
            CreateDirectoryW(LogoVideo().parent_path().c_str(), nullptr);
            MoveFileExW(staged.c_str(), LogoVideo().c_str(), MOVEFILE_REPLACE_EXISTING);
            CHECK(ExistsUnhooked(LogoVideo()));
        }
        ~VideoFolder()
        {
            RemoveLogoVideo();
            std::error_code ignored;
            fs::remove_all(root_, ignored);
        }
        VideoFolder(const VideoFolder&) = delete;
        VideoFolder& operator=(const VideoFolder&) = delete;

        [[nodiscard]] fs::path LogoVideo() const { return root_ / LogoVideos / L"COMMON" / L"MarvelLogoVideo_Common.bk2"; }
        [[nodiscard]] fs::path LobbyVideo() const { return root_ / L"Marvel/Content/Marvel/MoviesBink/Movies/LoginAndLobby/Lobby.bk2"; }

    private:
        // DeleteFileW and RemoveDirectoryW are not hooked, unlike the CreateFileW that std::filesystem would use.
        void RemoveLogoVideo() const
        {
            DeleteFileW(LogoVideo().c_str());
            RemoveDirectoryW(LogoVideo().parent_path().c_str());
        }

        fs::path root_;
    };

    bool OpensWith(decltype(&CreateFileW) createFile, const fs::path& path)
    {
        const HANDLE file = createFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        CloseHandle(file);
        return true;
    }
}

TEST(IntroVideosMatchTheRealLogoFiles)
{
    CHECK(IsIntroVideo(L"../../../Marvel/Content/Marvel/MoviesBink/Movies/MarvelLogo/CN/NeteaseLogoVideo_CN.bk2"));
    CHECK(IsIntroVideo(L"../../../Marvel/Content/Marvel/MoviesBink/Movies/MarvelLogo/COMMON/MarvelLogoVideo_Common.bk2"));
    CHECK(IsIntroVideo(L"../../../Marvel/Content/Marvel/MoviesBink/Movies/MarvelLogo/COMMON/UnrealLogoVideo_Common.bk2"));
    CHECK(IsIntroVideo(L"../../../Marvel/Content/Marvel/MoviesBink/Movies/MarvelLogo/HK_MO_TW/NeteaseLogoVideo_HK_MO_TW.bk2"));
}

TEST(IntroVideosMatchEitherSlashAnyCaseAndAbsolutePaths)
{
    CHECK(IsIntroVideo(L"D:\\Games\\MarvelRivals\\MarvelGame\\Marvel\\Content\\Marvel\\MoviesBink\\Movies\\MarvelLogo\\OTHER\\Logo.bk2"));
    CHECK(IsIntroVideo(L"d:/games/marvelgame/marvel/content/marvel/MOVIESBINK/movies/marvellogo/jp_kr/logo.bk2"));
    CHECK(IsIntroVideo(L"..\\..\\..\\Marvel/Content/Marvel\\MoviesBink/Movies\\MarvelLogo/COMMON\\MarvelLogoVideo_Common.bk2"));
    CHECK(IsIntroVideo(L"\\\\?\\D:\\Games\\Marvel\\Content\\Marvel\\MoviesBink\\Movies\\MarvelLogo\\CN\\Logo.bk2"));
    CHECK(IsIntroVideo(L"MoviesBink/Movies/MarvelLogo/COMMON/UnrealLogoVideo_Common.bk2"));
}

TEST(IntroVideosRejectLookAlikes)
{
    CHECK(!IsIntroVideo(L""));
    CHECK(!IsIntroVideo(L"../../../Marvel/Content/Marvel/MoviesBink/Movies/MarvelLogoX/COMMON/Logo.bk2"));
    CHECK(!IsIntroVideo(L"../../../Marvel/Content/Marvel/MoviesBink/Movies/MarvelLogo.bk2"));
    CHECK(!IsIntroVideo(L"../../../Marvel/Content/Marvel/MoviesBink/Movies/MarvelLogo"));
    CHECK(!IsIntroVideo(L"../../../Marvel/Content/Marvel/MoviesBink/Movies/LoginAndLobby/Lobby.bk2"));
    CHECK(!IsIntroVideo(L"../../../Marvel/Content/Marvel/MoviesBink/System/Loading.bk2"));
    CHECK(!IsIntroVideo(L"../../../Marvel/Content/Marvel/OldMoviesBink/Movies/MarvelLogo/COMMON/Logo.bk2"));
    CHECK(!IsIntroVideo(L"../../../Marvel/Content/Marvel/MoviesBink/Movies/Extra/MarvelLogo/Logo.bk2"));
    CHECK(!IsIntroVideo(L"MoviesBink/Movies/MarvelLog"));
}

TEST(SeenPathsCountsEachPathOnceIgnoringSlashAndCase)
{
    SeenPaths seen;
    CHECK(seen.Insert(L"Movies/MarvelLogo/COMMON/A.bk2"));
    CHECK(!seen.Insert(L"movies\\marvellogo\\common\\a.BK2"));
    CHECK(seen.Insert(L"Movies/MarvelLogo/COMMON/B.bk2"));
}

TEST(SeenPathsStopsCountingWhenFull)
{
    SeenPaths seen;
    for (std::size_t i = 0; i < SeenPaths::Capacity; ++i) CHECK(seen.Insert(std::to_wstring(i)));
    CHECK(!seen.Insert(L"one more"));
    CHECK(!seen.Insert(L"0"));
}

TEST(HookedFileCallsSeeLogoVideosAsMissing)
{
    const VideoFolder folder;
    InstallHooksOnce();
    const std::wstring logo = folder.LogoVideo().wstring();

    SetLastError(ERROR_SUCCESS);
    CHECK(!OpensWith(&CreateFileW, logo));
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);

    SetLastError(ERROR_SUCCESS);
    CHECK(CreateFile2(logo.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr) == INVALID_HANDLE_VALUE);
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);

    SetLastError(ERROR_SUCCESS);
    CHECK(GetFileAttributesW(logo.c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    SetLastError(ERROR_SUCCESS);
    CHECK(!GetFileAttributesExW(logo.c_str(), GetFileExInfoStandard, &attributes));
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    CHECK(!fs::exists(folder.LogoVideo()));
}

TEST(HookedFileCallsPassOtherVideosThrough)
{
    const VideoFolder folder;
    InstallHooksOnce();

    CHECK(OpensWith(&CreateFileW, folder.LobbyVideo()));
    CHECK(GetFileAttributesW(folder.LobbyVideo().c_str()) != INVALID_FILE_ATTRIBUTES);
    CHECK(fs::exists(folder.LobbyVideo()));
}

// Game code imports kernel32 and Bink or the CRT may import the api-ms file set; both must reach the KernelBase hook.
TEST(HookedFileCallsCatchKernel32AndApiSetCallers)
{
    const VideoFolder folder;
    InstallHooksOnce();

    const HMODULE apiSet = LoadLibraryW(L"api-ms-win-core-file-l1-1-0.dll");
    CHECK(apiSet != nullptr);
    for (const HMODULE module : { GetModuleHandleW(L"kernel32.dll"), apiSet })
    {
        const auto createFile = reinterpret_cast<decltype(&CreateFileW)>(GetProcAddress(module, "CreateFileW"));
        CHECK(createFile != nullptr);
        CHECK(!OpensWith(createFile, folder.LogoVideo()));
        CHECK(OpensWith(createFile, folder.LobbyVideo()));
    }
    FreeLibrary(apiSet);
}
