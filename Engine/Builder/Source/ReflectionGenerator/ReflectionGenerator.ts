import * as Fs from "node:fs/promises";
import * as Path from "node:path";

export interface ReflectionSource {
    Path: string;
    Include: string;
    Text: string;
}

export interface ReflectionGeneratorOptions {
    ModuleName: string;
    ApiMacro?: string;
    EntryHeader: string;
}

export interface SourceLocation {
    Path: string;
    Line: number;
    Column: number;
}

type TokenKind = "identifier" | "string" | "number" | "symbol" | "raw-string" | "character";
interface Token extends SourceLocation {
    Kind: TokenKind;
    Value: string;
    ConditionalDepth: number;
    Prefix?: string;
}

export interface ReflectedProperty {
    Id: string;
    Name: string;
    ValueTypeName: string;
    ValueTypeId?: string;
    BuiltinType?: string;
    Location: SourceLocation;
}

export interface ReflectedType {
    Id: string;
    Name: string;
    QualifiedName: string;
    CppName: string;
    Kind: "Struct" | "Class";
    Properties: ReflectedProperty[];
    Location: SourceLocation;
}

export interface ReflectedEnumValue {
    Name: string;
    ValueBits: bigint;
    Location: SourceLocation;
}

export interface ReflectedEnum {
    Id: string;
    Name: string;
    QualifiedName: string;
    CppName: string;
    UnderlyingType: string;
    Values: ReflectedEnumValue[];
    Location: SourceLocation;
}

export interface ReflectedModule {
    Name: string;
    EntryHeader: string;
    Inputs: string[];
    Types: ReflectedType[];
    Enums: ReflectedEnum[];
}

export class ReflectionGeneratorError extends Error {
    public readonly Location: SourceLocation;

    public constructor(Location: SourceLocation, Message: string) {
        super(`${Location.Path}:${Location.Line}:${Location.Column}: error: ${Message}`);
        this.Location = Location;
    }
}

const ReflectionMacros = new Set([
    "LE_STRUCT", "LE_CLASS", "LE_ENUM", "LE_PROPERTY", "LE_GENERATED_BODY",
]);

function Fail(Token: Token | SourceLocation, Message: string): never {
    throw new ReflectionGeneratorError(Token, Message);
}

function IsIdentifierStart(Character: string): boolean { return /[A-Za-z_]/.test(Character); }
function IsIdentifierContinue(Character: string): boolean { return /[A-Za-z0-9_]/.test(Character); }

