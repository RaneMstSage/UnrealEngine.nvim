#include "NeovimSourceCodeAccessor.h"
#include "HAL/PlatformProcess.h"
#include "Internationalization/Internationalization.h"
#if PLATFORM_LINUX
#include "Linux/LinuxPlatformProcess.h"
#elif PLATFORM_WINDOWS
#include "Windows/WindowsPlatformProcess.h"
#elif PLATFORM_MAC
#include "Mac/MacPlatformProcess.h"
#endif
#include "Logging/LogMacros.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogNeovimSourceCodeAccess, Log, All);

#define LOCTEXT_NAMESPACE "NeovimSourceCodeAccessor"
bool FNeovimSourceCodeAccessor::CanAccessSourceCode() const
{
    return true;
}

FName FNeovimSourceCodeAccessor::GetFName() const
{
    return FName("NeovimSourceCodeAccessor");
}

FText FNeovimSourceCodeAccessor::GetNameText() const
{
    return LOCTEXT("NeovimDisplayName", "Neovim");
}

FText FNeovimSourceCodeAccessor::GetDescriptionText() const
{
    return LOCTEXT("NeovimDisplayDesc", "Open source code files in Neovim");
}

bool FNeovimSourceCodeAccessor::OpenSolution()
{
    FString Arguments = FString::Printf(TEXT("\":Ex %s<CR>\""), *CurrentWorkingDirectory);
    return NeovimExecute(TEXT("remote-send"), *Arguments);
}

bool FNeovimSourceCodeAccessor::OpenSolutionAtPath(const FString& InSolutionPath)
{
    FString Path = FPaths::GetPath(InSolutionPath);
    FString Arguments = FString::Printf(TEXT("\":Ex %s<CR>\""), *Path);
    return NeovimExecute(TEXT("remote-send"), *Arguments);
}

bool FNeovimSourceCodeAccessor::DoesSolutionExist() const
{
    return FPaths::DirectoryExists(CurrentWorkingDirectory);
}

bool FNeovimSourceCodeAccessor::OpenFileAtLine(const FString& FullPath, int32 LineNumber, int32 ColumnNumber)
{
    if (FullPath.IsEmpty())
        return false;

    // Use remote-send with :edit command instead of --remote for better Windows compatibility
    // Escape backslashes and spaces for Vimscript
    FString EscapedPath = FullPath.Replace(TEXT("\\"), TEXT("/"));
    // Escape spaces with backslash for vim command line
    EscapedPath = EscapedPath.Replace(TEXT(" "), TEXT("\\ "));

    FString VimCommand;
    if (LineNumber > 0)
    {
        if (ColumnNumber > 0)
        {
            // :edit +line file then move to column
            VimCommand = FString::Printf(TEXT("\"<C-\\><C-n>:edit +%d %s<CR>:normal! %d|<CR>\""), LineNumber, *EscapedPath, ColumnNumber);
        }
        else
        {
            VimCommand = FString::Printf(TEXT("\"<C-\\><C-n>:edit +%d %s<CR>\""), LineNumber, *EscapedPath);
        }
    }
    else
    {
        VimCommand = FString::Printf(TEXT("\"<C-\\><C-n>:edit %s<CR>\""), *EscapedPath);
    }

    UE_LOG(LogNeovimSourceCodeAccess, Log, TEXT("OpenFileAtLine: %s Line:%d Col:%d -> %s"), *FullPath, LineNumber, ColumnNumber, *VimCommand);
    return NeovimExecute(TEXT("remote-send"), *VimCommand);
}

bool FNeovimSourceCodeAccessor::OpenSourceFiles(const TArray<FString>& AbsoluteSourcePaths)
{
    auto files = AbsoluteSourcePaths.Num();
    if (files == 0)
        return false;

    FString Arguments;
    for (const FString& Path : AbsoluteSourcePaths)
    {
        if (!Arguments.IsEmpty())
            Arguments += TEXT(" ");

        Arguments += FString::Printf(TEXT("\"%s\""), *Path);
    }

    return NeovimExecute(TEXT("remote"), *Arguments);
}

bool FNeovimSourceCodeAccessor::AddSourceFiles(const TArray<FString>& AbsoluteSourcePaths, const TArray<FString>& AvailableModules)
{
    return false;
}

bool FNeovimSourceCodeAccessor::SaveAllOpenDocuments() const
{
    const FString Arguments = FString::Printf(TEXT("\":wa<CR>\""));
    return NeovimExecute(TEXT("remote-send"), *Arguments);
}

void FNeovimSourceCodeAccessor::Tick(const float DeltaTime)
{
}

bool FNeovimSourceCodeAccessor::NeovimExecute(const TCHAR* Command, const TCHAR* Arguments) const
{
    if (!RemoteServer.IsEmpty())
    {
#if PLATFORM_WINDOWS
        // On Windows, use ExecProcess directly without cmd.exe to avoid shell quoting issues
        // Don't quote the server path - ExecProcess handles argument passing correctly
        FString RemoteArgs = FString::Printf(TEXT("--server %s --%s %s"), *RemoteServer, Command, Arguments);
        UE_LOG(LogNeovimSourceCodeAccess, Log, TEXT("Executing: %s %s"), *Application, *RemoteArgs);

        int32 ReturnCode = 0;
        FString StdOut;
        FString StdErr;

        bool bSuccess = FPlatformProcess::ExecProcess(
            *Application,
            *RemoteArgs,
            &ReturnCode,
            &StdOut,
            &StdErr);

        if (bSuccess && ReturnCode == 0)
        {
            UE_LOG(LogNeovimSourceCodeAccess, Log, TEXT("Successfully executed nvim remote command"));
            return true;
        }
        else
        {
            UE_LOG(LogNeovimSourceCodeAccess, Error, TEXT("nvim failed: code=%d stdout=%s stderr=%s"), ReturnCode, *StdOut, *StdErr);
        }
#else
        FString RemoteArgs = FString::Printf(TEXT("--server \"%s\" --%s %s"), *RemoteServer, Command, Arguments);
        // On Linux/Mac, ExecProcess with --remote typically returns quickly
        bool bSuccess = FPlatformProcess::ExecProcess(
            *Application,
            *RemoteArgs,
            nullptr,
            nullptr,
            nullptr);

        if (bSuccess)
        {
            UE_LOG(LogNeovimSourceCodeAccess, Log, TEXT("%s: %s %s"), *RemoteServer, *Application, *RemoteArgs);
            return true;
        }
#endif
    }

    UE_LOG(LogNeovimSourceCodeAccess, Warning, TEXT("Failed to communicate with Neovim, try launching UE via UnrealEngine.nvim"));
    return false;
}
#undef LOCTEXT_NAMESPACE
