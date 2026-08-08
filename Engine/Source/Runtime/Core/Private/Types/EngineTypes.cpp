#include "Types/EngineTypes.h"

#include <ostream>

namespace LE
{
std::ostream& operator<<(std::ostream& Stream, const String& Value)
{
    Stream.write(Value.Data(), static_cast<std::streamsize>(Value.Size()));
    return Stream;
}
} // namespace LE
