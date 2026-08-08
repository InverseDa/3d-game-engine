import { readdir, readFile } from "node:fs/promises";
import { extname, relative, resolve } from "node:path";

export type GovernanceRule =
    | "banned-std-owner"
    | "banned-glm"
    | "namespace-limitless"
    | "namespace-scope"
    | "global-engine-declaration"
    | "global-engine-alias"
    | "public-using-namespace";

export interface GovernanceDiagnostic {
    rule: GovernanceRule;
    file: string;
    line: number;
    symbol: string;
    fragment: string;
}

type TokenKind = "identifier" | "number" | "string" | "punctuation" | "preprocessor";
interface Token { kind: TokenKind; value: string; line: number; }
interface Scope { kind: "namespace" | "class" | "function" | "other"; name: string; namespacePath: string; }

const CPP_EXTENSIONS = new Set([".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx", ".m", ".mm"]);
const CONFIG_EXTENSIONS = new Set([".ts", ".json"]);
const HEADER_EXTENSIONS = new Set([".h", ".hpp", ".hh"]);
const APPROVED_NAMESPACES = new Set([
    "LE",
    "LE::Detail",
    "LE::Math",
    "LE::RAL",
    "LE::RAL::Vulkan",
    "LE::RendererDemoPasses",
    "LE::Launch::ShaderRuntimeCompiler",
]);
const CONTROL_WORDS = new Set(["if", "for", "while", "switch", "catch"]);
const OWNER_NAMES = new Set([
    "array", "vector", "deque", "list", "map", "set", "unordered_map", "unordered_set",
    "basic_string", "string", "wstring", "u8string", "u16string", "u32string",
    "unique_ptr", "shared_ptr", "weak_ptr", "optional", "function", "any", "variant",
    "make_unique", "make_shared", "allocate_shared", "ifstream", "ofstream", "fstream",
    "stringstream", "istringstream", "ostringstream",
]);

const normalize = (path: string): string => path.replaceAll("\\", "/");

function isIdentifierStart(char: string): boolean { return /[A-Za-z_]/.test(char); }
function isIdentifierPart(char: string): boolean { return /[A-Za-z0-9_]/.test(char); }

