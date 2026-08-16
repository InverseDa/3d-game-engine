import * as Assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import * as Fs from "node:fs/promises";
import * as Os from "node:os";
import * as Path from "node:path";
import Test from "node:test";
import { fileURLToPath } from "node:url";
import WorldBuild from "../../Source/Runtime/World/Build.ts";
import DemoApplicationBuild from "../../Source/Runtime/DemoApplication/Build.ts";
import { CreateModuleConfiguration } from "../Source/Configuration/ModuleBuild.ts";
import { Optimization, Platform, TargetType } from "../Source/Configuration/Types.ts";
import {
    EmitReflectionModule, ParseReflectionModule, ReflectionGeneratorError,
    type ReflectionSource,
} from "../Source/ReflectionGenerator/ReflectionGenerator.ts";

const BuilderRoot = Path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const CommandLine = Path.join(BuilderRoot, "Source", "ReflectionGenerator", "CommandLine.ts");

function Source(Text: string, Name = "Public/Test/Fixture.h"): ReflectionSource {
    return { Path: Name, Include: "Test/Fixture.h", Text };
}

const ValidSource = `
// LE_CLASS("ffffffff-ffff-ffff-ffff-ffffffffffff")
constexpr const char* Fake = "LE_STRUCT(\\\"ffffffff-ffff-ffff-ffff-ffffffffffff\\\")";
constexpr const char* Raw = R"tag(LE_PROPERTY("ffffffff-ffff-ffff-ffff-ffffffffffff"))tag";
constexpr char Character = 'L';
namespace LE::Tests {
LE_STRUCT("62000000-0000-4000-8000-000000000001")
struct TEST_API FValue { LE_GENERATED_BODY() };
LE_CLASS("62000000-0000-4000-8000-000000000002")
class TEST_API FOwner {
    LE_GENERATED_BODY()
private:
    LE_PROPERTY("62000000-0000-4000-8000-000000000003")
    FValue Value;
};
LE_ENUM("62000000-0000-4000-8000-000000000004")
enum class TEST_API EMode : int8 { Negative = -1, Positive = 0x7f, };
}
`;

const Options = { ModuleName: "Fixture", ApiMacro: "TEST_API", EntryHeader: "Test/FixtureReflection.h" };

