/**
 * QuickJS executor - runs bundled plan code in a sandboxed environment.
 *
 * This module is browser-compatible (uses quickjs-emscripten which is WASM-based).
 */

import { getQuickJS } from "quickjs-emscripten";

export interface ExecuteOptions {
  code: string;
  planFilename: string;
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

      // Disable Function constructor (both global and via prototype)
      // Without this, code can bypass via: (() => {}).constructor('return 1')()
      const FunctionProto = (function(){}).constructor.prototype;
      FunctionProto.constructor = undefined;
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
    const evalResult = vm.evalCode(options.code, options.planFilename);

    if (evalResult.error) {
      const error = vm.dump(evalResult.error);
      evalResult.error.dispose();
      throw new Error(
        `QuickJS execution failed for ${options.planFilename}: ${formatQuickJSError(error)}`
      );
    }
    if ("value" in evalResult) {
      evalResult.value.dispose();
    }

    // Verify __emitPlan was called exactly once
    if (emitCount === 0) {
      throw new Error(
        `Plan ${options.planFilename} did not emit an artifact. ` +
          `Ensure the plan calls definePlan() and exports default.`
      );
    }
    if (emitCount > 1) {
      throw new Error(
        `Plan ${options.planFilename} called __emitPlan ${emitCount} times. ` +
          `Expected exactly 1 call.`
      );
    }

    // Validate artifact is JSON-serializable (no undefined, no functions, no symbols)
    validateArtifactSerializable(capturedArtifact, options.planFilename);

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
function validateArtifactSerializable(artifact: unknown, planFilename: string): void {
  // Track current recursion stack to detect true cycles (not shared references)
  const stack = new Set<unknown>();

  function visit(value: unknown, path: string): void {
    // Check for non-JSON-serializable types
    if (value === undefined) {
      throw new Error(
        `Plan ${planFilename} emitted artifact with undefined at ${path}. ` +
          `Use null instead.`
      );
    }
    if (typeof value === "function") {
      throw new Error(
        `Plan ${planFilename} emitted artifact with function at ${path}`
      );
    }
    if (typeof value === "symbol") {
      throw new Error(
        `Plan ${planFilename} emitted artifact with symbol at ${path}`
      );
    }

    // Recurse into objects and arrays (with cycle detection)
    if (value !== null && typeof value === "object") {
      // Detect cycles: if value is already on the current recursion stack
      if (stack.has(value)) {
        throw new Error(
          `Plan ${planFilename} emitted artifact with circular reference at ${path}`
        );
      }
      stack.add(value);

      try {
        if (Array.isArray(value)) {
          value.forEach((item, i) => visit(item, `${path}[${i}]`));
        } else {
          for (const [key, val] of Object.entries(value)) {
            // Skip undefined values - they're omitted in JSON serialization
            if (val !== undefined) {
              visit(val, `${path}.${key}`);
            }
          }
        }
      } finally {
        // Remove from stack when exiting recursion (allows shared refs)
        stack.delete(value);
      }
    }
  }

  visit(artifact, "artifact");
}
