import { spawnSync } from "node:child_process";
import { FindVulkanShaderCompiler, PublishVulkanShader } from "./VulkanShaders.ts";

const [Source, Stage, EntryPoint, Output, ConfiguredCompiler, ...Extra] = process.argv.slice(2);
if (!Source || !["vert", "frag"].includes(Stage) || !EntryPoint || !Output || Extra.length !== 0) {
    throw new Error("Usage: CompileVulkanShader SOURCE vert|frag ENTRY OUTPUT [COMPILER]");
}

try {
    const Compiler = ConfiguredCompiler ?? FindVulkanShaderCompiler();
    if (!Compiler) {
        throw new Error("glslangValidator was not found. Install the Vulkan SDK and set VULKAN_SDK or add its bin directory to PATH.");
    }
    await PublishVulkanShader(Output, (Candidate) => {
        const Result = spawnSync(Compiler, ["-V", "-D", "-S", Stage, "-e", EntryPoint, Source, "-o", Candidate], {
            stdio: "inherit", shell: false,
        });
        if (Result.error) {
            throw Result.error;
        }
        if (Result.status !== 0) {
            throw new Error(`glslangValidator exited with ${Result.status ?? Result.signal}`);
        }
    });
} catch (Reason) {
    console.error(`Shader compilation failed for "${Source}": ${Reason instanceof Error ? Reason.message : String(Reason)}`);
    process.exitCode = 1;
}
