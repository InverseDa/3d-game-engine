#include "Source/Runtime/Core/Public/Logger/Log.h"

LE_DECLARE_LOG_CATEGORY_EXTERN(LogXBD);
LE_DECLARE_LOG_CATEGORY(LogXBD);

int main()
{
    LE_INIT()
    LE_LOG(LogXBD, Info, "Welcome to le!")
    LE_SHUTDOWN()
    return 0;
}