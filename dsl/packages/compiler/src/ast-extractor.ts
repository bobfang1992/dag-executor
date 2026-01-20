/**
 * AST Extractor: finds task calls with natural expressions/predicates and extracts/rewrites them.
 *
 * All task calls use named arguments: .task({ name: value, ... })
 * Extracts:
 * - { expr: naturalExpr } → ExprIR for vm() calls
 * - { pred: naturalPred } → PredIR for filter() calls
 *
 * Detection:
 * - Builder-style (skip): E.mul(...), E.key(...), Pred.and(...), { op: "...", ... }
 * - Natural expr (extract): Key.x * P.y, coalesce(a, b), 123, null, -x
 * - Natural pred (extract): Key.x > 0 && Key.y != null, regex(Key.z, "pattern")
 *
 * Rewrites natural expressions with { __expr_id: N } placeholders.
 * Rewrites natural predicates with { __pred_id: N } placeholders.
 */

import ts from "typescript";
import { compileExpr, getNodeLocation } from "./expr-compiler.js";
import { compilePred } from "./pred-compiler.js";
import type { ExprNode, PredNode } from "@ranking-dsl/runtime";
import { Key, P } from "@ranking-dsl/generated";

export interface ExtractionError {
  message: string;
  line: number;
  column: number;
}

export interface ExtractionResult {
  rewrittenSource: string;
  extractedExprs: Map<number, ExprNode>;
  extractedPreds: Map<number, PredNode>;
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
function isExprBuilderStyle(node: ts.Expression, sourceFile: ts.SourceFile): boolean {
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
 * Check if a predicate is builder-style (should be skipped).
 * Builder-style predicates:
 * - Pred.something(...) - calls on Pred object
 * - { op: "...", ... } - object literals with op property
 */
function isPredBuilderStyle(node: ts.Expression, sourceFile: ts.SourceFile): boolean {
  // Check for Pred.something(...) pattern
  if (ts.isCallExpression(node)) {
    if (ts.isPropertyAccessExpression(node.expression)) {
      const objName = node.expression.expression.getText(sourceFile);
      if (objName === "Pred") {
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
  if (isExprBuilderStyle(node, sourceFile)) {
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

/**
 * Check if an expression is a natural predicate that should be extracted.
 * Natural predicates are valid PredIR candidates that aren't builder-style.
 */
function isNaturalPred(node: ts.Expression, sourceFile: ts.SourceFile): boolean {
  if (isPredBuilderStyle(node, sourceFile)) {
    return false;
  }

  // Check if it's a valid predicate type for extraction
  // We support: true/false, &&, ||, !, comparisons, regex()
  if (node.kind === ts.SyntaxKind.TrueKeyword) return true;
  if (node.kind === ts.SyntaxKind.FalseKeyword) return true;

  // Unwrap parentheses and check inner expression
  if (ts.isParenthesizedExpression(node)) {
    return isNaturalPred(node.expression, sourceFile);
  }

  // Prefix unary: !a
  if (ts.isPrefixUnaryExpression(node)) {
    if (node.operator === ts.SyntaxKind.ExclamationToken) {
      return true;
    }
  }

  // Binary expressions: &&, ||, comparisons
  if (ts.isBinaryExpression(node)) {
    const opKind = node.operatorToken.kind;
    // Logical operators
    if (opKind === ts.SyntaxKind.AmpersandAmpersandToken) return true;
    if (opKind === ts.SyntaxKind.BarBarToken) return true;
    // Comparison operators
    if (opKind === ts.SyntaxKind.EqualsEqualsToken) return true;
    if (opKind === ts.SyntaxKind.EqualsEqualsEqualsToken) return true;
    if (opKind === ts.SyntaxKind.ExclamationEqualsToken) return true;
    if (opKind === ts.SyntaxKind.ExclamationEqualsEqualsToken) return true;
    if (opKind === ts.SyntaxKind.LessThanToken) return true;
    if (opKind === ts.SyntaxKind.LessThanEqualsToken) return true;
    if (opKind === ts.SyntaxKind.GreaterThanToken) return true;
    if (opKind === ts.SyntaxKind.GreaterThanEqualsToken) return true;
  }

  // Call expression: regex(Key.x, "pattern")
  if (ts.isCallExpression(node)) {
    const fnName = node.expression.getText(sourceFile);
    return fnName === "regex";
  }

  return false;
}

interface ExprReplacement {
  kind: "expr";
  start: number;
  end: number;
  id: number;
}

interface PredReplacement {
  kind: "pred";
  start: number;
  end: number;
  id: number;
}

type Replacement = ExprReplacement | PredReplacement;

/**
 * Extract natural expressions and predicates from task calls and rewrite the source.
 *
 * All task calls use named arguments: .task({ name: value, ... })
 * Extracts:
 * - { expr: naturalExpr } → ExprIR
 * - { pred: naturalPred } → PredIR
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
  const extractedPreds = new Map<number, PredNode>();
  const errors: ExtractionError[] = [];
  const replacements: Replacement[] = [];
  let exprCounter = 0;
  let predCounter = 0;

  /**
   * Try to extract and record a natural expression.
   * Returns true if extraction was successful or skipped (builder-style).
   * Returns false and records error if compilation failed.
   */
  function tryExtractExpr(exprNode: ts.Expression): boolean {
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
      kind: "expr",
      start: exprNode.getStart(sourceFile),
      end: exprNode.getEnd(),
      id: exprId,
    });
    return true;
  }

  /**
   * Try to extract and record a natural predicate.
   * Returns true if extraction was successful or skipped (builder-style).
   * Returns false and records error if compilation failed.
   */
  function tryExtractPred(predNode: ts.Expression): boolean {
    if (!isNaturalPred(predNode, sourceFile)) {
      return true; // Skip builder-style, not an error
    }

    const result = compilePred(predNode, keyLookup, paramLookup, sourceFile);
    if ("error" in result) {
      const loc = getNodeLocation(result.node, sourceFile);
      errors.push({
        message: result.error,
        line: loc.line,
        column: loc.column,
      });
      return false;
    }

    const predId = predCounter++;
    extractedPreds.set(predId, result.pred);
    replacements.push({
      kind: "pred",
      start: predNode.getStart(sourceFile),
      end: predNode.getEnd(),
      id: predId,
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
    // Extract { expr: naturalExpr } and { pred: naturalPred }
    for (const arg of call.arguments) {
      if (ts.isObjectLiteralExpression(arg)) {
        for (const prop of arg.properties) {
          if (ts.isPropertyAssignment(prop) && ts.isIdentifier(prop.name)) {
            if (prop.name.text === "expr") {
              tryExtractExpr(prop.initializer);
            } else if (prop.name.text === "pred") {
              tryExtractPred(prop.initializer);
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
      extractedPreds,
      errors,
    };
  }

  // Apply replacements in reverse order (to preserve offsets)
  replacements.sort((a, b) => b.start - a.start);

  let rewrittenSource = sourceCode;
  for (const r of replacements) {
    const placeholder = r.kind === "expr"
      ? `{ __expr_id: ${r.id} }`
      : `{ __pred_id: ${r.id} }`;
    rewrittenSource =
      rewrittenSource.slice(0, r.start) +
      placeholder +
      rewrittenSource.slice(r.end);
  }

  return {
    rewrittenSource,
    extractedExprs,
    extractedPreds,
    errors,
  };
}