Test("reflection parser ignores fake macro text and emits deterministic descriptors/access", () => {
    const Module = ParseReflectionModule([Source(ValidSource)], Options);
    Assert.deepEqual(Module.Types.map((Type) => Type.QualifiedName), ["LE.Tests.FValue", "LE.Tests.FOwner"]);
    Assert.equal(Module.Types[1].Properties[0].ValueTypeId, "62000000-0000-4000-8000-000000000001");
    Assert.deepEqual(Module.Enums[0].Values.map((Value) => Value.ValueBits), [255n, 127n]);
    const First = EmitReflectionModule(Module);
    const Second = EmitReflectionModule(ParseReflectionModule([Source(ValidSource)], Options));
    Assert.equal(First, Second);
    const Deduplicated = EmitReflectionModule({ ...Module, EntryHeader: Module.Inputs[0] });
    Assert.equal(Deduplicated.match(/#include \"Test\/Fixture\.h\"/g)?.length, 1);
    Assert.match(First, /TReflectionGeneratedAccess<::LE::Tests::FOwner>/);
    Assert.match(First, /template <typename FValue>[\s\S]*SetProperty0Impl/);
    Assert.match(First, /PropertySetter0\(\)/);
    Assert.match(First, /RegisterFixtureReflection/);
    Assert.doesNotMatch(First, /static\s+FReflectionModuleDescriptor/);
});

function ExpectDiagnostic(Text: string, Pattern: RegExp): void {
    Assert.throws(
        () => ParseReflectionModule([Source(Text)], Options),
        (Error: unknown) => Error instanceof ReflectionGeneratorError
            && /^Public\/Test\/Fixture\.h:\d+:\d+: error: /.test(Error.message)
            && Pattern.test(Error.message),
    );
}

Test("reflection parser rejects conditional, misplaced, duplicate, unsupported, and incomplete declarations", () => {
    const Cases: Array<[string, RegExp]> = [
        [`#if 0\n${ValidSource}\n#endif\n`, /conditional preprocessing region/],
        [`namespace LE { LE_STRUCT(L"62000000-0000-4000-8000-000000000001") struct TEST_API F { LE_GENERATED_BODY() }; }`, /ordinary UUID string literal/],
        [`namespace LE { LE_STRUCT("not-a-uuid") struct TEST_API F { LE_GENERATED_BODY() }; }`, /canonical non-nil UUID/],
        [`namespace LE { LE_STRUCT("00000000-0000-0000-0000-000000000000") struct TEST_API F { LE_GENERATED_BODY() }; }`, /canonical non-nil UUID/],
        [`namespace LE { template <typename T> LE_STRUCT("62000000-0000-4000-8000-000000000001") struct TEST_API F { LE_GENERATED_BODY() }; }`, /template declarations/],
        [`namespace LE { LE_CLASS("62000000-0000-4000-8000-000000000001") class TEST_API F { }; }`, /exactly one LE_GENERATED_BODY/],
        [`namespace LE { LE_CLASS("62000000-0000-4000-8000-000000000001") class TEST_API F { LE_GENERATED_BODY() LE_GENERATED_BODY() }; }`, /exactly one LE_GENERATED_BODY/],
        [`namespace LE { LE_STRUCT("62000000-0000-4000-8000-000000000001") struct TEST_API F { LE_GENERATED_BODY() void Run() { LE_PROPERTY("62000000-0000-4000-8000-000000000002") F Value; } }; }`, /nested scope/],
        [`namespace LE { LE_STRUCT("62000000-0000-4000-8000-000000000001") struct TEST_API F { LE_GENERATED_BODY() } }`, /must end with ';'/],
        [`namespace LE { LE_CLASS("62000000-0000-4000-8000-000000000001") class TEST_API F : Base { LE_GENERATED_BODY() }; }`, /base classes/],
        [`namespace LE { LE_STRUCT("62000000-0000-4000-8000-000000000001") struct TEST_API F { LE_GENERATED_BODY() LE_PROPERTY("62000000-0000-4000-8000-000000000002") int Value; }; }`, /neither a canonical builtin/],
        [`namespace LE { LE_STRUCT("62000000-0000-4000-8000-000000000001") struct TEST_API V { LE_GENERATED_BODY() }; LE_STRUCT("62000000-0000-4000-8000-000000000002") struct TEST_API O { LE_GENERATED_BODY() LE_PROPERTY("62000000-0000-4000-8000-000000000003") static V A; }; }`, /non-static/],
        [`namespace LE { LE_STRUCT("62000000-0000-4000-8000-000000000001") struct TEST_API V { LE_GENERATED_BODY() }; LE_STRUCT("62000000-0000-4000-8000-000000000002") struct TEST_API O { LE_GENERATED_BODY() LE_PROPERTY("62000000-0000-4000-8000-000000000003") V A[2]; }; }`, /non-array/],
        [`namespace LE { LE_STRUCT("62000000-0000-4000-8000-000000000001") struct TEST_API V { LE_GENERATED_BODY() }; LE_STRUCT("62000000-0000-4000-8000-000000000002") struct TEST_API O { LE_GENERATED_BODY() LE_PROPERTY("62000000-0000-4000-8000-000000000003") V A; LE_PROPERTY("62000000-0000-4000-8000-000000000003") V B; }; }`, /duplicate property ID/],
        [`namespace LE { LE_STRUCT("62000000-0000-4000-8000-000000000001") struct TEST_API V { LE_GENERATED_BODY() }; LE_STRUCT("62000000-0000-4000-8000-000000000002") struct TEST_API O { LE_GENERATED_BODY() LE_PROPERTY("62000000-0000-4000-8000-000000000003") V A; LE_PROPERTY("62000000-0000-4000-8000-000000000004") V A; }; }`, /duplicate property name/],
        [`namespace LE { LE_ENUM("62000000-0000-4000-8000-000000000001") enum class TEST_API E : uint8 { TooBig = 256, }; }`, /out of range/],
        [`namespace LE { LE_ENUM("62000000-0000-4000-8000-000000000001") enum class TEST_API E : uint8 { Same = 1, Same = 2, }; }`, /duplicate enum value name/],
        [`namespace LE { LE_STRUCT("62000000-0000-4000-8000-000000000001") struct TEST_API A { LE_GENERATED_BODY() }; LE_CLASS("62000000-0000-4000-8000-000000000001") class TEST_API B { LE_GENERATED_BODY() }; }`, /duplicate reflected type ID/],
    ];
    for (const [Text, Pattern] of Cases) ExpectDiagnostic(Text, Pattern);

    Assert.throws(
        () => ParseReflectionModule([
            Source(`namespace LE { LE_STRUCT("62000000-0000-4000-8000-000000000001") struct TEST_API Same { LE_GENERATED_BODY() }; }`, "Public/Test/A.h"),
            Source(`namespace LE { LE_CLASS("62000000-0000-4000-8000-000000000002") class TEST_API Same { LE_GENERATED_BODY() }; }`, "Public/Test/B.h"),
        ], Options),
        /duplicate reflected qualified name/,
    );
});

Test("reflection parser maps canonical builtin spellings to C++ accessors without UUID duplication", () => {
    const Module = ParseReflectionModule([Source(`namespace LE {
LE_STRUCT("62000000-0000-4000-8000-000000000010")
struct TEST_API FBuiltinOwner { LE_GENERATED_BODY()
LE_PROPERTY("62000000-0000-4000-8000-000000000011") uint32 Width;
LE_PROPERTY("62000000-0000-4000-8000-000000000012") LE::String Title;
}; }`)], Options);
    Assert.equal(Module.Types[0].Properties[0].BuiltinType, "UInt32");
    Assert.equal(Module.Types[0].Properties[1].BuiltinType, "String");
    const Generated = EmitReflectionModule(Module);
    Assert.match(Generated, /GetBuiltinPropertyTypeId\(EPropertyBuiltinType::UInt32\)/);
    Assert.match(Generated, /GetBuiltinPropertyTypeId\(EPropertyBuiltinType::String\)/);
    Assert.doesNotMatch(Generated, /Type0Properties\[0\]\.ValueTypeId\)\)/);
});

