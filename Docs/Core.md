# Core 模块设计文档

## 1. 模块职责与定位

Core 是引擎最底层的模块，为所有上层模块提供基础类型、数学运算、日志记录等通用能力。该模块**不依赖任何其他引擎模块**，仅通过 `Build.ts` 引入两个第三方库：

- **spdlog**：日志后端
- **glm**（可选，由 `LE_USE_GLM` 宏控制）：备用数学库

`CoreMinimal.h` 是引擎代码的统一最小包含头文件，聚合了 `Logger/Log.h` 与 `Types/EngineTypes.h`。

---

## 2. 核心类型系统

### 2.1 数值类型别名

在 `EngineTypes.h` 中定义了固定宽度的数值别名，屏蔽平台差异：

| 别名 | 实际类型 | 说明 |
|------|----------|------|
| `int8` | `int8_t` | 8-bit 有符号整数 |
| `int32` | `int32_t` | 32-bit 有符号整数 |
| `int64` | `int64_t` | 64-bit 有符号整数 |
| `uint8` | `uint8_t` | 8-bit 无符号整数 |
| `uint32` | `uint32_t` | 32-bit 无符号整数 |
| `uint64` | `uint64_t` | 64-bit 无符号整数 |
| `float32` | `float` | 32-bit 浮点数 |
| `float64` | `double` | 64-bit 浮点数 |

同时定义了两个常用浮点阈值宏：
- `LE_SMALL_NUMBER` (`1e-8`)
- `LE_KINDA_SMALL_NUMBER` (`1e-4`)

以及平台无关的强制内联宏 `FORCE_INLINE`。

### 2.2 FString

`FString` 是引擎封装的字符串类，当前基于 `std::string` 实现（标注为临时方案）。

**主要接口：**
- 构造：`FString()`, `FString(const char*)`, `FString(const UWString&)`，支持拷贝/移动语义
- 数据访问：`operator*()`, `GetData()` —— 返回 `const char*`
- 查询：`IsEmpty()`, `Length()`
- 运算：`operator+`, `operator+=`, `operator==`, `operator!=`
- 流输出：`friend std::ostream& operator<<`

---

## 3. 数学库设计

数学库位于 `FMath` 命名空间下，采用 **Template + Union** 的设计，在编译期限定数据类型与维度，同时提供符合图形学惯例的命名访问。

### 3.1 TVector<T, N>

**模板约束：**
- `T` 必须为 `float32` 或 `float64`
- `N` 必须为 1~4（`static_assert` 限定）

**存储设计：**
通过 `TVectorData<T, N>` 做偏特化，内部使用 `union` 提供数组与语义化字段的别名访问：

- `N = 2`：`x, y` / `r, g` / `s, t` / `u, v`
- `N = 3`：`x, y, z` / `r, g, b` / `s, t, p`
- `N = 4`：`x, y, z, w` / `r, g, b, a`

**主要接口：**
- 构造：默认零初始化、标量填充（`explicit TVector(T Scalar)`）、变参构造（`TVector(Args... args)`）
- 索引：`operator[](int32 Index)`（带 `assert` 边界检查）
- 运算：`operator+=`, `operator-=`, `operator*=`（标量/分量）
- 向量运算：`Size()`, `SizeSqr()`, `Normalized()`, `Perp()`, `PerpCCW()`
- 静态方法：`Dot()`, `Cross()`（仅 N=3）, `Cross2D()`（仅 N=2）
- 自由函数：`operator+`, `operator-`, `operator*`（支持标量左右乘）

**预定义别名：**
`FVector2f`, `FVector3f`, `FVector4f`, `FVector2d`, `FVector3d`, `FVector4d`，默认 `FVector = FVector4f`。

### 3.2 TMatrix<T, N>

**模板约束：**
- `T` 必须为 `float32` 或 `float64`
- `N` 当前通过偏特化支持 2/3/4 维方阵

**存储设计：**
`TMatrixData<T, N>` 偏特化为 2/3/4 维，内部 `union` 提供：
- 一维数组 `Data[N * N]`
- 二维数组 `M[Row][Col]`
- 展平元素名 `m00, m01, ...`

**主要接口：**
- 构造：默认构造、`explicit TMatrix(T Scalar)`（填充单位矩阵并缩放）、从 `const T*` 拷贝
- 工厂方法：`Identity()`, `Zero()`
- 矩阵运算：`operator*(const TMatrix&)`（矩阵乘法）、`operator*(T Scalar)`、`operator*(const TVector<T, N>&)`（矩阵-向量乘）
- 变换：`Transposed()`, `SetIdentity(T Scale = 1)`
- 索引：`operator[](int32 RowIndex)` 返回行指针

