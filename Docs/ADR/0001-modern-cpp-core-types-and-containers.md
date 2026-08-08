# ADR-0001: P0 Modern C++ Language, Core Types, and Containers

- 状态：Accepted
- 日期：2026-08-07
- 阶段：P0 / Roadmap Phase 1.5 C0
- 决策范围：非 `ThirdParty` 的引擎 C++ 代码

## 1. 背景与仓库事实

本 ADR 在 allocator、容器、数学类型和 namespace 迁移开始前确定共同契约，避免各模块
分别封装 STL、分别选择命名空间或形成跨 DLL 的隐式所有权。

截至本 ADR 接受时，仓库事实如下：

- Win64 的 Ninja/MSVC 编译命令使用 `/std:c++17`、`/EHsc` 和 `/GR`。
- Visual Studio 工程生成器使用 VS 17、v143 toolset 和 `stdcpp17`。
- Mac 工程由 Xcode 14 兼容工程驱动 Apple Clang，语言模式为 `c++17`；生成配置当前关闭
  C++ exceptions 和 RTTI。
- Core 的 `FString` 以 `std::string` 为临时存储，数学类型仍位于全局 `FMath`，Core 仍公开
  依赖 GLM，并通过 `LE_USE_GLM` 暴露包含路径。
- Runtime 公共头仍广泛暴露 `std::vector`、`std::unordered_map`、智能指针和
  `std::function`，多数引擎声明仍在全局 namespace。
- Win64 Editor 模块可构建为 DLL；模块 API 宏由 Builder 生成。因此 allocator 身份、析构
  位置和 ABI 不能留给“当前恰好使用同一 CRT”这一假设。

以上是迁移起点，不是本 ADR 对最终 API 的认可。

## 2. 语言与工具链基线

P0 的可移植语言基线是 **C++17**，以两个受支持平台的共同语言模式为准：

| 平台 | 编译器入口 | P0 语言模式 | 本 ADR 的结论 |
|------|------------|-------------|----------------|
| Win64 | VS2022/v143 MSVC（Builder 自动发现） | `/std:c++17` | C++17 是必需且足够的基线 |
| Mac arm64 | Xcode 随附 Apple Clang | `c++17` | C++17 是必需且足够的基线 |

具体编译器小版本不是本文凭空指定的兼容承诺；可支持版本由 Builder 探测与两平台 CI
实编译共同证明。非 `ThirdParty` Runtime 不得无保护地使用 C++20/23/26 特性。提升语言
基线必须同时完成 Builder/Ninja、Visual Studio、Xcode、测试矩阵和本 ADR 的更新。
实验性标准反射等能力只能位于可替换后端后方，并保留 C++17 路径，不能泄漏进稳定公共
API。

当前 MSVC 与 Xcode 的 exceptions/RTTI 开关并不一致。本 ADR 只确定代码契约，不在 C0
修改编译开关；后续必须在不破坏第三方依赖的前提下统一并由测试证明。

## 3. Namespace 决策

### 3.1 根与层级

- 引擎根 namespace 直接使用短 PascalCase `LE`。
- 不定义 `Limitless` namespace，也不提供 `Limitless = LE` 或 `LE = Limitless` 的公开 alias。
- Core 的通用词汇类型直接位于 `LE`，例如 `LE::Array`、`LE::String`。
- 仅在领域边界有真实语义时使用浅层 namespace：`LE::Core`、`LE::RAL`、`LE::RFG`、
  `LE::Math`、`LE::Vulkan`、`LE::Detail`。
- 不机械地为每个 Build 模块、目录或文件建立 namespace。Renderer、World、Platform 等类型
  可以先直接位于 `LE`，直到出现需要消歧或形成独立领域契约的证据。
- `LE::Detail` 是实现细节区，不属于稳定公共 API；公开签名不得要求调用者命名其中类型。
- 公共头禁止 `using namespace`。namespace alias 只能放在私有实现中，且不得成为持久化或
  模块边界格式的一部分。

