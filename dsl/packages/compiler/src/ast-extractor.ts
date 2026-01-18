/**
 * AST Extractor: finds task calls with natural expressions and extracts/rewrites them.
 *
 * All task calls use named arguments: .task({ name: value, ... })
 * Extracts any object argument with { expr: naturalExpr } property.
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
  // Unwrap parentheses and check inner expression
  if (ts.isParenthesizedExpression(node)) {
    return isNaturalExpr(node.expression, sourceFile);
  }

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
 * Extract natural expressions from task calls and rewrite the source.
 *
 * All task calls use named arguments: .task({ name: value, ... })
 * Extracts any object argument with { expr: naturalExpr } property.
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

  /**
   * Try to extract and record a natural expression.
   * Returns true if extraction was successful or skipped (builder-style).
   * Returns false and records error if compilation failed.
   */
  function tryExtract(exprNode: ts.Expression): boolean {
    if (!isNaturalExpr(exprNode, sourceFile)) {
      return true; // Skip builder-style, not an error
    }

    const result = compileExpr(exprNode, keyLookup, paramLookup, sourceFile);
    if ("error" in result) {
      const loc = getNodeLocation(result.node, sourceFile);
      errors.push({
        message: result.error,
        line: loc.line,
        column: loc.column,
      });
      return false;
    }

    const exprId = exprCounter++;
    extractedExprs.set(exprId, result.expr);
    replacements.push({
      start: exprNode.getStart(sourceFile),
      end: exprNode.getEnd(),
      exprId,
    });
    return true;
  }

  function visit(node: ts.Node): void {
    // Look for method calls: .someMethod(...)
    if (ts.isCallExpression(node)) {
      if (ts.isPropertyAccessExpression(node.expression)) {
        const methodName = node.expression.name.text;
        processMethodCall(methodName, node);
      }
    }

    ts.forEachChild(node, visit);
  }

  function processMethodCall(_methodName: string, call: ts.CallExpression): void {
    // All task calls use named arguments: .task({ name: value, ... })
    // Extract any object argument with { expr: naturalExpr } property
    for (const arg of call.arguments) {
      if (ts.isObjectLiteralExpression(arg)) {
        for (const prop of arg.properties) {
          if (ts.isPropertyAssignment(prop) && ts.isIdentifier(prop.name)) {
            if (prop.name.text === "expr") {
              tryExtract(prop.initializer);
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
