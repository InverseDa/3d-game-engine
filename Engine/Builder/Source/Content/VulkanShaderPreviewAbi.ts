type JsonObject = Record<string, unknown>;
type Signature = string | number | boolean | Signature[];

export class UnsupportedShaderInterface extends Error {}

function Require(Condition: unknown, Message: string): asserts Condition {
    if (!Condition) {
        throw new UnsupportedShaderInterface(Message);
    }
}

function ObjectWithFields(Value: unknown, Fields: readonly string[], Context: string): JsonObject {
    Require(Value !== null && typeof Value === "object" && !Array.isArray(Value), Context + " must be an object");
    const ObjectValue = Value as JsonObject;
    Require(Object.keys(ObjectValue).every((Key) => Fields.includes(Key)), Context + " contains an unsupported field");
    return ObjectValue;
}

function Entries(Value: unknown, Context: string): unknown[] {
    Require(Array.isArray(Value), Context + " must be an array");
    return Value;
}

function Text(Value: unknown, Context: string): string {
    Require(typeof Value === "string" && Value.length > 0, Context + " must be a string");
    return Value;
}

function Natural(Value: unknown, Context: string): number {
    Require(typeof Value === "number" && Number.isSafeInteger(Value) && Value >= 0, Context + " must be a nonnegative integer");
    return Value;
}

function Sorted(Values: readonly Signature[]): string[] {
    return Values.map((Value) => JSON.stringify(Value)).sort();
}

/** glslang/spirv-cross subsets used by the BasePass pixel interface. */
export function NormalizeShaderReflection(Reflection: unknown, Entry: string): string {
    const Groups = ["inputs", "outputs", "separate_images", "separate_samplers", "ubos", "push_constants"];
    const Module = ObjectWithFields(Reflection, ["entryPoints", "types", ...Groups], "reflection");
    const EntryPoints = Entries(Module.entryPoints, "entryPoints");
    Require(EntryPoints.length === 1, "expected a single shader entry point");
    const EntryPoint = ObjectWithFields(EntryPoints[0], ["name", "mode"], "entry point");
    Require(EntryPoint.name === Entry && EntryPoint.mode === "frag", "expected the configured fragment entry point");
    Require(Module.types !== null && typeof Module.types === "object" && !Array.isArray(Module.types), "expected block types");
    const Types = Module.types as JsonObject;

    function Block(TypeId: string): [Signature[], number] {
        const Type = ObjectWithFields(Types[TypeId], ["name", "members"], "block type");
        let Extent = 0;
        const Members = Entries(Type.members, "block members").map((Value): Signature => {
            const Member = ObjectWithFields(Value, ["name", "type", "offset"], "block member");
            const TypeName = Text(Member.type, "block member type");
            Require(/^(?:float|int|uint|[iu]?vec[2-4])$/.test(TypeName), "unsupported block member type " + TypeName);
            const Offset = Natural(Member.offset, "block member offset");
            const Size = 4 * (Number(TypeName.at(-1)) || 1);
            Extent = Math.max(Extent, Offset + Size);
            return [Offset, TypeName, Size];
        });
        return [Sorted(Members), Extent];
    }

    const Resources: Signature[] = [];
    for (const Group of Groups) {
        if (Module[Group] === undefined) {
            continue;
        }
        for (const Value of Entries(Module[Group], Group)) {
            if (Group === "inputs" || Group === "outputs") {
                const Variable = ObjectWithFields(Value, ["name", "type", "location"], Group);
                Resources.push([Group, Natural(Variable.location, "location"), Text(Variable.type, "stage type")]);
            } else if (Group === "push_constants") {
                const Variable = ObjectWithFields(Value, ["name", "type", "push_constant"], Group);
                Require(Variable.push_constant === true, "expected a push constant block");
                const [Members, Extent] = Block(Text(Variable.type, "push constant type"));
                Resources.push([Group, Extent, Members]);
            } else {
                const IsBlock = Group === "ubos";
                const Variable = ObjectWithFields(Value, ["name", "type", "set", "binding", ...(IsBlock ? ["block_size"] : [])], Group);
                const Set = Natural(Variable.set, "descriptor set");
                const Binding = Natural(Variable.binding, "descriptor binding");
                const Type = Text(Variable.type, "descriptor type");
                if (IsBlock) {
                    const [Members, Extent] = Block(Type);
                    Resources.push([Group, Set, Binding, Natural(Variable.block_size, "uniform block size"), Extent, Members]);
                } else {
                    Resources.push([Group, Set, Binding, Type]);
                }
            }
        }
    }
    return JSON.stringify(Sorted(Resources));
}

type Declaration = { Operation: string; Operands: string[] };
type Variable = { Id: string; Type: string; Storage: string };

