#include "CoreMinimal.h"

int32 GuardedMain();

#if PLATFORM_WINDOWS
    #include <Windows.h>

int32 WINAPI WinMain(_In_ HINSTANCE hInInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ char* pCmdLine, _In_ int32 nCmdShow)
{
    return GuardedMain();
}
#else
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return GuardedMain();
}
#endif
