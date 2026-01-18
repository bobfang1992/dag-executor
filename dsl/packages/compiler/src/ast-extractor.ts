/**
 * AST Extractor: finds vm() calls with natural expressions, extracts and rewrites them.
 *
 * Detection:
 * - Builder-style (skip): E.mul(...), E.key(...), { op: "...", ... }
 * - Natural (extract): Key.x * P.y, coalesce(a, b), 123, null, -x
 *
 * Rewrites natural expressions with { __expr_id: N } placeholders.
 */

import ts from "typescript";
import { compileExpr, getNodeLocation } from "./expr-compiler.js";
import type { ExprNode } from "@ranking-dsl/runtime";
import { Key, P } from "@ranking-dsl/generated";

export interface ExtractionError {
  message: string;
  line: number;
  column: number;
}

export interface ExtractionResult {
  rewrittenSource: string;
  extractedExprs: Map<number, ExprNode>;
  errors: ExtractionError[];
}

// Build lookup maps from generated registry
const keyLookup = new Map<string, number>();
for (const [name, token] of Object.entries(Key)) {
  keyLookup.set(name, token.id);
}

const paramLookup = new Map<string, number>();
for (const [name, token] of Object.entries(P)) {
  paramLookup.set(name, token.id);
}

/**
 * Check if an expression is builder-style (should be skipped).
 * Builder-style expressions:
 * - E.something(...) - calls on E object
 * - { op: "...", ... } - object literals with op property
 */
function isBuilderStyle(node: ts.Expression, sourceFile: ts.SourceFile): boolean {
  // Check for E.something(...) pattern
  if (ts.isCallExpression(node)) {
    if (ts.isPropertyAccessExpression(node.expression)) {
      const objName = node.expression.expression.getText(sourceFile);
      if (objName === "E") {
        return true;
      }
    }
  }

  // Check for object literal with op property
  if (ts.isObjectLiteralExpression(node)) {
    for (const prop of node.properties) {
      if (ts.isPropertyAssignment(prop) && ts.isIdentifier(prop.name)) {
        if (prop.name.text === "op") {
          return true;
        }
      }
    }
  }

  return false;
}

/**
 * Check if an expression is a natural expression that should be extracted.
 * Natural expressions are valid ExprIR candidates that aren't builder-style.
 */
function isNaturalExpr(node: ts.Expression, sourceFile: ts.SourceFile): boolean {
  if (isBuilderStyle(node, sourceFile)) {
    return false;
  }

  // Check if it's a valid expression type for extraction
  // We support: numeric literals, null, unary minus, binary ops, Key.*, P.*, coalesce()
  if (ts.isNumericLiteral(node)) return true;
  if (node.kind === ts.SyntaxKind.NullKeyword) return true;
  if (ts.isPrefixUnaryExpression(node)) return true;
  if (ts.isBinaryExpression(node)) return true;
  if (ts.isParenthesizedExpression(node)) return true;

  // Property access: Key.foo, P.bar
  if (ts.isPropertyAccessExpression(node)) {
    const objName = node.expression.getText(sourceFile);
    return objName === "Key" || objName === "P";
  }

  // Call expression: coalesce(a, b)
  if (ts.isCallExpression(node)) {
    const fnName = node.expression.getText(sourceFile);
    return fnName === "coalesce";
  }

  return false;
}

interface Replacement {
  start: number;
  end: number;
  exprId: number;
}

/**
 * Extract natural expressions from vm() calls and rewrite the source.
 */
export function extractExpressions(
  sourceCode: string,
  fileName: string
): ExtractionResult {
  const sourceFile = ts.createSourceFile(
    fileName,
    sourceCode,
    ts.ScriptTarget.Latest,
    true,
    ts.ScriptKind.TS
  );

  const extractedExprs = new Map<number, ExprNode>();
  const errors: ExtractionError[] = [];
  const replacements: Replacement[] = [];
  let exprCounter = 0;

  function visit(node: ts.Node): void {
    // Look for .vm() calls
    if (ts.isCallExpression(node)) {
      if (ts.isPropertyAccessExpression(node.expression)) {
        const methodName = node.expression.name.text;

        if (methodName === "vm") {
          // Handle vm() call
          processVmCall(node);
        }
      }
    }

    ts.forEachChild(node, visit);
  }

  function processVmCall(call: ts.CallExpression): void {
    const args = call.arguments;

    // 2-arg form: vm(outKey, expr, opts?)
    if (args.length >= 2) {
      const firstArg = args[0];
      // Check if first arg is Key.* (outKey) - indicates 2-arg form
      if (ts.isPropertyAccessExpression(firstArg)) {
        const objName = firstArg.expression.getText(sourceFile);
        if (objName === "Key") {
          // This is 2-arg form
          const exprArg = args[1];
          if (isNaturalExpr(exprArg, sourceFile)) {
            const result = compileExpr(exprArg, keyLookup, paramLookup, sourceFile);
            if ("error" in result) {
              const loc = getNodeLocation(result.node, sourceFile);
              errors.push({
                message: result.error,
                line: loc.line,
                column: loc.column,
              });
            } else {
              const exprId = exprCounter++;
              extractedExprs.set(exprId, result.expr);
              replacements.push({
                start: exprArg.getStart(sourceFile),
                end: exprArg.getEnd(),
                exprId,
              });
            }
          }
          return;
        }
      }
    }

    // Object form: vm({ outKey: ..., expr: ... })
    if (args.length >= 1 && ts.isObjectLiteralExpression(args[0])) {
      const objArg = args[0];
      for (const prop of objArg.properties) {
        if (ts.isPropertyAssignment(prop) && ts.isIdentifier(prop.name)) {
          if (prop.name.text === "expr") {
            const exprValue = prop.initializer;
            if (isNaturalExpr(exprValue, sourceFile)) {
              const result = compileExpr(exprValue, keyLookup, paramLookup, sourceFile);
              if ("error" in result) {
                const loc = getNodeLocation(result.node, sourceFile);
                errors.push({
                  message: result.error,
                  line: loc.line,
                  column: loc.column,
                });
              } else {
                const exprId = exprCounter++;
                extractedExprs.set(exprId, result.expr);
                replacements.push({
                  start: exprValue.getStart(sourceFile),
                  end: exprValue.getEnd(),
                  exprId,
                });
              }
            }
          }
        }
      }
    }
  }

  visit(sourceFile);

  // Return early if errors
  if (errors.length > 0) {
    return {
      rewrittenSource: sourceCode,
      extractedExprs,
      errors,
    };
  }

  // Apply replacements in reverse order (to preserve offsets)
  replacements.sort((a, b) => b.start - a.start);

  let rewrittenSource = sourceCode;
  for (const r of replacements) {
    const placeholder = `{ __expr_id: ${r.exprId} }`;
    rewrittenSource =
      rewrittenSource.slice(0, r.start) +
      placeholder +
      rewrittenSource.slice(r.end);
  }

  return {
    rewrittenSource,
    extractedExprs,
    errors,
  };
}