C6 按依赖顺序完成 namespace 迁移。在 C6 结束前，短期内部兼容声明只有在维持逐模块可
构建时才允许，必须记录删除点并在 C6 完成前移除；不得建立第二套永久根 namespace。

### 3.2 现有命名约定

P0 保留现有 `F/L/T/E/I` 前缀以及现有函数、成员、常量和布尔命名方式，不把基础设施迁移
变成 cosmetic rename：

- `F`：普通 native/value/RAII 类型，不参与未来 tracing GC 生命周期。`F` 类型仍可通过
  明确 RAII 拥有资源。
- `L`：保留给参与未来 managed object / tracing GC 契约的对象。在基类/trait、反射元数据、
  分配路径、registry 和 collector 尚未形成可机械验证的整体前，不随意新增 `L` 类型。
- `T`：模板类型；`E`：枚举；`I`：接口。

前缀是可见 API 信号，不是 enforcement。未来 GC 基础完成后，结构检查必须验证 `L` 类型
确实采用规定的基类、元数据和分配路径。

## 4. 宏边界

引擎自有宏统一使用 `LE_*` 前缀，并仅用于预处理器确有必要的边界：

- 平台、编译器和 feature switches；
- DLL import/export 与符号可见性；
- assertions、日志和编译期诊断；
- 反射声明及其他必须参与预处理的代码生成入口。

普通常量、类型别名、模板、inline 函数、enum 和算法不得为了缩短书写而做成宏。宏不得
偷偷分配、转移 ownership 或改变控制流。现有 `FORCE_INLINE`、`CORE_API`/`RAL_API` 等
非 `LE_*` 引擎宏是迁移债务，不在 C0 改代码；后续宏迁移应落为 `LE_FORCE_INLINE`、
`LE_<MODULE>_API` 等形式，并与调用方一次性同步。

## 5. Core 类型目录与稳定名字

以下是 P0 实现与迁移使用的规范名字；不得再引入同义 wrapper：

| 领域 | 规范类型 | 位置与边界 |
|------|----------|------------|
| 动态连续存储 | `LE::Array<T, Allocator>` | 自有 storage/ABI，不得 alias 或包裹 `std::vector` |
| 固定连续存储 | `LE::StaticArray<T, N>` | 值类型、容量编译期固定 |
| 非拥有视图 | `LE::Span<T>` | 不延长生命周期，不隐式拥有 |
| 哈希关联 | `LE::HashMap<K, V, Hash, Equal>` | 无序；明确 hash/equality/load policy |
| 哈希集合 | `LE::HashSet<K, Hash, Equal>` | 无序；与 HashMap 共用 policy |
| UTF-8 字节字符串 | `LE::String` | 自有 storage；P0 文本编码契约为 UTF-8 |
| 字符串视图 | `LE::StringView` | 非拥有；不保证 NUL 结尾 |
| 数学 | `LE::Math::Vector<T, N>` | 向量值类型；提供简洁 scalar/dimension aliases |
| 数学 | `LE::Math::Matrix<T, R, C>` | 矩阵值类型；布局和乘法约定由 C4 测试锁定 |
| 数学 | `LE::Math::Quaternion<T>` | 四元数值类型；分量顺序和角度单位由 C4 锁定 |

使用 `HashMap` 而不是含义模糊的 `Map`。只有真实的有序消费者出现后才新增
`OrderedMap`/`OrderedSet`，不得让 `HashMap` 假装提供稳定顺序。

以下类型按真实消费者需要引入，不为“STL 对称性”预先建设：

- `LE::UniquePtr`：需要唯一 heap ownership 且不能内嵌值语义时；必须携带正确 deleter/
  allocator route。
- `LE::SharedPtr`/`LE::WeakPtr`：只有共享生命周期不可由单一 owner 表达时；禁止用 shared
  ownership 回避设计 owner。跨 DLL 销毁必须回到创建方。
