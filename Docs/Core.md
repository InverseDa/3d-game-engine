# Core 模块设计文档

> 架构基线：Core 类型、allocator、容器、namespace、ownership/DLL 与 STL 使用边界以
> [ADR-0001: P0 Modern C++ Language, Core Types, and Containers](ADR/0001-modern-cpp-core-types-and-containers.md)
> 为准。C4 已移除 GLM，C5 已迁移 Runtime owning data，C6 已将非 ThirdParty 引擎
> 声明统一迁入直接根命名空间 `LE`；不提供全局 compatibility aliases。

> C1-C3 已提供 allocator、基础容器和常用 ownership utilities：`LE::IAllocator`、
> `LE::Array`、`LE::StaticArray`、`LE::Span`、`LE::HashMap`、`LE::HashSet`、
> `LE::String`/`StringView`、`LE::UniquePtr`、`LE::SharedPtr`/`WeakPtr`、`LE::Function`。其 allocation failure、
> 跨 DLL 释放、relocation、hash policy、iteration/invalidation 和 borrowed lifetime 契约见
> [Core allocation and containers](CoreContainers.md) 与
> [Core ownership and callable utilities](CoreOwnership.md)。

## 1. 模块职责与定位

Core 是引擎最底层的模块，为所有上层模块提供基础类型、数学运算、日志记录等通用能力。该模块**不依赖任何其他引擎模块**，仅通过 `Build.ts` 引入一个第三方库：

- **spdlog**：日志后端

GLM、`LE_USE_GLM` define 和 include path 已在 C4 从 Runtime/Core 退出。
`CoreMinimal.h` 是引擎代码的统一最小包含头文件，聚合 Core containers、math、ownership、
logger 与基础类型。

---

## 2. 核心类型系统

### 2.1 数值类型别名

在 `EngineTypes.h` 中定义了固定宽度的数值别名，屏蔽平台差异：

| 别名 | 实际类型 | 说明 |
|------|----------|------|
| `LE::int8` | `std::int8_t` | 8-bit 有符号整数 |
| `LE::int32` | `std::int32_t` | 32-bit 有符号整数 |
| `LE::int64` | `std::int64_t` | 64-bit 有符号整数 |
| `LE::uint8` | `std::uint8_t` | 8-bit 无符号整数 |
| `LE::uint32` | `std::uint32_t` | 32-bit 无符号整数 |
| `LE::uint64` | `std::uint64_t` | 64-bit 无符号整数 |
| `LE::float32` | `float` | 32-bit 浮点数 |
| `LE::float64` | `double` | 64-bit 浮点数 |

同时定义了两个常用浮点阈值宏：
- `LE_SMALL_NUMBER` (`1e-8`)
- `LE_KINDA_SMALL_NUMBER` (`1e-4`)

以及平台无关的强制内联宏 `FORCE_INLINE`。

### 2.2 String

`LE::String` 是自有 allocator-backed UTF-8 byte string，`LE::StringView` 是不拥有的 byte
view。实现不持有或公开 `std::string` storage；旧全局 `FString` 名称已在 C6 删除。

**主要接口：**
- owned/view 构造、拷贝/移动，以及 embedded NUL byte 支持
- `Data()`/`GetData()`/`operator*()` 始终提供 trailing-NUL pointer
- `TryReserve`/`TryAssign`/`TryAppend` 与 ordinary OOM path
- overlap/self-alias safe assign/append
- 与 `StringView` 一致的 transparent hash/equality

---

## 3. 数学库设计

稳定实现位于 `LE::Math`，提供自有 `Vector<T, N>`、rectangular
`Matrix<T, R, C>` 与 `Quaternion<T>` 值类型，不包装或转换 GLM。右手坐标系、basis/forward、
row-major storage、column-vector multiplication、composition order、quaternion `xyzw`/Hamilton、
radians 与 layout/alignment 契约由数值和 compile-time tests 锁定，详见
[Core Math Conventions](CoreMath.md)。

旧 `FMath::T*` 和全局 `FVector*`/`FMatrix*`/`FQuaternion*` 名称已在 C6 删除。

---

## 4. 日志系统

日志系统是对 **spdlog** 的轻量级封装，位于 `LE` 命名空间。

### 4.1 日志级别

引擎定义了五级日志枚举 `LogLevel`：
`Trace` < `Info` < `Warn` < `Error` < `Fatal`

内部通过 `LogLevelToSpdlog()` 映射到 spdlog 的 `level_enum`。

### 4.2 Log 类

```cpp
class Log
{
    static void Init();
    static spdlog::logger* GetCoreLogger();
    static spdlog::logger* GetLoggerOrCreate(LE::StringView Name);
};
```

- `Init()` 创建双 Sink 输出：控制台（带 ANSI 颜色）与文件 `Firefly.log`（追加模式）。
- Windows 平台下会主动启用控制台 `ENABLE_VIRTUAL_TERMINAL_PROCESSING` 以支持 ANSI 颜色码。
- 自定义了三个 `spdlog::custom_flag_formatter`：`LevelTagFormatter`（级别标签）、`LevelAnsiBeginFormatter` / `LevelAnsiEndFormatter`（Warning 以上标黄，Error/Fatal 标红）。
- `GetLoggerOrCreate()` 支持按名称获取或懒创建分类 Logger，所有 Logger 共享同一组 Sink。