function Lex(Source: ReflectionSource): Token[] {
    const Tokens: Token[] = [];
    let Index = 0;
    let Line = 1;
    let Column = 1;
    let AtLineStart = true;
    let ConditionalDepth = 0;

    const Location = (): SourceLocation => ({ Path: Source.Path, Line, Column });
    const Advance = (): string => {
        const Character = Source.Text[Index++];
        if (Character === "\n") { Line++; Column = 1; AtLineStart = true; }
        else { Column++; if (Character !== " " && Character !== "\t" && Character !== "\r") AtLineStart = false; }
        return Character;
    };
    const Starts = (Value: string): boolean => Source.Text.startsWith(Value, Index);

    while (Index < Source.Text.length) {
        const Character = Source.Text[Index];
        if (/\s/.test(Character)) { Advance(); continue; }

        if (AtLineStart && Character === "#") {
            const DirectiveLocation = Location();
            const StartDepth = ConditionalDepth;
            Advance();
            while (Index < Source.Text.length && /[ \t]/.test(Source.Text[Index])) Advance();
            let Directive = "";
            while (Index < Source.Text.length && IsIdentifierContinue(Source.Text[Index])) Directive += Advance();
            if (Directive === "endif") {
                if (ConditionalDepth === 0) Fail(DirectiveLocation, "unmatched #endif");
                ConditionalDepth--;
            }
            let Continued = false;
            do {
                Continued = false;
                while (Index < Source.Text.length && Source.Text[Index] !== "\n") Advance();
                let Probe = Index - 1;
                while (Probe >= 0 && (Source.Text[Probe] === " " || Source.Text[Probe] === "\t" || Source.Text[Probe] === "\r")) Probe--;
                Continued = Probe >= 0 && Source.Text[Probe] === "\\";
                if (Index < Source.Text.length) Advance();
            } while (Continued && Index < Source.Text.length);
            if (Directive === "if" || Directive === "ifdef" || Directive === "ifndef") ConditionalDepth = StartDepth + 1;
            continue;
        }

        if (Starts("//")) {
            while (Index < Source.Text.length && Source.Text[Index] !== "\n") Advance();
            continue;
        }
        if (Starts("/*")) {
            const Start = Location(); Advance(); Advance();
            while (Index < Source.Text.length && !Starts("*/")) Advance();
            if (Index >= Source.Text.length) Fail(Start, "unterminated block comment");
            Advance(); Advance();
            continue;
        }

        const RawPrefixes = ["u8R\"", "uR\"", "UR\"", "LR\"", "R\""];
        const RawPrefix = RawPrefixes.find(Starts);
        if (RawPrefix) {
            const Start = Location();
            for (let Count = 0; Count < RawPrefix.length; Count++) Advance();
            let Delimiter = "";
            while (Index < Source.Text.length && Source.Text[Index] !== "(") {
                if (Source.Text[Index] === "\n" || Delimiter.length >= 16) Fail(Start, "invalid raw string delimiter");
                Delimiter += Advance();
            }
            if (Index >= Source.Text.length) Fail(Start, "unterminated raw string literal");
            Advance();
            const End = `)${Delimiter}\"`;
            while (Index < Source.Text.length && !Starts(End)) Advance();
            if (Index >= Source.Text.length) Fail(Start, "unterminated raw string literal");
            for (let Count = 0; Count < End.length; Count++) Advance();
            Tokens.push({ ...Start, Kind: "raw-string", Value: "", ConditionalDepth });
            continue;
        }

        const StringPrefixes = ["u8\"", "u\"", "U\"", "L\"", "\""];
        const StringPrefix = StringPrefixes.find(Starts);
        if (StringPrefix) {
            const Start = Location();
            for (let Count = 0; Count < StringPrefix.length; Count++) Advance();
            let Value = "";
            let Escaped = false;
            while (Index < Source.Text.length) {
                const Current = Advance();
                if (!Escaped && Current === "\"") break;
                if (!Escaped && Current === "\n") Fail(Start, "unterminated string literal");
                Value += Current;
                if (!Escaped && Current === "\\") Escaped = true;
                else Escaped = false;
            }
            if (Source.Text[Index - 1] !== "\"") Fail(Start, "unterminated string literal");
            Tokens.push({
                ...Start, Kind: "string", Value, ConditionalDepth,
                Prefix: StringPrefix.slice(0, -1),
            });
            continue;
        }

        const CharacterPrefixes = ["u'", "U'", "L'", "'"];
        const CharacterPrefix = CharacterPrefixes.find(Starts);
        if (CharacterPrefix) {
            const Start = Location();
            for (let Count = 0; Count < CharacterPrefix.length; Count++) Advance();
            let Escaped = false;
            while (Index < Source.Text.length) {
                const Current = Advance();
                if (!Escaped && Current === "'") break;
                if (!Escaped && Current === "\n") Fail(Start, "unterminated character literal");
                if (!Escaped && Current === "\\") Escaped = true;
                else Escaped = false;
            }
            if (Source.Text[Index - 1] !== "'") Fail(Start, "unterminated character literal");
            Tokens.push({ ...Start, Kind: "character", Value: "", ConditionalDepth });
            continue;
        }

        const Start = Location();
        if (IsIdentifierStart(Character)) {
            let Value = "";
            while (Index < Source.Text.length && IsIdentifierContinue(Source.Text[Index])) Value += Advance();
            if (ReflectionMacros.has(Value) && ConditionalDepth > 0) {
                Fail(Start, `${Value} is not supported inside a conditional preprocessing region`);
            }
            Tokens.push({ ...Start, Kind: "identifier", Value, ConditionalDepth });
            continue;
        }
        if (/[0-9]/.test(Character)) {
            let Value = "";
            while (Index < Source.Text.length && /[A-Za-z0-9_']/i.test(Source.Text[Index])) Value += Advance();
            Tokens.push({ ...Start, Kind: "number", Value, ConditionalDepth });
            continue;
        }
        const Two = Source.Text.slice(Index, Index + 2);
        if (["::", "&&", "||", "->", "<=", ">=", "==", "!=", "[[", "]]"].includes(Two)) {
            Advance(); Advance();
            Tokens.push({ ...Start, Kind: "symbol", Value: Two, ConditionalDepth });
        } else {
            Tokens.push({ ...Start, Kind: "symbol", Value: Advance(), ConditionalDepth });
        }
    }
    if (ConditionalDepth !== 0) Fail({ Path: Source.Path, Line, Column }, "unterminated conditional preprocessing region");
    return Tokens;
}

function IsStableId(Value: string): boolean {
    return /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(Value)
        && Value !== "00000000-0000-0000-0000-000000000000";
}

function ParseMacro(Tokens: Token[], Index: number, Name: string, HasId: boolean): { Id?: string; Next: number } {
    const Macro = Tokens[Index];
    if (Macro.Value !== Name) Fail(Macro, `expected ${Name}`);
    if (Tokens[Index + 1]?.Value !== "(") Fail(Macro, `${Name} requires parentheses`);
    if (HasId) {
        const Id = Tokens[Index + 2];
        if (!Id || Id.Kind !== "string" || Id.Prefix !== "" || Tokens[Index + 3]?.Value !== ")" || Id.Value.includes("\\")) {
            Fail(Macro, `${Name} requires exactly one ordinary UUID string literal`);
        }
        if (!IsStableId(Id.Value)) Fail(Id, `${Name} requires a lowercase canonical non-nil UUID`);
        return { Id: Id.Value, Next: Index + 4 };
    }
    if (Tokens[Index + 2]?.Value !== ")") Fail(Macro, `${Name} takes no arguments`);
    return { Next: Index + 3 };
}

function FindClosing(Tokens: Token[], Open: number, Left: string, Right: string): number {
    let Depth = 0;
    for (let Index = Open; Index < Tokens.length; Index++) {
        if (Tokens[Index].Value === Left) Depth++;
        else if (Tokens[Index].Value === Right && --Depth === 0) return Index;
    }
    Fail(Tokens[Open], `missing closing ${Right}`);
}

interface PendingProperty {
    Id: string;
    Name: string;
    TypeText: string;
    Namespace: string[];
    Location: SourceLocation;
}

interface MutableType extends Omit<ReflectedType, "Properties"> { PendingProperties: PendingProperty[]; }

const EnumKinds: Record<string, { Emitted: string; Bits: bigint; Signed: boolean }> = {
    int8: { Emitted: "Int8", Bits: 8n, Signed: true }, uint8: { Emitted: "UInt8", Bits: 8n, Signed: false },
    int16: { Emitted: "Int16", Bits: 16n, Signed: true }, uint16: { Emitted: "UInt16", Bits: 16n, Signed: false },
    int32: { Emitted: "Int32", Bits: 32n, Signed: true }, uint32: { Emitted: "UInt32", Bits: 32n, Signed: false },
    int64: { Emitted: "Int64", Bits: 64n, Signed: true }, uint64: { Emitted: "UInt64", Bits: 64n, Signed: false },
};

const BuiltinPropertyTypes: Record<string, string> = {
    bool: "Bool",
    int8: "Int8", "LE::int8": "Int8", "::LE::int8": "Int8",
    uint8: "UInt8", "LE::uint8": "UInt8", "::LE::uint8": "UInt8",
    int32: "Int32", "LE::int32": "Int32", "::LE::int32": "Int32",
    uint32: "UInt32", "LE::uint32": "UInt32", "::LE::uint32": "UInt32",
    int64: "Int64", "LE::int64": "Int64", "::LE::int64": "Int64",
    uint64: "UInt64", "LE::uint64": "UInt64", "::LE::uint64": "UInt64",
    float32: "Float32", "LE::float32": "Float32", "::LE::float32": "Float32",
    float64: "Float64", "LE::float64": "Float64", "::LE::float64": "Float64",
    String: "String", "LE::String": "String", "::LE::String": "String",
};

class Parser {
    private readonly Options: ReflectionGeneratorOptions;
    private readonly Types: MutableType[] = [];
    private readonly Enums: ReflectedEnum[] = [];

    public constructor(Options: ReflectionGeneratorOptions) { this.Options = Options; }

    public Parse(Sources: ReflectionSource[]): ReflectedModule {
        for (const Source of [...Sources].sort((A, B) => A.Include.localeCompare(B.Include))) {
            const Tokens = Lex(Source);
            this.ParseScope(Tokens, 0, Tokens.length, []);
        }
        this.ValidateAndResolve();
        return {
            Name: this.Options.ModuleName,
            EntryHeader: this.Options.EntryHeader,
            Inputs: [...Sources].map((Source) => Source.Include).sort(),
            Types: this.Types.map((Type) => ({
                Id: Type.Id, Name: Type.Name, QualifiedName: Type.QualifiedName, CppName: Type.CppName,
                Kind: Type.Kind,
                Properties: Type.PendingProperties.map((Property) => {
                    const Resolved = this.ResolvePropertyType(Property);
                    return {
                        Id: Property.Id, Name: Property.Name, ValueTypeName: Property.TypeText,
                        ...("BuiltinType" in Resolved
                            ? { BuiltinType: Resolved.BuiltinType }
                            : { ValueTypeId: Resolved.Id }),
                        Location: Property.Location,
                    };
                }),
                Location: Type.Location,
            })),
            Enums: this.Enums,
        };
    }

    private ParseScope(Tokens: Token[], Begin: number, End: number, Namespace: string[]): void {
        for (let Index = Begin; Index < End;) {
            const Token = Tokens[Index];
            if (Token.Value === "namespace") {
                Index = this.ParseNamespace(Tokens, Index, End, Namespace);
                continue;
            }
            if (Token.Value === "template") {
                for (let Probe = Index + 1; Probe < End; Probe++) {
                    if (ReflectionMacros.has(Tokens[Probe].Value)) {
                        Fail(Tokens[Probe], "reflected template declarations are outside the Task 13 supported subset");
                    }
                    if (Tokens[Probe].Value === ";" || Tokens[Probe].Value === "{") break;
                }
            }
            if (Token.Value === "LE_STRUCT" || Token.Value === "LE_CLASS") {
                Index = this.ParseType(Tokens, Index, End, Namespace);
                continue;
            }
            if (Token.Value === "LE_ENUM") {
                Index = this.ParseEnum(Tokens, Index, End, Namespace);
                continue;
            }
            if (ReflectionMacros.has(Token.Value)) Fail(Token, `${Token.Value} is not valid at namespace scope`);
            if (Token.Value === "{") {
                const Close = FindClosing(Tokens, Index, "{", "}");
                const NestedMacro = Tokens.slice(Index + 1, Close).find((Value) => ReflectionMacros.has(Value.Value));
                if (NestedMacro) Fail(NestedMacro, `${NestedMacro.Value} is only supported in a namespace or reflected type body`);
                Index = Close + 1;
                continue;
            }
            Index++;
        }
    }

    private ParseNamespace(Tokens: Token[], Index: number, End: number, Parent: string[]): number {
        const Start = Tokens[Index++];
        const Names: string[] = [];
        if (Tokens[Index]?.Value === "{") {
            const Close = FindClosing(Tokens, Index, "{", "}");
            const Reflected = Tokens.slice(Index + 1, Close).find((Value) => ReflectionMacros.has(Value.Value));
            if (Reflected) Fail(Reflected, "reflected declarations in anonymous namespaces are outside the Task 13 supported subset");
            return Close + 1;
        }
        while (Index < End) {
            const Name = Tokens[Index];
            if (Name?.Kind !== "identifier") Fail(Name ?? Start, "unsupported namespace declaration");
            Names.push(Name.Value); Index++;
            if (Tokens[Index]?.Value !== "::") break;
            Index++;
        }
        if (Tokens[Index]?.Value !== "{") Fail(Tokens[Index] ?? Start, "namespace aliases are not supported by the reflection generator");
        const Close = FindClosing(Tokens, Index, "{", "}");
        this.ParseScope(Tokens, Index + 1, Close, [...Parent, ...Names]);
        return Close + 1;
    }

    private ParseType(Tokens: Token[], Index: number, End: number, Namespace: string[]): number {
        const MacroName = Tokens[Index].Value;
        const Macro = ParseMacro(Tokens, Index, MacroName, true);
        Index = Macro.Next;
        const Expected = MacroName === "LE_STRUCT" ? "struct" : "class";
        if (Tokens[Index]?.Value !== Expected) Fail(Tokens[indexOr(Index, Tokens)] ?? Tokens[Index - 1], `${MacroName} must immediately precede a ${Expected} declaration`);
        Index++;
        if (this.Options.ApiMacro && Tokens[Index]?.Value === this.Options.ApiMacro) Index++;
        const Name = Tokens[Index];
        if (!Name || Name.Kind !== "identifier") Fail(Name ?? Tokens[Index - 1], `expected reflected ${Expected} name${this.Options.ApiMacro ? ` after optional ${this.Options.ApiMacro}` : ""}`);
        Index++;
        if (Tokens[Index]?.Value === "final") Index++;
        if (Tokens[Index]?.Value === ":") Fail(Tokens[Index], "reflected base classes are outside the Task 13 supported subset");
        if (Tokens[Index]?.Value !== "{") Fail(Tokens[Index] ?? Name, "templates, attributes, and complex reflected declarations are outside the Task 13 supported subset");
        const Open = Index;
        const Close = FindClosing(Tokens, Open, "{", "}");
        if (Close >= End) Fail(Name, "reflected declaration crosses its namespace scope");
        const PendingProperties: PendingProperty[] = [];
        let BodyCount = 0;
        for (let Body = Open + 1; Body < Close;) {
            const Current = Tokens[Body];
            if (Current.Value === "{") {
                const NestedClose = FindClosing(Tokens, Body, "{", "}");
                const NestedMacro = Tokens.slice(Body + 1, NestedClose).find((Value) => ReflectionMacros.has(Value.Value));
                if (NestedMacro) Fail(NestedMacro, `${NestedMacro.Value} is not valid in a nested scope of a reflected type`);
                Body = NestedClose + 1;
                continue;
            }
            if (Current.Value === "LE_GENERATED_BODY") {
                const Parsed = ParseMacro(Tokens, Body, "LE_GENERATED_BODY", false);
                BodyCount++; Body = Parsed.Next; continue;
            }
            if (Current.Value === "LE_PROPERTY") {
                const Parsed = ParseMacro(Tokens, Body, "LE_PROPERTY", true);
                const Property = this.ParseProperty(Tokens, Parsed.Next, Close, Namespace, Parsed.Id!, Current);
                PendingProperties.push(Property.Value); Body = Property.Next; continue;
            }
            if (Current.Value === "LE_STRUCT" || Current.Value === "LE_CLASS" || Current.Value === "LE_ENUM") {
                Fail(Current, "nested reflected declarations are outside the Task 13 supported subset");
            }
            Body++;
        }
        if (BodyCount !== 1) Fail(Name, `reflected ${Expected} must contain exactly one LE_GENERATED_BODY(), found ${BodyCount}`);
        const CppName = `::${[...Namespace, Name.Value].join("::")}`;
        const QualifiedName = [...Namespace, Name.Value].join(".");
        if (Tokens[Close + 1]?.Value !== ";") Fail(Tokens[Close + 1] ?? Tokens[Close], "reflected type declaration must end with ';'");
        this.Types.push({
            Id: Macro.Id!, Name: Name.Value, QualifiedName, CppName,
            Kind: Expected === "struct" ? "Struct" : "Class", PendingProperties,
            Location: Name,
        });
        return Close + 1;
    }

    private ParseProperty(
        Tokens: Token[], Index: number, End: number, Namespace: string[], Id: string, Macro: Token,
    ): { Value: PendingProperty; Next: number } {
        const Declaration: Token[] = [];
        let Paren = 0;
        for (; Index < End; Index++) {
            const Token = Tokens[Index];
            if (Token.Value === "(") Paren++;
            if (Token.Value === ")") Paren--;
            if (Token.Value === ";" && Paren === 0) break;
            Declaration.push(Token);
        }
        if (Index >= End) Fail(Macro, "LE_PROPERTY must immediately precede one data member ending in ';'");
        const Cut = Declaration.findIndex((Token) => Token.Value === "=" || Token.Value === "{");
        const Head = Cut >= 0 ? Declaration.slice(0, Cut) : Declaration;
        if (Head.some((Token) => ["static", "(", ")", "[", "]", ",", ":", "*", "&", "&&"].includes(Token.Value))) {
            Fail(Macro, "Task 13 properties must be one non-static, non-array, non-bitfield value data member");
        }
        const NameIndex = Head.map((Token) => Token.Kind).lastIndexOf("identifier");
        if (NameIndex <= 0 || NameIndex !== Head.length - 1) Fail(Macro, "cannot determine the reflected property name");
        const Name = Head[NameIndex];
        const TypeTokens = Head.slice(0, NameIndex);
        if (TypeTokens.length === 0 || TypeTokens.some((Token) => Token.Kind !== "identifier" && Token.Value !== "::")) {
            Fail(TypeTokens[0] ?? Macro, "property value type must name a reflected type in this generator batch");
        }
        return {
            Value: { Id, Name: Name.Value, TypeText: TypeTokens.map((Token) => Token.Value).join(""), Namespace, Location: Name },
            Next: Index + 1,
        };
    }

    private ParseEnum(Tokens: Token[], Index: number, End: number, Namespace: string[]): number {
        const MacroToken = Tokens[Index];
        const Macro = ParseMacro(Tokens, Index, "LE_ENUM", true); Index = Macro.Next;
        if (Tokens[Index]?.Value !== "enum" || !["class", "struct"].includes(Tokens[Index + 1]?.Value)) {
            Fail(Tokens[Index] ?? MacroToken, "LE_ENUM must immediately precede a scoped enum class/struct declaration");
        }
        Index += 2;
        if (this.Options.ApiMacro && Tokens[Index]?.Value === this.Options.ApiMacro) Index++;
        const Name = Tokens[Index];
        if (!Name || Name.Kind !== "identifier") Fail(Name ?? MacroToken, "expected reflected enum name");
        Index++;
        if (Tokens[Index]?.Value !== ":") Fail(Tokens[Index] ?? Name, "reflected enum requires an explicit fixed underlying type");
        Index++;
        const Underlying = Tokens[Index++];
        const Kind = Underlying && EnumKinds[Underlying.Value];
        if (!Kind) Fail(Underlying ?? Name, "unsupported enum underlying type; use int8/uint8/int16/uint16/int32/uint32/int64/uint64");
        if (Tokens[Index]?.Value !== "{") Fail(Tokens[Index] ?? Underlying, "expected reflected enum body");
        const Close = FindClosing(Tokens, Index, "{", "}");
        if (Close >= End) Fail(Name, "reflected enum crosses its namespace scope");
        const Values: ReflectedEnumValue[] = [];
        Index++;
        while (Index < Close) {
            if (Tokens[Index].Value === ",") { Index++; continue; }
            const ValueName = Tokens[Index++];
            if (ValueName.Kind !== "identifier") Fail(ValueName, "expected enum value name");
            if (Tokens[Index]?.Value !== "=") Fail(Tokens[Index] ?? ValueName, "every reflected enum value requires an explicit integer literal");
            Index++;
            let Negative = false;
            if (Tokens[Index]?.Value === "-" || Tokens[Index]?.Value === "+") { Negative = Tokens[Index].Value === "-"; Index++; }
            const NumberToken = Tokens[Index++];
            if (!NumberToken || NumberToken.Kind !== "number") Fail(NumberToken ?? ValueName, "enum value must be an integer literal");
            if (Index < Close && Tokens[Index].Value !== ",") Fail(Tokens[Index], "arbitrary enum constant expressions are outside the Task 13 subset");
            let Numeric: bigint;
            try {
                const Clean = NumberToken.Value.replaceAll("'", "");
                if (!/^(?:0[xX][0-9a-fA-F]+|[0-9]+)$/.test(Clean)) throw new Error();
                Numeric = BigInt(Clean) * (Negative ? -1n : 1n);
            } catch { Fail(NumberToken, "invalid enum integer literal"); }
            const Bits = Kind.Bits;
            const Minimum = Kind.Signed ? -(1n << (Bits - 1n)) : 0n;
            const Maximum = Kind.Signed ? (1n << (Bits - 1n)) - 1n : (1n << Bits) - 1n;
            if (Numeric! < Minimum || Numeric! > Maximum) Fail(NumberToken, `enum value is out of range for ${Underlying.Value}`);
            const Raw = Numeric! < 0n ? (1n << Bits) + Numeric! : Numeric!;
            Values.push({ Name: ValueName.Value, ValueBits: Raw, Location: ValueName });
        }
        const CppName = `::${[...Namespace, Name.Value].join("::")}`;
        if (Tokens[Close + 1]?.Value !== ";") Fail(Tokens[Close + 1] ?? Tokens[Close], "reflected enum declaration must end with ';'");
        this.Enums.push({
            Id: Macro.Id!, Name: Name.Value, QualifiedName: [...Namespace, Name.Value].join("."), CppName,
            UnderlyingType: Kind.Emitted, Values, Location: Name,
        });
        return Close + 1;
    }

    private ValidateAndResolve(): void {
        const All = [...this.Types, ...this.Enums];
        const Ids = new Map<string, SourceLocation>();
        const Names = new Map<string, SourceLocation>();
        for (const Type of All) {
            const ExistingId = Ids.get(Type.Id);
            if (ExistingId) Fail(Type.Location, `duplicate reflected type ID ${Type.Id}; first declared at ${ExistingId.Path}:${ExistingId.Line}:${ExistingId.Column}`);
            Ids.set(Type.Id, Type.Location);
            const ExistingName = Names.get(Type.QualifiedName);
            if (ExistingName) Fail(Type.Location, `duplicate reflected qualified name ${Type.QualifiedName}; first declared at ${ExistingName.Path}:${ExistingName.Line}:${ExistingName.Column}`);
            Names.set(Type.QualifiedName, Type.Location);
            if ("PendingProperties" in Type) {
                const PropertyIds = new Map<string, SourceLocation>();
                const PropertyNames = new Map<string, SourceLocation>();
                for (const Property of Type.PendingProperties) {
                    if (PropertyIds.has(Property.Id)) Fail(Property.Location, `duplicate property ID ${Property.Id} in ${Type.QualifiedName}`);
                    if (PropertyNames.has(Property.Name)) Fail(Property.Location, `duplicate property name ${Property.Name} in ${Type.QualifiedName}`);
                    PropertyIds.set(Property.Id, Property.Location); PropertyNames.set(Property.Name, Property.Location);
                    this.ResolvePropertyType(Property);
                }
            } else {
                const ValueNames = new Set<string>();
                for (const Value of Type.Values) {
                    if (ValueNames.has(Value.Name)) Fail(Value.Location, `duplicate enum value name ${Value.Name} in ${Type.QualifiedName}`);
                    ValueNames.add(Value.Name);
                }
            }
        }
    }

    private ResolvePropertyType(Property: PendingProperty): MutableType | ReflectedEnum | { BuiltinType: string } {
        const BuiltinType = BuiltinPropertyTypes[Property.TypeText];
        if (BuiltinType) return { BuiltinType };
        const Text = Property.TypeText.replace(/^::/, "");
        const Candidates = Text.includes("::")
            ? [Text]
            : Property.Namespace.map((_Value, Index) => [...Property.Namespace.slice(0, Property.Namespace.length - Index), Text].join("::"))
                .concat(Text);
        for (const Candidate of Candidates) {
            const Match = [...this.Types, ...this.Enums].find((Type) => Type.CppName.slice(2) === Candidate);
            if (Match) return Match;
        }
        Fail(Property.Location, `property type '${Property.TypeText}' is neither a canonical builtin property type nor a reflected type in this generator batch`);
    }
}

function indexOr(Index: number, Tokens: Token[]): number { return Math.min(Index, Tokens.length - 1); }

export function ParseReflectionModule(Sources: ReflectionSource[], Options: ReflectionGeneratorOptions): ReflectedModule {
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(Options.ModuleName)) {
        throw new Error(`Invalid reflection module name '${Options.ModuleName}'.`);
    }
    if (Options.ApiMacro && !/^[A-Za-z_][A-Za-z0-9_]*$/.test(Options.ApiMacro)) {
        throw new Error(`Invalid API macro '${Options.ApiMacro}'.`);
    }
    return new Parser(Options).Parse(Sources);
}

function EscapeCpp(Value: string): string { return JSON.stringify(Value); }

export function EmitReflectionModule(Module: ReflectedModule): string {
    const Includes = [Module.EntryHeader, ...Module.Inputs]
        .filter((Value, Index, Values) => Values.indexOf(Value) === Index);
    const Lines: string[] = [
        "// Generated by Limitless ReflectionGenerator v1. Do not edit.",
        `// Inputs: ${Module.Inputs.join(", ")}`,
        ...Includes.map((Input) => `#include ${EscapeCpp(Input)}`),
        "#include \"Reflection/ReflectionThunks.h\"",
        "#include \"Reflection/PropertyValue.h\"",
        "",
        "#include <cstdint>",
        "#include <new>",
        "#include <type_traits>",
        "#include <utility>",
        "",
        "namespace LE::Detail",
        "{",
    ];
    for (const Type of Module.Types) {
        Lines.push(
            "", "template <>", `struct TReflectionGeneratedAccess<${Type.CppName}>`, "{",
            `    using FType = ${Type.CppName};`,
            "    static_assert(noexcept(FType()), \"reflected types must be nothrow default constructible\");",
            "    static_assert(noexcept(std::declval<FType&>().~FType()), \"reflected types must be nothrow destructible\");",
            "",
            "    static bool Construct(void* Storage) noexcept",
            "    {",
            "        if (!Storage || reinterpret_cast<std::uintptr_t>(Storage) % alignof(FType) != 0) return false;",
            "        ::new (Storage) FType();",
            "        return true;",
            "    }",
            "",
            "    static bool Destruct(void* Storage) noexcept",
            "    {",
            "        if (!Storage || reinterpret_cast<std::uintptr_t>(Storage) % alignof(FType) != 0) return false;",
            "        static_cast<FType*>(Storage)->~FType();",
            "        return true;",
            "    }",
        );
        for (let Index = 0; Index < Type.Properties.length; Index++) {
            const Property = Type.Properties[Index];
            Lines.push(
                "", `    using FProperty${Index} = decltype(FType::${Property.Name});`,
                `    static const void* GetProperty${Index}(const void* Object) noexcept`, "    {",
                "        if (!Object || reinterpret_cast<std::uintptr_t>(Object) % alignof(FType) != 0) return nullptr;",
                `        return &(static_cast<const FType*>(Object)->${Property.Name});`, "    }",
                "    template <typename FValue>",
                `    static bool SetProperty${Index}Impl(void* Object, const void* Value) noexcept`, "    {",
                `        if constexpr (!std::is_nothrow_assignable_v<FProperty${Index}&, const FValue&>)`, "        {",
                "            (void)Object; (void)Value; return false;", "        }", "        else", "        {",
                `            if (!Object || !Value || reinterpret_cast<std::uintptr_t>(Object) % alignof(FType) != 0 || reinterpret_cast<std::uintptr_t>(Value) % alignof(FValue) != 0) return false;`,
                `            static_cast<FType*>(Object)->${Property.Name} = *static_cast<const FValue*>(Value);`,
                "            return true;", "        }", "    }",
                `    static constexpr FReflectionPropertySetterThunk PropertySetter${Index}() noexcept`, "    {",
                `        if constexpr (std::is_nothrow_assignable_v<FProperty${Index}&, const FProperty${Index}&>) return &SetProperty${Index}Impl<FProperty${Index}>;`,
                "        else return nullptr;", "    }",
            );
        }
        Lines.push("};");
    }
    Lines.push("", "} // namespace LE::Detail", "", "namespace LE", "{", "");
    Lines.push(
        `EReflectionRegisterResult Register${Module.Name}Reflection(`,
        "    FReflectionRegistry& Registry, FReflectionModuleHandle& OutHandle) noexcept",
        "{",
    );
    for (let TypeIndex = 0; TypeIndex < Module.Types.length; TypeIndex++) {
        const Type = Module.Types[TypeIndex];
        if (Type.Properties.length > 0) Lines.push(`    FPropertyDescriptor Type${TypeIndex}Properties[${Type.Properties.length}]{};`);
        for (let PropertyIndex = 0; PropertyIndex < Type.Properties.length; PropertyIndex++) {
            const Property = Type.Properties[PropertyIndex];
            Lines.push(
                `    if (!FPropertyId::TryParse(${EscapeCpp(Property.Id)}, Type${TypeIndex}Properties[${PropertyIndex}].Id))`,
                "        return EReflectionRegisterResult::InvalidDescriptor;",
                ...(Property.BuiltinType
                    ? [`    Type${TypeIndex}Properties[${PropertyIndex}].ValueTypeId = GetBuiltinPropertyTypeId(EPropertyBuiltinType::${Property.BuiltinType});`]
                    : [
                        `    if (!FTypeId::TryParse(${EscapeCpp(Property.ValueTypeId!)}, Type${TypeIndex}Properties[${PropertyIndex}].ValueTypeId))`,
                        "        return EReflectionRegisterResult::InvalidDescriptor;",
                    ]),
                `    Type${TypeIndex}Properties[${PropertyIndex}].Name = ${EscapeCpp(Property.Name)};`,
                `    Type${TypeIndex}Properties[${PropertyIndex}].Getter = &Detail::TReflectionGeneratedAccess<${Type.CppName}>::GetProperty${PropertyIndex};`,
                `    Type${TypeIndex}Properties[${PropertyIndex}].Setter = Detail::TReflectionGeneratedAccess<${Type.CppName}>::PropertySetter${PropertyIndex}();`,
            );
        }
    }
    if (Module.Types.length > 0) Lines.push(`    FTypeDescriptor Types[${Module.Types.length}]{};`);
    for (let Index = 0; Index < Module.Types.length; Index++) {
        const Type = Module.Types[Index];
        Lines.push(
            `    if (!FTypeId::TryParse(${EscapeCpp(Type.Id)}, Types[${Index}].Id)) return EReflectionRegisterResult::InvalidDescriptor;`,
            `    Types[${Index}].Name = ${EscapeCpp(Type.QualifiedName)};`,
            `    Types[${Index}].Kind = EReflectedTypeKind::${Type.Kind};`,
            `    Types[${Index}].Size = sizeof(${Type.CppName});`,
            `    Types[${Index}].Alignment = alignof(${Type.CppName});`,
            ...(Type.Properties.length > 0 ? [`    Types[${Index}].Properties = Span<const FPropertyDescriptor>(Type${Index}Properties, ${Type.Properties.length});`] : []),
            `    Types[${Index}].Construct = &Detail::TReflectionGeneratedAccess<${Type.CppName}>::Construct;`,
            `    Types[${Index}].Destruct = &Detail::TReflectionGeneratedAccess<${Type.CppName}>::Destruct;`,
        );
    }
    for (let EnumIndex = 0; EnumIndex < Module.Enums.length; EnumIndex++) {
        const Enum = Module.Enums[EnumIndex];
        if (Enum.Values.length > 0) {
            Lines.push(`    FEnumValueDescriptor Enum${EnumIndex}Values[${Enum.Values.length}]{};`);
            Enum.Values.forEach((Value, ValueIndex) => Lines.push(
                `    Enum${EnumIndex}Values[${ValueIndex}].Name = ${EscapeCpp(Value.Name)};`,
                `    Enum${EnumIndex}Values[${ValueIndex}].ValueBits = UINT64_C(${Value.ValueBits.toString()});`,
            ));
        }
    }
    if (Module.Enums.length > 0) Lines.push(`    FEnumDescriptor Enums[${Module.Enums.length}]{};`);
    for (let Index = 0; Index < Module.Enums.length; Index++) {
        const Enum = Module.Enums[Index];
        Lines.push(
            `    if (!FTypeId::TryParse(${EscapeCpp(Enum.Id)}, Enums[${Index}].Id)) return EReflectionRegisterResult::InvalidDescriptor;`,
            `    Enums[${Index}].Name = ${EscapeCpp(Enum.QualifiedName)};`,
            `    Enums[${Index}].UnderlyingType = EEnumUnderlyingType::${Enum.UnderlyingType};`,
            ...(Enum.Values.length > 0 ? [`    Enums[${Index}].Values = Span<const FEnumValueDescriptor>(Enum${Index}Values, ${Enum.Values.length});`] : []),
        );
    }
    Lines.push(
        "    FReflectionModuleDescriptor Module;",
        `    Module.Name = ${EscapeCpp(Module.Name)};`,
        ...(Module.Types.length > 0 ? [`    Module.Types = Span<const FTypeDescriptor>(Types, ${Module.Types.length});`] : []),
        ...(Module.Enums.length > 0 ? [`    Module.Enums = Span<const FEnumDescriptor>(Enums, ${Module.Enums.length});`] : []),
        "    return Registry.RegisterModule(Module, OutHandle);",
        "}", "",
        `EReflectionUnregisterResult Unregister${Module.Name}Reflection(`,
        "    FReflectionRegistry& Registry, const FReflectionModuleHandle Handle) noexcept",
        "{", "    return Registry.UnregisterModule(Handle);", "}", "", "} // namespace LE", "",
    );
    return Lines.join("\n");
}

export async function WriteFileIfChanged(Output: string, Content: string): Promise<boolean> {
    let Existing: string | undefined;
    try { Existing = await Fs.readFile(Output, "utf-8"); } catch { Existing = undefined; }
    if (Existing === Content) return false;
    await Fs.mkdir(Path.dirname(Output), { recursive: true });
    const Temporary = `${Output}.${process.pid}.${Date.now()}.tmp`;
    const Backup = `${Output}.${process.pid}.${Date.now()}.bak`;
    await Fs.writeFile(Temporary, Content, { encoding: "utf-8", flag: "wx" });
    let MovedOld = false;
    try {
        if (Existing !== undefined) { await Fs.rename(Output, Backup); MovedOld = true; }
        await Fs.rename(Temporary, Output);
        if (MovedOld) await Fs.rm(Backup, { force: true });
    } catch (Error) {
        await Fs.rm(Temporary, { force: true });
        if (MovedOld) {
            try { await Fs.rename(Backup, Output); } catch { /* preserve the original error */ }
        }
        throw Error;
    }
    return true;
}
