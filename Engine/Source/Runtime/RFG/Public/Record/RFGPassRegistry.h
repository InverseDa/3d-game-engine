#pragma once

#include "CoreMinimal.h"
#include "Core/RFGTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

class FRFGPassContext;

struct FRFGPassParameterDesc
{
    FString Name;
    FString TypeName;
    bool bRequired = true;
};

struct FRFGPassSchema
{
    FString PassTypeName;
    ERFGQueueType PreferredQueue = ERFGQueueType::Graphics;
    ERFGPassFlags DefaultFlags = ERFGPassFlags::None;
    std::vector<FRFGPassParameterDesc> Parameters;
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
    virtual bool UnregisterPassType(const FString& PassTypeName) = 0;

public:
    virtual bool HasPassType(const FString& PassTypeName) const = 0;
    virtual const FRFGRegisteredPassType* FindPassType(const FString& PassTypeName) const = 0;
};

class RFG_API FRFGPassRegistry final : public IRFGPassRegistry
{
public:
    FRFGPassRegistry() = default;
    ~FRFGPassRegistry() override = default;

public:
    bool RegisterPassType(const FRFGRegisteredPassType& InPassType) override;
    bool UnregisterPassType(const FString& PassTypeName) override;

public:
    bool HasPassType(const FString& PassTypeName) const override;
    const FRFGRegisteredPassType* FindPassType(const FString& PassTypeName) const override;

private:
    std::unordered_map<std::string, FRFGRegisteredPassType> RegisteredPassTypes;
};
