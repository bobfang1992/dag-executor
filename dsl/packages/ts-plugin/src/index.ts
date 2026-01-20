/**
 * TypeScript Language Service Plugin for Ranking DSL
 *
 * Suppresses ts(2362) and ts(2363) arithmetic operation errors in .plan.ts files.
 * These errors occur because natural expression syntax (Key.x * coalesce(P.y, 0.2))
 * uses AST extraction at compile time, not TypeScript operator overloading.
 *
 * The dslc compiler handles these expressions correctly via AST transformation.
 */

import type * as ts from "typescript/lib/tsserverlibrary";

/**
 * Error codes to suppress in plan files:
 * - 2362: "The left-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type"
 * - 2363: "The right-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type"
 */
const ARITHMETIC_ERROR_CODES = new Set([2362, 2363]);

/**
 * Check if error 2322 should be suppressed.
 * Suppress type assignment errors for natural expression syntax:
 * - "Type 'number' is not assignable to type 'ExprInput'" (arithmetic result)
 * - "Type 'KeyToken' is not assignable to type 'ExprInput'" (key-only expr)
 * - "Type 'ParamToken' is not assignable to type 'ExprInput'" (param-only expr)
 */
function shouldSuppress2322(messageText: string | ts.DiagnosticMessageChain): boolean {
  const text = typeof messageText === "string" ? messageText : messageText.messageText;
  if (!text.includes("ExprInput")) return false;
  return text.includes("number") || text.includes("KeyToken") || text.includes("ParamToken");
}

function init(_modules: { typescript: typeof ts }): ts.server.PluginModule {
  console.log("[@ranking-dsl/ts-plugin] Plugin loaded");

  function create(info: ts.server.PluginCreateInfo): ts.LanguageService {
    info.project.projectService.logger.info("[@ranking-dsl/ts-plugin] Creating language service proxy");
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const proxy: ts.LanguageService = Object.create(null) as any;

    // Copy all methods from the original language service
    for (const k of Object.keys(info.languageService) as Array<
      keyof ts.LanguageService
    >) {
      const x = info.languageService[k];
      if (typeof x === "function") {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        (proxy as any)[k] = (...args: unknown[]) =>
          // eslint-disable-next-line @typescript-eslint/no-explicit-any
          (x as any).apply(info.languageService, args);
      }
    }

    // Override getSemanticDiagnostics to filter out arithmetic errors in plan files
    proxy.getSemanticDiagnostics = (fileName: string): ts.Diagnostic[] => {
      const original = info.languageService.getSemanticDiagnostics(fileName);

      // Only filter in .plan.ts files
      if (!fileName.endsWith(".plan.ts")) {
        return original;
      }

      return original.filter((diagnostic: ts.Diagnostic) => {
        // Suppress arithmetic operation errors (natural expression syntax)
        if (ARITHMETIC_ERROR_CODES.has(diagnostic.code)) {
          info.project.projectService.logger.info(
            `[@ranking-dsl/ts-plugin] Suppressing ts(${diagnostic.code}) in ${fileName}`
          );
          return false;
        }

        // Suppress "Type 'number' is not assignable to type 'ExprInput'" errors
        if (diagnostic.code === 2322 && shouldSuppress2322(diagnostic.messageText)) {
          info.project.projectService.logger.info(
            `[@ranking-dsl/ts-plugin] Suppressing ts(2322) ExprInput assignment in ${fileName}`
          );
          return false;
        }

        return true;
      });
    };

    return proxy;
  }

  return { create };
}

module.exports = init;
