#pragma once

#include "CoreMinimal.h"
#include "Core/RFGTypes.h"


namespace LE
{

class FRFGPassContext;

struct FRFGPassParameterDesc
{
    LE::String Name;
    LE::String TypeName;
    bool bRequired = true;
};

struct FRFGPassSchema
{
    LE::String PassTypeName;
    ERFGQueueType PreferredQueue = ERFGQueueType::Graphics;
    ERFGPassFlags DefaultFlags = ERFGPassFlags::None;
    LE::Array<FRFGPassParameterDesc> Parameters;
};

class RFG_API IRFGPassExecutor
{
public:
    virtual ~IRFGPassExecutor() = default;
    virtual void Execute(FRFGPassContext& Context, const FRFGPassParameterBlock& ParameterBlock) = 0;
};

struct FRFGRegisteredPassType
{
    FRFGPassSchema Schema;
    IRFGPassExecutor* Executor = nullptr;
};

class RFG_API IRFGPassRegistry
{
public:
    virtual ~IRFGPassRegistry() = default;

public:
    virtual bool RegisterPassType(const FRFGRegisteredPassType& InPassType) = 0;
    virtual bool UnregisterPassType(const LE::String& PassTypeName) = 0;

public:
    virtual bool HasPassType(const LE::String& PassTypeName) const = 0;
    virtual const FRFGRegisteredPassType* FindPassType(const LE::String& PassTypeName) const = 0;
};

class RFG_API FRFGPassRegistry final : public IRFGPassRegistry
{
public:
    FRFGPassRegistry() = default;
    ~FRFGPassRegistry() override = default;

public:
    bool RegisterPassType(const FRFGRegisteredPassType& InPassType) override;
    bool UnregisterPassType(const LE::String& PassTypeName) override;

public:
    bool HasPassType(const LE::String& PassTypeName) const override;
    const FRFGRegisteredPassType* FindPassType(const LE::String& PassTypeName) const override;

private:
    LE::HashMap<LE::String, FRFGRegisteredPassType> RegisteredPassTypes;
};

} // namespace LE
