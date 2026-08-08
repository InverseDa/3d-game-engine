#include "CoreMinimal.h"

namespace LE
{
int32 GuardedMain();
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