/** A deliberately small C++ lexer: comments and literals are tokens, never executable identifiers. */
export function tokenizeCpp(source: string): Token[] {
    const tokens: Token[] = [];
    let index = 0;
    let line = 1;
    let atLineStart = true;
    const advance = (): string => {
        const char = source[index++];
        if (char === "\n") { ++line; atLineStart = true; }
        else if (!/\s/.test(char)) { atLineStart = false; }
        return char;
    };
    while (index < source.length) {
        const char = source[index];
        if (/\s/.test(char)) { advance(); continue; }
        if (atLineStart && char === "#") {
            const startLine = line;
            let value = "";
            do {
                const current = advance();
                value += current;
                if (current === "\n" && !value.trimEnd().endsWith("\\")) { break; }
            } while (index < source.length);
            tokens.push({ kind: "preprocessor", value, line: startLine });
            continue;
        }
        if (source.startsWith("//", index)) {
            while (index < source.length && advance() !== "\n") { /* skip */ }
            continue;
        }
        if (source.startsWith("/*", index)) {
            advance(); advance();
            while (index < source.length && !source.startsWith("*/", index)) { advance(); }
            if (index < source.length) { advance(); advance(); }
            continue;
        }
        const literalMatch = source.slice(index).match(/^(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(/);
        if (literalMatch) {
            const startLine = line;
            const terminator = `)${literalMatch[1]}"`;
            let value = "";
            while (index < source.length) {
                if (source.startsWith(terminator, index)) {
                    for (let count = 0; count < terminator.length; ++count) { value += advance(); }
                    break;
                }
                value += advance();
            }
            tokens.push({ kind: "string", value, line: startLine });
            continue;
        }
        const prefixLength = source.startsWith("u8\"", index) ? 2
            : /[uUL]/.test(char) && source[index + 1] === "\"" ? 1 : 0;
        if (char === "\"" || char === "'" || prefixLength > 0) {
            const startLine = line;
            let value = "";
            for (let count = 0; count < prefixLength; ++count) { value += advance(); }
            const quote = source[index];
            value += advance();
            while (index < source.length) {
                const current = advance();
                value += current;
                if (current === "\\" && index < source.length) { value += advance(); continue; }
                if (current === quote) { break; }
            }
            tokens.push({ kind: "string", value, line: startLine });
            continue;
        }
        if (isIdentifierStart(char)) {
            const startLine = line;
            let value = "";
            while (index < source.length && isIdentifierPart(source[index])) { value += advance(); }
            tokens.push({ kind: "identifier", value, line: startLine });
            continue;
        }
        if (/[0-9]/.test(char)) {
            const startLine = line;
            let value = "";
            while (index < source.length && /[A-Za-z0-9_.']/.test(source[index])) { value += advance(); }
            tokens.push({ kind: "number", value, line: startLine });
            continue;
        }
        const startLine = line;
        const pair = source.slice(index, index + 2);
        if (["::", "->", "[[", "]]", "<=", ">=", "==", "!=", "&&", "||", "++", "--"].includes(pair)) {
            advance(); advance(); tokens.push({ kind: "punctuation", value: pair, line: startLine });
        } else {
            tokens.push({ kind: "punctuation", value: advance(), line: startLine });
        }
    }
    return tokens;
}

function qualifierBefore(tokens: Token[], index: number): string[] {
    const result: string[] = [tokens[index].value];
    let cursor = index - 1;
    while (cursor >= 1 && tokens[cursor].value === "::" && tokens[cursor - 1].kind === "identifier") {
        result.unshift(tokens[cursor - 1].value);
        cursor -= 2;
    }
    return result;
}

function functionName(header: Token[]): string | undefined {
    let close = -1;
    for (let index = header.length - 1; index >= 0; --index) {
        if (header[index].value === ")") { close = index; break; }
    }
    if (close < 0) { return undefined; }
    let depth = 0;
    let open = -1;
    for (let index = close; index >= 0; --index) {
        if (header[index].value === ")") { ++depth; }
        else if (header[index].value === "(" && --depth === 0) { open = index; break; }
    }
    if (open <= 0) { return undefined; }
    const nameToken = header[open - 1];
    if (nameToken.kind !== "identifier" || CONTROL_WORDS.has(nameToken.value)) { return undefined; }
    return qualifierBefore(header, open - 1).join("::");
}

function className(header: Token[]): string | undefined {
    const marker = header.findIndex((token) => ["class", "struct", "union", "enum"].includes(token.value));
    if (marker < 0) { return undefined; }
    let index = marker + 1;
    if (header[marker].value === "enum" && header[index]?.value === "class") { ++index; }
    while (index < header.length) {
        if (header[index].kind !== "identifier") { ++index; continue; }
        if (header[index].value.endsWith("_API")) { ++index; continue; }
        if (header[index].value === "alignas" && header[index + 1]?.value === "(") {
            ++index;
            let depth = 0;
            do {
                if (header[index].value === "(") { ++depth; }
                else if (header[index].value === ")") { --depth; }
                ++index;
            } while (index < header.length && depth > 0);
            continue;
        }
        return header[index].value;
    }
    return undefined;
}

function statementSymbol(tokens: Token[], fallback: string): string {
    const fn = functionName(tokens);
    if (fn) { return fn; }
    const type = className(tokens);
    if (type) { return type; }
    for (let index = tokens.length - 1; index >= 0; --index) {
        if (tokens[index].kind === "identifier" && !["const", "override", "final", "noexcept"].includes(tokens[index].value)) {
            return tokens[index].value;
        }
    }
    return fallback;
}

function aliasSymbol(tokens: Token[], fallback: string): string {
    const usingIndex = tokens.findIndex((token) => token.value === "using");
    if (usingIndex >= 0 && tokens[usingIndex + 1]?.kind === "identifier" && tokens[usingIndex + 1].value !== "namespace") {
        return tokens[usingIndex + 1].value;
    }
    if (tokens.some((token) => token.value === "typedef")) {
        for (let index = tokens.length - 1; index >= 0; --index) {
            if (tokens[index].kind === "identifier") { return tokens[index].value; }
        }
    }
    return fallback;
}

function enclosingSymbol(scopes: Scope[], statement: Token[], fallback: string): string {
    for (let index = scopes.length - 1; index >= 0; --index) {
        if (scopes[index].kind === "function") { return scopes[index].name; }
    }
    const inferred = statementSymbol(statement, "");
    if (inferred) { return inferred; }
    const classes = scopes.filter((scope) => scope.kind === "class").map((scope) => scope.name);
    return classes.at(-1) ?? fallback;
}

function occurrenceSymbol(scopes: Scope[], statement: Token[], tokens: Token[], index: number, fallback: string): string {
    for (let cursor = scopes.length - 1; cursor >= 0; --cursor) {
        if (scopes[cursor].kind === "function") { return scopes[cursor].name; }
    }
    const tail: Token[] = [];
    for (let cursor = index; cursor < tokens.length && ![";", "{"].includes(tokens[cursor].value); ++cursor) {
        tail.push(tokens[cursor]);
    }
    const inferred = statementSymbol([...statement, ...tail], fallback);
    const containingClass = [...scopes].reverse().find((scope) => scope.kind === "class")?.name;
    return containingClass && functionName([...statement, ...tail]) ? `${containingClass}::${inferred}` : inferred;
}

interface Allow { file: string; symbol: string; owners: Set<string>; }
const OWNER_ALLOWLIST: Allow[] = [
    { file: "Engine/Source/Runtime/Core/Private/Logger/Log.cpp", symbol: "s_Sinks", owners: new Set(["vector"]) },
    { file: "Engine/Source/Runtime/Core/Private/Logger/Log.cpp", symbol: "LevelTagFormatter::clone", owners: new Set(["unique_ptr", "make_unique"]) },
    { file: "Engine/Source/Runtime/Core/Private/Logger/Log.cpp", symbol: "LevelAnsiBeginFormatter::clone", owners: new Set(["unique_ptr", "make_unique"]) },
    { file: "Engine/Source/Runtime/Core/Private/Logger/Log.cpp", symbol: "LevelAnsiEndFormatter::clone", owners: new Set(["unique_ptr", "make_unique"]) },
    { file: "Engine/Source/Runtime/Core/Private/Logger/Log.cpp", symbol: "Log::Init", owners: new Set(["string", "unique_ptr", "shared_ptr", "make_unique", "make_shared"]) },
    { file: "Engine/Source/Runtime/Core/Private/Logger/Log.cpp", symbol: "Log::GetLoggerOrCreate", owners: new Set(["string", "make_shared"]) },
    { file: "Engine/Source/Runtime/Launch/Private/Windows/LaunchWindows.cpp", symbol: "TryReadShaderFile", owners: new Set(["ifstream"]) },
    { file: "Engine/Source/Runtime/Launch/Private/Mac/LaunchMac.cpp", symbol: "TryReadShaderFile", owners: new Set(["ifstream"]) },
    { file: "Engine/Source/Runtime/Launch/Private/ShaderRuntimeCompiler.h", symbol: "ReadBinaryFile", owners: new Set(["ifstream"]) },
    { file: "Engine/Source/Runtime/Launch/Private/ShaderRuntimeCompiler.h", symbol: "CompileHlslToSpirv", owners: new Set(["filesystem::path", "error_code", "path::string"]) },
];

function isOwnerAllowed(file: string, symbol: string, owner: string): boolean {
    return OWNER_ALLOWLIST.some((entry) => entry.file === file && entry.symbol === symbol && entry.owners.has(owner));
}

function diagnostic(rule: GovernanceRule, file: string, token: Token, symbol: string, fragment: string): GovernanceDiagnostic {
    return { rule, file, line: token.line, symbol, fragment };
}

function namespaceAt(scopes: Scope[]): string {
    return scopes.filter((scope) => scope.kind === "namespace" && scope.name !== "<anonymous>").at(-1)?.namespacePath ?? "";
}

function isPublicHeader(file: string): boolean {
    return file.includes("/Public/") && HEADER_EXTENSIONS.has(extname(file).toLowerCase());
}

function isAllowedGlobal(file: string, symbol: string): boolean {
    return (file === "Engine/Source/Runtime/Launch/Private/Launch.cpp" && ["main", "WinMain"].includes(symbol))
        || (file === "Engine/Source/Tests/BuilderTests/Private/BuilderTests.cpp" && symbol === "main")
        || (file === "Engine/Source/Runtime/Launch/Private/Mac/MacWindow.mm" && symbol === "FMacWindowDelegate");
}

function isApprovedSpdlogForward(file: string, statement: Token[]): boolean {
    const identifiers = statement.filter((token) => token.kind === "identifier").map((token) => token.value);
    return file === "Engine/Source/Runtime/Core/Public/Logger/Log.h"
        && identifiers.length === 2 && identifiers[0] === "class" && identifiers[1] === "logger";
}

export function scanCppSource(filePath: string, source: string): GovernanceDiagnostic[] {
    const file = normalize(filePath);
    const tokens = tokenizeCpp(source);
    const result: GovernanceDiagnostic[] = [];
    const scopes: Scope[] = [];
    let statement: Token[] = [];
    let objcScope = "";

    for (let index = 0; index < tokens.length; ++index) {
        const token = tokens[index];
        if (token.kind === "preprocessor") {
            const directiveTokens = tokenizeCpp(token.value.replace(/^\s*#/, " "));
            const usesGlm = directiveTokens.some((value) => value.kind === "identifier" && value.value.toLowerCase().includes("glm"));
            if (usesGlm) {
                result.push(diagnostic("banned-glm", file, token, "#directive", token.value.trim()));
            }
            statement = [];
            continue;
        }

        const currentNamespace = namespaceAt(scopes);
        const symbol = enclosingSymbol(scopes, statement, token.value);

        if (token.kind === "identifier" && token.value.toLowerCase() === "glm") {
            result.push(diagnostic("banned-glm", file, token, symbol, token.value));
        }
        if (isPublicHeader(file) && token.value === "using" && tokens[index + 1]?.value === "namespace") {
            result.push(diagnostic("public-using-namespace", file, token, symbol, "using namespace"));
        }

        if (token.kind === "identifier") {
            const qualified = qualifierBefore(tokens, index);
            let owner = "";
            if (OWNER_NAMES.has(token.value) && qualified[0] === "std") { owner = token.value; }
            if (OWNER_NAMES.has(token.value) && qualified[0] === "std" && qualified[1] === "pmr") { owner = token.value; }
            if (token.value === "path" && qualified.join("::") === "std::filesystem::path") { owner = "filesystem::path"; }
            if (token.value === "error_code" && qualified.join("::") === "std::error_code") { owner = "error_code"; }
            const ownerSymbol = occurrenceSymbol(scopes, statement, tokens, index, symbol);
            if (owner && !isOwnerAllowed(file, ownerSymbol, owner)) {
                result.push(diagnostic("banned-std-owner", file, token, ownerSymbol, qualified.join("::")));
            }
            if (token.value === "string" && tokens[index - 1]?.value === "." && tokens[index + 1]?.value === "(" && !isOwnerAllowed(file, ownerSymbol, "path::string")) {
                result.push(diagnostic("banned-std-owner", file, token, ownerSymbol, "path::string()"));
            }
        }

        if (token.value === "@" && ["interface", "implementation"].includes(tokens[index + 1]?.value)) {
            const objcName = tokens[index + 2]?.value ?? "<objective-c>";
            if (!isAllowedGlobal(file, objcName)) {
                result.push(diagnostic("global-engine-declaration", file, token, objcName, `@${tokens[index + 1].value} ${objcName}`));
            }
            objcScope = objcName;
        } else if (token.value === "@" && tokens[index + 1]?.value === "end") {
            objcScope = "";
        }

        if (token.value === "namespace") {
            let cursor = index + 1;
            const names: string[] = [];
            while (tokens[cursor]?.kind === "identifier") {
                names.push(tokens[cursor].value);
                if (tokens[cursor + 1]?.value !== "::") { break; }
                cursor += 2;
            }
            if (names.includes("Limitless")) {
                result.push(diagnostic("namespace-limitless", file, token, names.join("::"), `namespace ${names.join("::")}`));
            }
            const parent = currentNamespace;
            const full = names.length === 0 ? parent : parent ? `${parent}::${names.join("::")}` : names.join("::");
            const externalSpdlog = file === "Engine/Source/Runtime/Core/Public/Logger/Log.h" && full === "spdlog";
            if (names.length > 0 && !APPROVED_NAMESPACES.has(full) && !externalSpdlog) {
                result.push(diagnostic("namespace-scope", file, token, full, `namespace ${full}`));
            }
        }

        const atGlobal = scopes.length === 0 && !objcScope;
        if (token.value === ";") {
            if (currentNamespace === "spdlog" && statement.length > 0 && !isApprovedSpdlogForward(file, statement)) {
                const externalSymbol = statementSymbol(statement, "<external>");
                const first = statement.find((candidate) => candidate.kind === "identifier") ?? token;
                result.push(diagnostic("namespace-scope", file, first, externalSymbol, "only `class logger;` is approved in namespace spdlog"));
            }
            if (atGlobal && statement.length > 0) {
                const declarationSymbol = statementSymbol(statement, "<global>");
                const first = statement.find((candidate) => candidate.kind === "identifier");
                if (first && first.value !== "namespace" && !isAllowedGlobal(file, declarationSymbol)) {
                    const alias = first.value === "using" || first.value === "typedef";
                    const reportedSymbol = alias ? aliasSymbol(statement, declarationSymbol) : declarationSymbol;
                    result.push(diagnostic(alias ? "global-engine-alias" : "global-engine-declaration", file, first, reportedSymbol, statement.slice(0, 8).map((part) => part.value).join(" ")));
                }
            }
            statement = [];
            continue;
        }

        if (token.value === "{") {
            const namespaceIndex = statement.findIndex((candidate) => candidate.value === "namespace");
            let scope: Scope;
            if (namespaceIndex >= 0) {
                const names: string[] = [];
                for (let cursor = namespaceIndex + 1; cursor < statement.length; ++cursor) {
                    if (statement[cursor].kind === "identifier") { names.push(statement[cursor].value); }
                }
                const parent = currentNamespace;
                const path = names.length === 0 ? parent : parent ? `${parent}::${names.join("::")}` : names.join("::");
                scope = { kind: "namespace", name: names.join("::") || "<anonymous>", namespacePath: path };
            } else {
                const type = className(statement);
                const fn = functionName(statement);
                const containingClass = [...scopes].reverse().find((candidate) => candidate.kind === "class")?.name;
                const scopedFunction = fn && containingClass && !fn.includes("::") ? `${containingClass}::${fn}` : fn;
                scope = type ? { kind: "class", name: type, namespacePath: currentNamespace }
                    : scopedFunction ? { kind: "function", name: scopedFunction, namespacePath: currentNamespace }
                    : { kind: "other", name: "<scope>", namespacePath: currentNamespace };
                if (currentNamespace === "spdlog") {
                    const first = statement.find((candidate) => candidate.kind === "identifier") ?? token;
                    result.push(diagnostic("namespace-scope", file, first, type ?? fn ?? "<external>", "definitions are not approved in namespace spdlog"));
                }
                if (atGlobal) {
                    const declarationSymbol = fn ?? type ?? statementSymbol(statement, "<global>");
                    if (statement.length > 0 && !isAllowedGlobal(file, declarationSymbol)) {
                        const first = statement.find((candidate) => candidate.kind === "identifier") ?? token;
                        result.push(diagnostic("global-engine-declaration", file, first, declarationSymbol, statement.slice(0, 8).map((part) => part.value).join(" ")));
                    }
                }
            }
            scopes.push(scope);
            statement = [];
            continue;
        }
        if (token.value === "}") {
            scopes.pop();
            statement = [];
            continue;
        }
        statement.push(token);
    }
    return result;
}

async function walk(directory: string): Promise<string[]> {
    const files: string[] = [];
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const path = resolve(directory, entry.name);
        if (entry.isDirectory()) { files.push(...await walk(path)); }
        else { files.push(path); }
    }
    return files;
}

export async function scanRepository(repositoryRoot: string): Promise<GovernanceDiagnostic[]> {
    const root = resolve(repositoryRoot);
    const sourceRoot = resolve(root, "Engine/Source");
    const builderRoot = resolve(root, "Engine/Builder");
    const candidates = [...await walk(sourceRoot), ...await walk(builderRoot)];
    const diagnostics: GovernanceDiagnostic[] = [];
    for (const absolute of candidates) {
        const file = normalize(relative(root, absolute));
        if (file.startsWith("Engine/Source/ThirdParty/") || file.includes("/node_modules/")) { continue; }
        const extension = extname(file).toLowerCase();
        const isBuildConfiguration = (file.startsWith("Engine/Source/") && file.endsWith("/Build.ts"))
            || file.startsWith("Engine/Builder/Targets/");
        if (!CPP_EXTENSIONS.has(extension) && !(CONFIG_EXTENSIONS.has(extension) && isBuildConfiguration)) { continue; }
        const source = await readFile(absolute, "utf8");
        if (CPP_EXTENSIONS.has(extension)) {
            diagnostics.push(...scanCppSource(file, source));
        } else {
            const tokens = tokenizeCpp(source);
            for (const token of tokens) {
                if (token.kind === "identifier" && token.value.toLowerCase().includes("glm")) {
                    diagnostics.push(diagnostic("banned-glm", file, token, "configuration", token.value));
                }
                if (token.kind === "string" && /glm/i.test(token.value)) {
                    diagnostics.push(diagnostic("banned-glm", file, token, "configuration", token.value));
                }
            }
        }
    }
    return diagnostics.sort((left, right) => left.file.localeCompare(right.file) || left.line - right.line || left.rule.localeCompare(right.rule));
}

export function formatDiagnostic(value: GovernanceDiagnostic): string {
    return `${value.file}:${value.line}: [${value.rule}] ${value.symbol}: ${value.fragment}`;
}
