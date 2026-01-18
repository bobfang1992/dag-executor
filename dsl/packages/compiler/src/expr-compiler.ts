/**
 * Expression compiler: converts TypeScript AST expressions to ExprIR.
 *
 * Supported expressions (fail-closed - anything else is rejected):
 * - 123, 0.5         → { op: "const_number", value }
 * - null             → { op: "const_null" }
 * - -x               → { op: "neg", x }
 * - a + b            → { op: "add", a, b }
 * - a - b            → { op: "sub", a, b }
 * - a * b            → { op: "mul", a, b }
 * - Key.foo          → { op: "key_ref", key_id }
 * - P.bar            → { op: "param_ref", param_id }
 * - coalesce(a, b)   → { op: "coalesce", a, b }
 */

import ts from "typescript";
import type { ExprNode } from "@ranking-dsl/runtime";

export interface CompileExprSuccess {
  expr: ExprNode;
}

export interface CompileExprError {
  error: string;
  node: ts.Node;
}

export type CompileExprResult = CompileExprSuccess | CompileExprError;

/**
 * Compile a TypeScript AST expression to ExprIR.
 *
 * @param node The TypeScript expression node
 * @param keyLookup Map from key name to key_id
 * @param paramLookup Map from param name to param_id
 * @param sourceFile The source file (for error messages)
 */
export function compileExpr(
  node: ts.Expression,
  keyLookup: Map<string, number>,
  paramLookup: Map<string, number>,
  sourceFile: ts.SourceFile
): CompileExprResult {
  // Numeric literal: 123, 0.5, 1_000 (with numeric separators)
  if (ts.isNumericLiteral(node)) {
    // Strip numeric separators (underscores) before parsing
    // TypeScript allows 1_000, 0.1_5, etc. but parseFloat stops at underscore
    const sanitized = node.text.replace(/_/g, "");
    const value = parseFloat(sanitized);
    if (!Number.isFinite(value)) {
      return {
        error: `Invalid numeric literal: ${node.text}`,
        node,
      };
    }
    return { expr: { op: "const_number", value } };
  }

  // null literal
  if (node.kind === ts.SyntaxKind.NullKeyword) {
    return { expr: { op: "const_null" } };
  }

  // Prefix unary: -x
  if (ts.isPrefixUnaryExpression(node)) {
    if (node.operator === ts.SyntaxKind.MinusToken) {
      const operand = compileExpr(node.operand, keyLookup, paramLookup, sourceFile);
      if ("error" in operand) return operand;
      return { expr: { op: "neg", x: operand.expr } };
    }
    // Plus token just returns the value
    if (node.operator === ts.SyntaxKind.PlusToken) {
      return compileExpr(node.operand, keyLookup, paramLookup, sourceFile);
    }
    return {
      error: `Unsupported unary operator: ${ts.SyntaxKind[node.operator]}`,
      node,
    };
  }

  // Binary expressions: a + b, a - b, a * b
  if (ts.isBinaryExpression(node)) {
    const left = compileExpr(node.left, keyLookup, paramLookup, sourceFile);
    if ("error" in left) return left;

    const right = compileExpr(node.right, keyLookup, paramLookup, sourceFile);
    if ("error" in right) return right;

    switch (node.operatorToken.kind) {
      case ts.SyntaxKind.PlusToken:
        return { expr: { op: "add", a: left.expr, b: right.expr } };
      case ts.SyntaxKind.MinusToken:
        return { expr: { op: "sub", a: left.expr, b: right.expr } };
      case ts.SyntaxKind.AsteriskToken:
        return { expr: { op: "mul", a: left.expr, b: right.expr } };
      case ts.SyntaxKind.SlashToken:
        return {
          error: "Division (/) is not supported in expressions. Use multiplication with reciprocal instead.",
          node,
        };
      default:
        return {
          error: `Unsupported binary operator: ${ts.SyntaxKind[node.operatorToken.kind]}`,
          node,
        };
    }
  }

  // Property access: Key.foo, P.bar
  if (ts.isPropertyAccessExpression(node)) {
    const objName = node.expression.getText(sourceFile);
    const propName = node.name.text;

    if (objName === "Key") {
      const keyId = keyLookup.get(propName);
      if (keyId === undefined) {
        return {
          error: `Unknown key: Key.${propName}`,
          node,
        };
      }
      return { expr: { op: "key_ref", key_id: keyId } };
    }

    if (objName === "P") {
      const paramId = paramLookup.get(propName);
      if (paramId === undefined) {
        return {
          error: `Unknown param: P.${propName}`,
          node,
        };
      }
      return { expr: { op: "param_ref", param_id: paramId } };
    }

    return {
      error: `Unsupported property access: ${objName}.${propName}. Only Key.* and P.* are allowed.`,
      node,
    };
  }

  // Call expression: coalesce(a, b)
  if (ts.isCallExpression(node)) {
    const fnName = node.expression.getText(sourceFile);

    if (fnName === "coalesce") {
      if (node.arguments.length !== 2) {
        return {
          error: `coalesce() requires exactly 2 arguments, got ${node.arguments.length}`,
          node,
        };
      }
      const a = compileExpr(node.arguments[0], keyLookup, paramLookup, sourceFile);
      if ("error" in a) return a;

      const b = compileExpr(node.arguments[1], keyLookup, paramLookup, sourceFile);
      if ("error" in b) return b;

      return { expr: { op: "coalesce", a: a.expr, b: b.expr } };
    }

    return {
      error: `Unsupported function call: ${fnName}(). Only coalesce() is allowed.`,
      node,
    };
  }

  // Parenthesized expression: (a + b)
  if (ts.isParenthesizedExpression(node)) {
    return compileExpr(node.expression, keyLookup, paramLookup, sourceFile);
  }

  // Catch-all for unsupported expressions
  return {
    error: `Unsupported expression type: ${ts.SyntaxKind[node.kind]}`,
    node,
  };
}

/**
 * Get line and column for a node in the source file.
 */
export function getNodeLocation(
  node: ts.Node,
  sourceFile: ts.SourceFile
): { line: number; column: number } {
  const pos = sourceFile.getLineAndCharacterOfPosition(node.getStart(sourceFile));
  return { line: pos.line + 1, column: pos.character + 1 };
}