### 4.3 使用宏

| 宏 | 说明 |
|----|------|
| `LE_INIT()` | 初始化日志系统 |
| `LE_DECLARE_LOG_CATEGORY_EXTERN(CategoryName)` | 外部声明日志分类 |
| `LE_DECLARE_LOG_CATEGORY(CategoryName)` | 定义并创建日志分类 |
| `LE_LOG(CategoryName, Level, ...)` | 输出日志，Level 为 Trace/Info/Warn/Error/Fatal |
| `LE_SHUTDOWN()` | 关闭 spdlog |

示例：
```cpp
LE_DECLARE_LOG_CATEGORY(MyLog);
LE_LOG(MyLog, Info, "Hello {}", "World");
```

---

## 5. LE::FNonCopyable 工具基类

`LE::FNonCopyable` 是一个极简的工具基类，用于禁止派生类的拷贝语义，但保留移动语义：

```cpp
namespace LE
{
class FNonCopyable
{
public:
    FNonCopyable() = default;
    virtual ~FNonCopyable() = default;

    FNonCopyable(const FNonCopyable&) = delete;
    FNonCopyable& operator=(const FNonCopyable&) = delete;

    FNonCopyable(FNonCopyable&&) = default;
    FNonCopyable& operator=(FNonCopyable&&) = default;
};
}
```

继承该基类即可使类型不可拷贝、可移动，常用于单例、资源句柄等场景。

---

## 6. 公开 API 简要说明

| 文件 | 类型/宏 | 说明 |
|------|---------|------|
| `CoreMinimal.h` | 统一头文件 | 包含 Log 与 EngineTypes |
| `Containers/HashMap.h` | `LE::HashMap` / `DefaultHash` | 自有 storage 的开放寻址哈希表与 policy |
| `Containers/HashSet.h` | `LE::HashSet` | 与 HashMap 共用 probing/load/failure 契约的集合 |
| `Containers/String.h` | `LE::String` / `StringHash` / `StringEqual` | UTF-8 byte owned storage 与 transparent policies |
| `Containers/StringView.h` | `LE::StringView` | 非拥有 byte view，不保证 NUL 结尾 |
| `Memory/UniquePtr.h` | `LE::UniquePtr` | creator-side destroy route 的唯一 ownership |
| `Memory/SharedPtr.h` | `LE::SharedPtr` / `LE::WeakPtr` | 自有 control block 的共享/观察 ownership |
| `Templates/Function.h` | `LE::Function` | allocator-backed copyable callable type erasure |
| `Math/Vector.h` | `LE::Math::Vector` | 自有浮点向量值类型 |
| `Math/Matrix.h` | `LE::Math::Matrix` | row-major、column-vector rectangular matrix |
| `Math/Quaternion.h` | `LE::Math::Quaternion` | xyzw Hamilton quaternion，radians |
| `Types/EngineTypes.h` | `LE::int8` 等数值别名 / `LE::FNonCopyable` / `FORCE_INLINE` | 跨平台基础类型与编译器宏 |
| `Logger/Log.h` | `LE::Log` / `LE::LogLevel` | 日志管理与级别枚举 |
| `Logger/Log.h` | `LE_INIT/LE_LOG/LE_SHUTDOWN` 等 | 日志使用宏 |

---

## 7. 已知不足与待扩展项

当前 Core 模块提供了引擎启动所需的最小能力集，但以下基础设施尚未实现或仍需完善：

1. **治理闭环**：C1-C6 已完成 Core primitives、Runtime owning data 与 namespace 迁移；
   后续由 C7 自动检查禁止回归。
2. **文件系统**：无 `IFileManager` 或路径/文件操作抽象，日志文件路径目前硬编码为 `Firefly.log`。
3. **allocator 扩展**：C1 已建立 `LE::IAllocator`、默认 allocator、集中 allocation helpers
   与统一 OOM policy；arena/pool/tracking allocator 仅在真实消费者出现后增加窄 adapter。
4. **数学库扩展**：C4 只建立 Transform/camera 前所需的基础数值契约；行列式、通用 matrix
   inverse 及 Vulkan projection builders 应在出现真实消费者时增加并补 numerical tests。
5. **平台抽象**：仅日志系统包含 Windows 控制台处理，缺少通用的平台检测、线程、时间、原子操作等封装。
6. **配置与反射**：无命令行参数解析、无属性/反射基础设施。

基础设施治理采用 ADR-0001 的固定顺序：allocator/连续容器 → 哈希容器 → String 与按需
ownership utilities → GLM 退出与数学约定 → 分模块 owning data migration → namespace
migration → enforcement。迁移期间不得引入第二套同义容器或 `Limitless` namespace alias。

---

## 附录：模块构建配置

`Build.ts`（LimitlessBuilder）声明模块依赖：

```ts
Configuration.PublicDependencies.push("Spdlog");
```