- `LE::Optional`：现有 API 确有“有值/无值”的值语义，并且状态 enum 或指针不更清楚时。
- `LE::Function`：现有消费者确需 type erasure 时；其分配、复制、异常和 DLL 销毁规则必须
  先测试。无捕获/静态调用优先函数指针，热路径优先模板 callable。

这些类型均不得仅 typedef 到对应的 `std` owning 类型后对外暴露。

## 6. Ownership、错误与异常

### 6.1 Ownership

- 值、`LE::UniquePtr`、容器和明确的 RAII handle 表示 ownership。
- 裸指针、引用、`LE::Span`、`LE::StringView` 默认是 borrowed；不得超出 owner 生命周期。
- API 若转移 ownership，必须在类型系统和命名/文档中可见，不能靠注释猜测 `delete` 责任。
- 优先单一 owner。共享 owner 必须说明为何 observer 或句柄不足，并避免引用环。
- Vulkan/C 等外部 handle 由明确的 RAII owner 包装；观察 handle 不负责销毁。

### 6.2 错误与异常

- 引擎公共 API 和模块/DLL 边界采用显式错误通道，不以 C++ exception 作为正常控制流。
- 在通用 `Result` 类型出现前，使用领域状态 enum、`bool` + 明确 out value，或可空 borrowed
  pointer；失败语义必须写在 API 契约和测试中。
- assertion 只检查程序员错误和不变量，不能替代可恢复输入、I/O、设备或 allocation failure
  处理。Release 行为不能依赖 assertion 的副作用。
- 引擎 exception 不得跨模块、DLL、C API 或第三方 callback 边界。调用可能抛异常的第三方
  代码必须在边界内转换为显式错误或执行明确的 fatal policy。
- Core containers 的元素操作以不抛异常为设计目标。若用户类型的 move/copy/constructor
  抛出，P0 不承诺跨两套编译开关提供强异常保证；此类类型不得用于要求 exception-free 的
  容器路径。

## 7. Allocator 与跨 DLL 销毁

C1 建立 `LE::IAllocator`、默认 allocator 及集中式 allocation helpers。契约如下：

- allocation 输入至少包含 size 与 alignment；deallocation 必须沿用产生该 allocation 的
  allocator/owner route。需要 resize 时必须显式定义旧/新 size 和移动语义。
- `size * count`、capacity 计算和对齐上取整必须先做 overflow 检查。alignment 必须为非零
  2 的幂，并满足平台 API 与元素类型要求；非法输入触发可诊断 contract failure。
- 零字节 allocation 的返回值不作为可解引用存储；调用方不得据其地址身份作逻辑判断。
- allocator 原语以空结果报告 allocation failure；容器的普通 mutating API 不改变现有签名来
  隐式传播失败，而是调用 Core 统一、non-returning 的 out-of-memory handler。只有名称明确
  的 `Try*` API（如确有可恢复消费者再提供）可以返回失败且保持原值有效。不得抛
  `std::bad_alloc`、静默继续或让不同容器自选策略。C1 必须用可注入 failing allocator 和
  可测试 OOM handler 覆盖两条路径。
- allocator 对象必须比其所有 allocation 与容器活得更久。容器保存足以把释放送回原
  allocator 的身份，不依赖“调用方 DLL 的默认 delete”。
- 跨 DLL 传递 owning object 时，销毁必须调用创建模块导出的 destroy/release 操作，或由
  对象保存的 allocator/deleter 回到创建方。禁止一侧 `new`、另一侧裸 `delete`；即使当前
  Win64 使用动态 CRT 也不放宽此规则。
- 第三方 allocator 只可在窄 adapter 中接入。adapter 记录 ownership、alignment、失败和
  线程安全语义，不能把第三方 allocator 类型扩散到引擎公共 API。

## 8. Container 行为契约

### 8.1 Growth、relocation 与失效规则

