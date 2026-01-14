/**
 * QuickJS executor - runs bundled plan code in a sandboxed environment.
 */

import { getQuickJS } from "quickjs-emscripten";

export interface ExecuteOptions {
  code: string;
  planPath: string;
}

export interface ExecuteResult {
  artifact: unknown;
}

/**
 * Execute bundled plan code in QuickJS sandbox.
 * The sandbox has no IO, no eval, no Function constructor, no dynamic imports.
 */
export async function executePlan(
  options: ExecuteOptions
): Promise<ExecuteResult> {
  const QuickJS = await getQuickJS();
  const vm = QuickJS.newContext();

  try {
    // Capture emitted plan artifact
    let capturedArtifact: unknown = null;
    let emitCount = 0;

    // Inject __emitPlan function
    const emitPlanFn = vm.newFunction("__emitPlan", (artifactHandle) => {
      emitCount++;
      const artifact = vm.dump(artifactHandle);
      capturedArtifact = artifact;
    });
    vm.setProp(vm.global, "__emitPlan", emitPlanFn);
    emitPlanFn.dispose();

    // Inject minimal console.log for debugging (safe, just logs to host)
    const consoleObj = vm.newObject();
    const logFn = vm.newFunction("log", (...args) => {
      const messages = args.map((arg) => vm.dump(arg));
      console.log("[QuickJS]", ...messages);
    });
    vm.setProp(consoleObj, "log", logFn);
    vm.setProp(vm.global, "console", consoleObj);
    logFn.dispose();
    consoleObj.dispose();

    // Disable eval and Function constructor for security
    const sandboxSetup = `
      // Disable eval
      globalThis.eval = undefined;

      // Disable Function constructor
      globalThis.Function = undefined;

      // Prevent access to common Node globals (should not exist, but be explicit)
      globalThis.process = undefined;
      globalThis.require = undefined;
      globalThis.module = undefined;
      globalThis.exports = undefined;
      globalThis.__dirname = undefined;
      globalThis.__filename = undefined;
    `;

    const setupResult = vm.evalCode(sandboxSetup);
    if (setupResult.error) {
      const error = vm.dump(setupResult.error);
      setupResult.error.dispose();
      throw new Error(`Failed to setup sandbox: ${error}`);
    }
    if ("value" in setupResult) {
      setupResult.value.dispose();
    }

    // Execute the bundled plan code
    const evalResult = vm.evalCode(options.code, options.planPath);

    if (evalResult.error) {
      const error = vm.dump(evalResult.error);
      evalResult.error.dispose();
      throw new Error(
        `QuickJS execution failed for ${options.planPath}: ${formatQuickJSError(error)}`
      );
    }
    if ("value" in evalResult) {
      evalResult.value.dispose();
    }

    // Verify __emitPlan was called exactly once
    if (emitCount === 0) {
      throw new Error(
        `Plan ${options.planPath} did not emit an artifact. ` +
          `Ensure the plan calls definePlan() and exports default.`
      );
    }
    if (emitCount > 1) {
      throw new Error(
        `Plan ${options.planPath} called __emitPlan ${emitCount} times. ` +
          `Expected exactly 1 call.`
      );
    }

    // Validate artifact is JSON-serializable (no undefined, no functions, no symbols)
    validateArtifact(capturedArtifact, options.planPath);

    return { artifact: capturedArtifact };
  } finally {
    vm.dispose();
  }
}

/**
 * Format QuickJS error for user-friendly output.
 */
function formatQuickJSError(error: unknown): string {
  if (typeof error === "string") {
    return error;
  }
  if (
    error !== null &&
    typeof error === "object" &&
    "message" in error &&
    typeof error.message === "string"
  ) {
    let result = error.message;
    if ("stack" in error && typeof error.stack === "string") {
      result += "\n" + error.stack;
    }
    return result;
  }
  return String(error);
}

/**
 * Validate that artifact is JSON-serializable.
 * Throws if artifact contains undefined, functions, or symbols.
 */
function validateArtifact(artifact: unknown, planPath: string): void {
  const seen = new Set<unknown>();

  function visit(value: unknown, path: string): void {
    // Detect cycles
    if (value !== null && typeof value === "object") {
      if (seen.has(value)) {
        throw new Error(
          `Plan ${planPath} emitted artifact with circular reference at ${path}`
        );
      }
      seen.add(value);
    }

    // Check for non-JSON-serializable types
    if (value === undefined) {
      throw new Error(
        `Plan ${planPath} emitted artifact with undefined at ${path}. ` +
          `Use null instead.`
      );
    }
    if (typeof value === "function") {
      throw new Error(
        `Plan ${planPath} emitted artifact with function at ${path}`
      );
    }
    if (typeof value === "symbol") {
      throw new Error(
        `Plan ${planPath} emitted artifact with symbol at ${path}`
      );
    }

    // Recurse into objects and arrays
    if (Array.isArray(value)) {
      value.forEach((item, i) => visit(item, `${path}[${i}]`));
    } else if (value !== null && typeof value === "object") {
      for (const [key, val] of Object.entries(value)) {
        // Skip undefined values - they're omitted in JSON serialization
        if (val !== undefined) {
          visit(val, `${path}.${key}`);
        }
      }
    }
  }

  visit(artifact, "artifact");
}
