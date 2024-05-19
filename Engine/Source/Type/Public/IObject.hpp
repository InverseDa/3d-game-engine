#ifndef ENGINE_IOBJECT_HPP
#define ENGINE_IOBJECT_HPP

class IObject {
  public:
    virtual void Init() = 0;
    virtual void Destroy() = 0;
    virtual ~IObject() = default;
};

#endif // ENGINE_IOBJECT_HPP
