#ifndef ENGINE_MANAGERBASE_HPP
#define ENGINE_MANAGERBASE_HPP

class ManagerBase {
  public:
    virtual void Init() = 0;
    virtual void Cleanup() = 0;
    virtual void Update() = 0;
    virtual ~ManagerBase() = default;

  private:

};

#endif // ENGINE_MANAGERBASE_HPP