/** Covers the decorations and built-ins which spirv-cross --reflect does not list. */
export function NormalizeShaderDeclarations(Disassembly: string, Entry: string): string {
    const Declarations = new Map<string, Declaration>();
    const Decorations = new Map<string, string[]>();
    const MemberDecorations = new Map<string, string[]>();
    const Variables: Variable[] = [];
    const Module: Signature[] = [];
    const EntryPoints: string[][] = [];
    const ExecutionModes: string[][] = [];
    const Id = /^%\d+$/;

    function AddDecoration(Target: Map<string, string[]>, Key: string, Decoration: string): void {
        const Previous = Target.get(Key);
        if (Previous) {
            Previous.push(Decoration);
        } else {
            Target.set(Key, [Decoration]);
        }
    }

    for (const Line of Disassembly.split(/\r?\n/)) {
        if (!Line.trim() || Line.trimStart().startsWith(";")) {
            continue;
        }
        const Instruction = /^\s*(?:(%\d+)\s*=\s*)?(Op\w+)(?:\s+(.*?))?\s*$/.exec(Line);
        Require(Instruction, "unrecognized SPIR-V declaration");
        const [, ResultId, Operation, OperandText = ""] = Instruction;
        const Operands = OperandText.split(/\s+/);
        if (Operation === "OpCapability" || Operation === "OpExtension" ||
            Operation === "OpExtInstImport" || Operation === "OpMemoryModel") {
            Module.push([Operation, OperandText]);
        } else if (Operation === "OpEntryPoint") {
            EntryPoints.push(Operands);
        } else if (Operation === "OpExecutionMode") {
            ExecutionModes.push(Operands);
        } else if (Operation === "OpDecorate" || Operation === "OpMemberDecorate") {
            const [Target, ...Arguments] = Operands;
            Require(Id.test(Target) && Arguments.length >= (Operation === "OpDecorate" ? 1 : 2), "invalid decoration");
            // Direct scalar decorations are the only supported interface decorations.
            Require(!Arguments.some((Operand) => Id.test(Operand)), "unsupported ID decoration operand");
            AddDecoration(Operation === "OpDecorate" ? Decorations : MemberDecorations, Target, Arguments.join(" "));
        } else if (/^Op(?:Group.*Decorate|Decorate|MemberDecorate|DecorationGroup|ExecutionMode)/.test(Operation)) {
            throw new UnsupportedShaderInterface("unsupported SPIR-V declaration " + Operation);
        } else if (Operation.startsWith("OpType")) {
            Declarations.set(ResultId, { Operation, Operands });
        } else if (Operation === "OpVariable") {
            const [Type, Storage] = Operands;
            Variables.push({ Id: ResultId, Type, Storage });
        }
    }

    Require(EntryPoints.length === 1, "expected a single SPIR-V entry point");
    const [Model, FunctionId, EntryName, ...InterfaceIds] = EntryPoints[0];
    Require(Model === "Fragment" && EntryName === JSON.stringify(Entry), "expected the configured fragment entry point");
    for (const [Target, ...Mode] of ExecutionModes) {
        Require(Target === FunctionId, "execution mode does not belong to the fragment entry");
        Module.push(["OpExecutionMode", ...Mode]);
    }

    function TypeOf(TypeId: string, Depth: number): Signature {
        Require(Depth <= 8, "unsupported recursive interface type");
        const Type = Declarations.get(TypeId);
        Require(Type, "missing interface type declaration");
        const { Operation, Operands } = Type;
        let Shape: Signature[];
        const Nested = (Operand: string): Signature => TypeOf(Operand, Depth + 1);
        switch (Operation) {
            case "OpTypeBool":
            case "OpTypeFloat":
            case "OpTypeInt":
            case "OpTypeSampler":
                Shape = Operands;
                break;
            case "OpTypeVector":
            case "OpTypeImage":
                Shape = [Nested(Operands[0]), ...Operands.slice(1)];
                break;
            case "OpTypePointer":
                Shape = [Operands[0], Nested(Operands[1])];
                break;
            case "OpTypeStruct":
                Shape = Operands.map(Nested);
                break;
            default:
                throw new UnsupportedShaderInterface("unsupported interface type " + Operation);
        }
        return [Operation, Shape, (Decorations.get(TypeId) ?? []).sort(), (MemberDecorations.get(TypeId) ?? []).sort()];
    }

    const EntryInterface = new Set(InterfaceIds);
    const SupportedStorage = ["Input", "Output", "Uniform", "UniformConstant", "PushConstant"];
    for (const Variable of Variables) {
        const IsListed = EntryInterface.delete(Variable.Id);
        if (SupportedStorage.includes(Variable.Storage)) {
            Module.push([Variable.Storage, IsListed, TypeOf(Variable.Type, 0), (Decorations.get(Variable.Id) ?? []).sort()]);
        } else {
            Require(!IsListed && ["Function", "Private"].includes(Variable.Storage), "unsupported variable storage " + Variable.Storage);
        }
    }
    Require(EntryInterface.size === 0, "unknown fragment entry interface variable");
    return JSON.stringify(Sorted(Module));
}