Test("World declares a typed generated-source CustomAction with complete incremental inputs", () => {
    const Configuration = CreateModuleConfiguration();
    new WorldBuild().Configure({ Platform: Platform.Win64, Optimization: Optimization.Debug, TargetType: TargetType.Game }, Configuration);
    Assert.equal(Configuration.CustomActions.length, 1);
    const Action = Configuration.CustomActions[0];
    Assert.equal(Action.Id, "GenerateReflection");
    Assert.equal(Action.RunBeforeCompile, true);
    Assert.deepEqual(Action.Inputs, ["[module.SourceRoot]/Public/World/World.h"]);
    Assert.ok(Action.ImplicitInputs?.some((Input) => Input.endsWith("/ReflectionGenerator.ts")));
    Assert.ok(Action.ImplicitInputs?.some((Input) => Input.endsWith("/Reflection/ReflectionMacros.h")));
    Assert.ok(Action.Outputs[0].endsWith("/Reflection/World.reflection.generated.cpp"));
    Assert.deepEqual(Action.Command.slice(Action.Command.indexOf("--api-macro"), Action.Command.indexOf("--api-macro") + 2), ["--api-macro", "WORLD_API"]);
});

Test("DemoApplication private settings action stays internal and depends on Reflection", () => {
    const Configuration = CreateModuleConfiguration();
    new DemoApplicationBuild().Configure(
        { Platform: Platform.Win64, Optimization: Optimization.Debug, TargetType: TargetType.Game },
        Configuration,
    );
    Assert.ok(Configuration.PublicDependencies.includes("Reflection"));
    const Action = Configuration.CustomActions.find((Value) => Value.Id === "GenerateReflection");
    Assert.ok(Action);
    Assert.deepEqual(Action.Inputs, ["[module.SourceRoot]/Private/DemoSettings.h"]);
    Assert.ok(!Action.Command.includes("--api-macro"));
    Assert.ok(Action.Outputs[0].endsWith("/Reflection/DemoApplication.reflection.generated.cpp"));
});

Test("CLI is write-if-changed and a failed generation preserves the previous output", async (Context) => {
    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-reflection-generator-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const Header = Path.join(Root, "Public", "Test", "Fixture.h");
    const Output = Path.join(Root, "Generated", "Fixture.reflection.generated.cpp");
    await Fs.mkdir(Path.dirname(Header), { recursive: true });
    await Fs.writeFile(Header, ValidSource);
    const Args = [
        "--no-warnings", "--experimental-strip-types", CommandLine,
        "--module", "Fixture", "--module-root", Root, "--api-macro", "TEST_API",
        "--entry-header", "Test/FixtureReflection.h", "--output", Output, "--input", Header,
    ];
    const First = spawnSync(process.execPath, Args, { encoding: "utf-8", shell: false });
    Assert.equal(First.status, 0, First.stderr);
    const Original = await Fs.readFile(Output, "utf-8");
    const FirstMtime = (await Fs.stat(Output)).mtimeMs;
    await new Promise((Resolve) => setTimeout(Resolve, 30));
    const Second = spawnSync(process.execPath, Args, { encoding: "utf-8", shell: false });
    Assert.equal(Second.status, 0, Second.stderr);
    Assert.equal((await Fs.stat(Output)).mtimeMs, FirstMtime);

    await Fs.writeFile(Header, `#if 0\n${ValidSource}\n#endif\n`);
    const Failed = spawnSync(process.execPath, Args, { encoding: "utf-8", shell: false });
    Assert.notEqual(Failed.status, 0);
    Assert.match(Failed.stderr, /conditional preprocessing region/);
    Assert.equal(await Fs.readFile(Output, "utf-8"), Original);
    Assert.deepEqual((await Fs.readdir(Path.dirname(Output))).filter((Name) => /\.(?:tmp|bak)$/.test(Name)), []);

    const NeverCreated = Path.join(Root, "Generated", "invalid-first.cpp");
    const InvalidFirstArgs = [...Args];
    InvalidFirstArgs[InvalidFirstArgs.indexOf(Output)] = NeverCreated;
    const InvalidFirst = spawnSync(process.execPath, InvalidFirstArgs, { encoding: "utf-8", shell: false });
    Assert.notEqual(InvalidFirst.status, 0);
    await Assert.rejects(() => Fs.access(NeverCreated));
});