- `LE::Array` 的 growth factor 属于实现细节，但增长必须摊还常数复杂度、单调增加、避免
  overflow，并保证 `Reserve(N)` 后在 size 不超过 N 时不再次分配。调用方不得依赖具体
  capacity 数列。
- `Array` 发生 reallocation、`Reserve` 导致扩容或 `Shrink` 移动 storage 时，全部 iterator、
  pointer、reference 和 `Span` 失效。未 reallocate 的尾部 append 保持已有元素引用有效但
  旧 end iterator 失效；insert/erase 使操作点及之后的位置失效；`Clear` 使所有元素引用失效。
- `StaticArray` 地址在对象生命周期内稳定；对象 move/assignment/destruction 及包含对象的
  relocation 仍会使既有视图失效。
- `Span`/`StringView` 永不延长 owner 生命周期；owner 销毁或按上述规则变更后立即失效。
- `HashMap`/`HashSet` 的任何 insertion 或 rehash 都允许使全部 iterator、pointer 和 reference
  失效；erase 至少使被删元素失效。只有具体 API 明确给出更强保证时调用方才能依赖它。
- relocation 只能对显式 trait 认定可 bitwise relocate 的类型使用 `memcpy/memmove`；其他
  类型逐元素 move-construct 并正确 destroy。不能用 `is_trivially_copyable` 之外的猜测替代
  经测试的 trait 契约。

Debug 构建至少检查 bounds、空 view 解引用、无效 alignment、size/capacity overflow 和可
检测的 iterator generation mismatch；Release 保留防止内存破坏所必需的 overflow/size 检查。

### 8.2 Hash、equality 与顺序

- `Hash` 与 `Equal` 是显式 policy；若 `Equal(A, B)` 为真，二者 hash 必须相等。
- 默认 hash 只覆盖已明确支持的基础类型、enum、pointer（仅进程内）和 String/StringView；
  业务 key 应提供显式 hash，而不是对对象 padding/原始内存 hash。
- String 与 StringView 使用相同内容 hash/equality，使 heterogeneous lookup 无需临时分配。
  对其他 key，在 hash/equality 均声明透明且语义一致时提供 heterogeneous lookup。
- iteration order 明确不稳定，可因插入、删除、rehash、构建配置或实现版本改变。序列化、
  网络、资产 hash、测试 golden output 等确定性消费者必须显式排序，不能依赖桶顺序。
- load factor、rehash 触发点与 bucket growth 是实现细节；提供 `Reserve`/rehash 能力并用碰撞、
  adversarial hash、删除 tombstone 和重复 key 测试验证正确性与终止性。

## 9. ABI、DLL 与调试保证

- P0 的 engine C++ API 是同一源码、同一 toolchain、同一配置构建的模块间 API；不承诺跨
  编译器、跨标准库、跨 Debug/Release 或跨引擎版本的稳定二进制 ABI。
- 公共容器拥有自有 storage 与 layout，但这不等于永久冻结 object layout。不得持久化容器
  原始字节、把它作为网络格式，或作为第三方插件的长期 ABI。
- 需要稳定插件/工具边界时使用版本化 C ABI、opaque handle、明确长度的 buffer、固定宽度
  POD 和创建方销毁函数；不得暴露 STL 或 allocator implementation type。
- 导出模板只在调用方共同编译并满足同一配置时使用。非模板 DLL API 通过模块 export 宏
  暴露；allocation/deallocation 成对留在同一 ownership domain。
- Debug iterator/guard 信息不得改变持久化格式。Debug/Release 对可观察语义一致，仅诊断
  强度和性能不同。

## 10. Standard Library 使用边界

C5 完成后，非 `ThirdParty` 引擎代码禁止拥有 storage/lifetime 的 Standard Library 类型，
包括但不限于：

- `std::vector`、`deque`、`list`、`map`、`set`、`unordered_map`、`unordered_set`；
- `std::basic_string`/`std::string`；
- `std::unique_ptr`、`shared_ptr`、`weak_ptr`；
- 作为持久成员或公开 API 的 `std::optional`、`std::function` 及其他拥有对象。

