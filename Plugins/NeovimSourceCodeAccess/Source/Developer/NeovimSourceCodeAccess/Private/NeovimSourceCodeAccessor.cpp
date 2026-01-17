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

#if PLATFORM_WINDOWS
    // On Windows, use --remote-send with :tabedit command to avoid argument parsing issues
    // and open in a new tab so user's current work isn't displaced
    // Convert backslashes to forward slashes (vim handles these on Windows)
    FString VimPath = FullPath.Replace(TEXT("\\"), TEXT("/"));
    // Escape spaces for vim command line
    VimPath = VimPath.Replace(TEXT(" "), TEXT("\\ "));

    FString VimCommand;
    if (LineNumber > 0)
    {
        // <C-\><C-n> ensures normal mode, then :tabedit +line path
        VimCommand = FString::Printf(TEXT("\"<C-\\\\><C-n>:tabedit +%d %s<CR>\""), LineNumber, *VimPath);
        if (ColumnNumber > 0)
        {
            // Add cursor positioning after file loads
            VimCommand = FString::Printf(TEXT("\"<C-\\\\><C-n>:tabedit +%d %s<CR>:call cursor(%d,%d)<CR>\""),
                LineNumber, *VimPath, LineNumber, ColumnNumber);
        }
    }
    else
    {
        VimCommand = FString::Printf(TEXT("\"<C-\\\\><C-n>:tabedit %s<CR>\""), *VimPath);
    }

    return NeovimExecute(TEXT("remote-send"), *VimCommand);
#else
    FString Arguments;
    if (LineNumber > 0)
    {
        if (ColumnNumber > 0)
        {
            Arguments = FString::Printf(TEXT("+%d:%d \"%s\""), LineNumber, ColumnNumber, *FullPath);
        }
        else
        {
            Arguments = FString::Printf(TEXT("+%d \"%s\""), LineNumber, *FullPath);
        }
    }
    else
    {
        Arguments = FString::Printf(TEXT("\"%s\""), *FullPath);
    }

    return NeovimExecute(TEXT("remote"), *Arguments);
#endif
}

bool FNeovimSourceCodeAccessor::OpenSourceFiles(const TArray<FString>& AbsoluteSourcePaths)
{
    auto files = AbsoluteSourcePaths.Num();
    if (files == 0)
        return false;

#if PLATFORM_WINDOWS
    // On Windows, use --remote-send with :edit commands to avoid argument parsing issues
    FString VimCommands = TEXT("\"<C-\\\\><C-n>");
    for (const FString& Path : AbsoluteSourcePaths)
    {
        FString VimPath = Path.Replace(TEXT("\\"), TEXT("/"));
        VimPath = VimPath.Replace(TEXT(" "), TEXT("\\ "));
        VimCommands += FString::Printf(TEXT(":edit %s<CR>"), *VimPath);
    }
    VimCommands += TEXT("\"");

    return NeovimExecute(TEXT("remote-send"), *VimCommands);
#else
    FString Arguments;
    for (const FString& Path : AbsoluteSourcePaths)
    {
        if (!Arguments.IsEmpty())
            Arguments += TEXT(" ");

        Arguments += FString::Printf(TEXT("\"%s\""), *Path);
    }

    return NeovimExecute(TEXT("remote"), *Arguments);
#endif
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
        FString RemoteArgs = FString::Printf(TEXT("--server \"%s\" --%s %s"), *RemoteServer, Command, Arguments);

#if PLATFORM_WINDOWS
        // On Windows, use CreateProc for non-blocking execution
        // ExecProcess blocks until the process exits, which freezes Unreal
        FProcHandle ProcHandle = FPlatformProcess::CreateProc(
            *Application,
            *RemoteArgs,
            true,   // bLaunchDetached - run independently of Unreal
            true,   // bLaunchHidden - no console window flash
            false,  // bLaunchReallyHidden
            nullptr, // OutProcessID
            0,      // PriorityModifier
            nullptr, // OptionalWorkingDirectory
            nullptr  // PipeWriteChild
        );

        if (ProcHandle.IsValid())
        {
            // Close the handle immediately - we don't need to track the process
            FPlatformProcess::CloseProc(ProcHandle);
            UE_LOG(LogNeovimSourceCodeAccess, Log, TEXT("%s: %s %s"), *RemoteServer, *Application, *RemoteArgs);
            return true;
        }
#else
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
