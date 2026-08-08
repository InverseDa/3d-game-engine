#include "CoreMinimal.h"
#include "Application/EngineLoop.h"
#include "DemoApplication/DemoApplication.h"

namespace LE
{

int32 GuardedMain()
{
    LE_INIT()

    int32 ExitCode = -1;
    {
        FApplicationPtr Application = CreateDemoApplication();
        if (Application)
        {
            FEngineLoop EngineLoop;
            if (EngineLoop.Initialize(*Application) == EEngineLoopInitializeResult::Success)
            {
                while (EngineLoop.Tick() == EEngineLoopTickResult::Continue)
                {
                }
                ExitCode = 0;
            }
            EngineLoop.Shutdown();
        }
    }

    LE_SHUTDOWN()
    return ExitCode;
}

}

#if PLATFORM_WINDOWS
    #include <Windows.h>

LE::int32 WINAPI WinMain(_In_ HINSTANCE hInInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ char* pCmdLine, _In_ LE::int32 nCmdShow)
{
    return LE::GuardedMain();
}
#else
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return LE::GuardedMain();
}
#endif