禁令不扩展到有价值的非 owning/语言支持设施：algorithms、ranges（在语言基线允许时）、
type traits、concept 替代的 C++17 traits、atomics、mutex/locks、同步原语、`move`/`forward`、
numeric limits、chrono，以及其他不把 Standard Library ownership/ABI 泄漏进引擎接口的
设施。P0 不为 atomics 和 synchronization 做 cosmetic wrapper。

允许的 interoperability boundary 必须窄且有文档，例如 spdlog callback、Vulkan 输入数组、
OS API 或一次性的第三方转换。边界应位于 Private/adapter 层，把数据立即复制/观察为引擎
类型；不得用“第三方需要”作为整个模块继续存放 STL owning containers 的静默例外。每个
例外记录 owner、转换方向、生命周期和删除条件，并由 C7 allowlist 精确到文件/符号。

## 11. GLM 退出方向

GLM 在 C4 从 Runtime/Core 完全移除：删除 `LE_USE_GLM` include/define 路径和 Core 的公开
`Glm` dependency，确认所有非 `ThirdParty` 代码不再暴露 GLM 类型或转换。替代类型为
`LE::Math::Vector`、`Matrix`、`Quaternion`。

C4 在迁移消费者前必须用数值测试和文档锁定：坐标系 handedness、矩阵 storage order、
vector/matrix 乘法方向、Quaternion 分量顺序、角度单位和 alignment。当前 `FMath` 行为不能
未经测试直接被宣布为这些约定。

## 12. 强制迁移顺序

1. **C1**：allocator foundation、relocation/destruction traits、`Array`、`StaticArray`、`Span`；
   先用 Phase 1 的 Test target 覆盖生命周期、move-only、growth、失效、alignment、overflow、
   bounds 和 allocation failure。
2. **C2**：`HashMap`、`HashSet`；覆盖 hash/equality、heterogeneous lookup、collision、
   adversarial input、rehash 和 iteration-order contract。
3. **C3**：`String`、`StringView`；仅按真实消费者决定 `UniquePtr`、`SharedPtr`、`WeakPtr`、
   `Optional`、`Function`，并先落实跨 DLL 销毁规则。
4. **C4**：移除 Runtime/Core 的 GLM 并验证 `LE::Math` 数学约定。
5. **C5**：按 Core → RAL → RFG → Renderer → World → Platform/Launch → Programs/Tests 的
   顺序迁移 owning data structures。每批先迁公共 API，再迁私有和临时 Vulkan arrays；每个
   模块批次单独 rebuild/test，不夹带渲染或玩法重构。
6. **C6**：另行按依赖顺序迁移到 `LE` 及批准的浅层 namespace，不夹带语义或 cosmetic
   rename，并在结束前清除短期兼容 alias。
7. **C7**：在 CI/tests/Builder 增加 enforcement，拒绝非 `ThirdParty` 新增被禁 STL owning
   types、GLM 和批准 namespace 外的全局引擎声明；只允许精确记录的 interop allowlist。

任务 3（Builder Custom action）在 C0-C7 后执行。本顺序不得把 C5 和 C6 合并为一次难以
审查的大范围改写。

## 13. 验收与后续 ADR 触发条件

每个 C1-C7 任务必须有自动化测试、相关平台生成/构建验证与 `git diff --check`。当以下任一
事项发生时，应修订本文或新增 ADR，而不是在实现中隐式改变契约：

- 语言基线高于 C++17，或新增正式受支持编译器/平台；
- exception/RTTI 编译策略统一或改变；
- 引入稳定插件 ABI、跨进程 ABI 或可热重载模块边界；
- allocator lifetime、OOM policy、container invalidation 或 hash order guarantee 改变；
- managed `L` object 生命周期/GC 契约落地；
- 出现要求 ordered containers、Unicode normalization 或新 ownership wrapper 的真实消费者。
