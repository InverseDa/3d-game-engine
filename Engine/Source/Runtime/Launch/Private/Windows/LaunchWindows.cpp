#include "CoreMinimal.h"
#include <Windows.h>

int32 GuardedMain();

int32 WINAPI WinMain(_In_ HINSTANCE hInInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ char* pCmdLine, _In_ int32 nCmdShow)
{
    return GuardedMain();
}

LE_DECLARE_LOG_CATEGORY_EXTERN(LogXBD);
LE_DECLARE_LOG_CATEGORY(LogXBD);

int32 GuardedMain()
{
    LE_INIT()
    LE_LOG(LogXBD, Info, "Welcome to le!")
    LE_SHUTDOWN()
    return 0;
}