**预定义别名：**
`FMatrix2f`, `FMatrix3f`, `FMatrix4f`, `FMatrix2d`, `FMatrix3d`, `FMatrix4d`。

### 3.3 TQuaternion<T>

**模板约束：**
- `T` 必须为 `float32` 或 `float64`

**存储设计：**
`TQuatData<T>` 使用 `union` 提供 `Data[4]` 与 `x, y, z, w` 别名。

**主要接口：**
- 构造：默认构造（单位四元数）、显式 `(x, y, z, w)`、轴角构造 `TQuaternion(const TVector<T, 3>& Axis, T AngleRad)`
- 工厂方法：`Identity()`
- 运算：`operator*(const TQuaternion&)`（哈密顿积）
- 向量旋转：`RotateVector(const TVector<T, 3>&)` / `operator*(const TVector<T, 3>&)`
- 归一化：`Normalize()`
- 逆：`Inverse()`（返回共轭，假设为单位四元数）

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
    static void Init();                                     // 初始化日志系统
    static std::shared_ptr<spdlog::logger>& GetCoreLogger();
    static std::shared_ptr<spdlog::logger> GetLoggerOrCreate(const std::string& Name);
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

## 5. FNonCopyable 工具基类

`FNonCopyable` 是一个极简的工具基类，用于禁止派生类的拷贝语义，但保留移动语义：

```cpp
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
```

继承该基类即可使类型不可拷贝、可移动，常用于单例、资源句柄等场景。

---

## 6. 公开 API 简要说明

| 文件 | 类型/宏 | 说明 |
|------|---------|------|
| `CoreMinimal.h` | 统一头文件 | 包含 Log 与 EngineTypes |
| `Types/EngineTypes.h` | 数值别名 / `FORCE_INLINE` | 跨平台基础类型与编译器宏 |
| `Types/EngineTypes.h` | `FString` | 引擎字符串封装 |
| `Types/EngineTypes.h` | `FMath::TVector` | N 维向量模板 |
| `Types/EngineTypes.h` | `FMath::TMatrix` | NxN 方阵模板 |
| `Types/EngineTypes.h` | `FMath::TQuaternion` | 四元数模板 |
| `Types/EngineTypes.h` | `FMath::Sqrt/Sin/Cos` | 基础数学函数转发 |
| `Logger/Log.h` | `LE::Log` / `LE::LogLevel` | 日志管理与级别枚举 |
| `Logger/Log.h` | `LE_INIT/LE_LOG/LE_SHUTDOWN` 等 | 日志使用宏 |

---

## 7. 已知不足与待扩展项

当前 Core 模块提供了引擎启动所需的最小能力集，但以下基础设施尚未实现或仍需完善：

1. **容器库**：缺少 `TArray`、`TMap`、`TSet` 等引擎级容器。当前字符串直接依赖 `std::string`，后续需评估是否引入自定义内存分配的容器体系。
2. **文件系统**：无 `IFileManager` 或路径/文件操作抽象，日志文件路径目前硬编码为 `Firefly.log`。
3. **内存分配器**：无自定义 `FMalloc` 或 `TMemory` 接口，所有类型当前使用默认 `new/delete`。
4. **数学库完整性**：
   - `TMatrix` 缺少行列式（`Determinant`）、逆矩阵（`Inverse`）、透视/正交投影构造等图形学常用操作。
   - `TQuaternion` 的 `Inverse()` 仅返回共轭，未做范数除法；`Normalize()` 的阈值硬编码为 `1e-8`，未使用引擎宏 `LE_SMALL_NUMBER`。
   - `TVector` 的 `explicit TVector(T Scalar)` 构造函数实现疑似存在笔误（`Scalar[i]` 对标量做下标访问）。
   - `TVector::operator[]` 的 `assert` 下界为 `0 < Index`，导致 `Index == 0` 会触发断言失败，应为 `0 <= Index`。
5. **平台抽象**：仅日志系统包含 Windows 控制台处理，缺少通用的平台检测、线程、时间、原子操作等封装。
6. **配置与反射**：无命令行参数解析、无属性/反射基础设施。

---

## 附录：模块构建配置

`Build.ts`（LimitlessBuilder）声明模块依赖：

```csharp
public class CoreProject : ModuleRule
{
    public override void ConfigureAll(Configuration conf, TargetRule target)
    {
        conf.AddPublicDependency<GlmProject>(target);
        conf.AddPublicDependency<SpdlogProject>(target);
    }
}
```
