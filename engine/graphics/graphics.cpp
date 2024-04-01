#include "graphics.hpp"
#include "core/context.hpp"

namespace ida {
Graphics::~Graphics() {
    renderer_.reset();
    window_.reset();
    //    gameObjects_.clear();
    /**
     * @Warning: Context::Quit() must be called after all other game objects are destroyed
     */
    //    Context::Quit();
}

} // namespace ida