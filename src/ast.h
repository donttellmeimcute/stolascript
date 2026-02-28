#ifndef AST_H
#define AST_H

#include "token.h"

typedef enum {
  // Statements
  AST_PROGRAM,
  AST_BLOCK,
  AST_EXPRESSION_STMT,
  AST_ASSIGNMENT,
  AST_IF_STMT,
  AST_WHILE_STMT,
  AST_LOOP_STMT,
  AST_FOR_STMT,
  AST_MATCH_STMT,
  AST_RETURN_STMT,
  AST_FUNCTION_DECL,
  AST_STRUCT_DECL,
  AST_BREAK_STMT,
  AST_CONTINUE_STMT,
  AST_IMPORT_STMT,

  // Expressions
  AST_IDENTIFIER,
  AST_NUMBER_LITERAL,
  AST_STRING_LITERAL,
  AST_BOOLEAN_LITERAL,
  AST_NULL_LITERAL,
  AST_BINARY_OP,
  AST_UNARY_OP,
  AST_CALL_EXPR,
  AST_ARRAY_LITERAL,
  AST_DICT_LITERAL,
  AST_MEMBER_ACCESS, // p.name or arr at 0 or dict["key"]
  AST_STRUCT_INITIALIZATION
} ASTNodeType;

typedef struct ASTNode ASTNode;

// --- Expressions ---

typedef struct {
  char *value;
} IdentifierNode;

typedef struct {
  char *value; // Keep as string for now, parse to double later if needed
} NumberLiteralNode;

typedef struct {
  char *value;
} StringLiteralNode;

typedef struct {
  int value; // 1 for true, 0 for false
} BooleanLiteralNode;

typedef struct {
  Token op;
  ASTNode *left;
  ASTNode *right;
} BinaryOpNode;

typedef struct {
  Token op;
  ASTNode *right;
} UnaryOpNode;

typedef struct {
  ASTNode *function; // normally Identifier
  ASTNode **args;
  int arg_count;
} CallExprNode;

typedef struct {
  ASTNode **elements;
  int element_count;
} ArrayLiteralNode;

typedef struct {
  ASTNode **keys;
  ASTNode **values;
  int pair_count;
} DictLiteralNode;

typedef struct {
  ASTNode *object;
  ASTNode *property; // Can be identifier (dot), expression (at, [])
  int is_computed;   // 0 for dot access, 1 for 'at' or '[]'
} MemberAccessNode;

typedef struct {
  char *struct_name;
  ASTNode **args;
  int arg_count;
} StructInitNode;

// --- Statements ---

typedef struct {
  ASTNode **statements;
  int statement_count;
} BlockNode;

typedef struct {
  ASTNode *expression;
} ExpressionStmtNode;

typedef struct {
  ASTNode *target; // Identifier or MemberAccess
  ASTNode *value;
} AssignmentNode;

typedef struct {
  ASTNode *condition;
  ASTNode *consequence; // BlockNode
  ASTNode **elif_conditions;
  ASTNode **elif_consequences;
  int elif_count;
  ASTNode *alternative; // BlockNode, can be NULL
} IfStmtNode;

typedef struct {
  ASTNode *condition;
  ASTNode *body; // BlockNode
} WhileStmtNode;

typedef struct {
  char *iterator_name;
  ASTNode *start_expr;
  ASTNode *end_expr;
  ASTNode *step_expr; // Can be NULL (defaults to 1)
  ASTNode *body;      // BlockNode
} LoopStmtNode;

typedef struct {
  char *iterator_name;
  ASTNode *iterable; // e.g. CallExpr range(...) or Identifier (arr)
  ASTNode *body;     // BlockNode
} ForStmtNode;

typedef struct {
  ASTNode *condition;     // match value
  ASTNode **cases;        // Array of literal expressions
  ASTNode **consequences; // Array of BlockNodes
  int case_count;
  ASTNode *default_consequence; // BlockNode
} MatchStmtNode;

typedef struct {
  ASTNode *return_value; // Can be NULL
} ReturnStmtNode;

typedef struct {
  char *module_name;
} ImportStmtNode;

typedef struct {
  char *name;
  char **parameters;
  int param_count;
  ASTNode *body; // BlockNode
} FunctionDeclNode;

typedef struct {
  char *name;
  char **fields;
  int field_count;
} StructDeclNode;

typedef struct {
  ASTNode **statements;
  int statement_count;
} ProgramNode;

// The generic AST Node
struct ASTNode {
  ASTNodeType type;
  union {
    // Statements
    ProgramNode program;
    BlockNode block;
    ExpressionStmtNode expression_stmt;
    AssignmentNode assignment;
    IfStmtNode if_stmt;
    WhileStmtNode while_stmt;
    LoopStmtNode loop_stmt;
    ForStmtNode for_stmt;
    MatchStmtNode match_stmt;
    ReturnStmtNode return_stmt;
    ImportStmtNode import_stmt;
    FunctionDeclNode function_decl;
    StructDeclNode struct_decl;

    // Expressions
    IdentifierNode identifier;
    NumberLiteralNode number_literal;
    StringLiteralNode string_literal;
    BooleanLiteralNode boolean_literal;
    BinaryOpNode binary_op;
    UnaryOpNode unary_op;
    CallExprNode call_expr;
    ArrayLiteralNode array_literal;
    DictLiteralNode dict_literal;
    MemberAccessNode member_access;
    StructInitNode struct_init;
  } as;
};

// Allocation functions
ASTNode *ast_create_program();
ASTNode *ast_create_block();
ASTNode *ast_create_identifier(const char *value);
ASTNode *ast_create_number_literal(const char *value);
ASTNode *ast_create_string_literal(const char *value);
ASTNode *ast_create_boolean_literal(int value);
ASTNode *ast_create_null_literal();
ASTNode *ast_create_binary_op(Token op, ASTNode *left, ASTNode *right);
ASTNode *ast_create_unary_op(Token op, ASTNode *right);
ASTNode *ast_create_call_expr(ASTNode *function);
ASTNode *ast_create_expression_stmt(ASTNode *expression);
ASTNode *ast_create_assignment(ASTNode *target, ASTNode *value);
ASTNode *ast_create_if_stmt(ASTNode *condition, ASTNode *consequence,
                            ASTNode *alternative);
ASTNode *ast_create_while_stmt(ASTNode *condition, ASTNode *body);
ASTNode *ast_create_loop_stmt(const char *iterator_name, ASTNode *start_expr,
                              ASTNode *end_expr, ASTNode *step_expr,
                              ASTNode *body);
ASTNode *ast_create_for_stmt(const char *iterator_name, ASTNode *iterable,
                             ASTNode *body);
ASTNode *ast_create_match_stmt(ASTNode *condition);
ASTNode *ast_create_return_stmt(ASTNode *return_value);
ASTNode *ast_create_array_literal();
ASTNode *ast_create_dict_literal();
ASTNode *ast_create_member_access(ASTNode *object, ASTNode *property,
                                  int is_computed);
ASTNode *ast_create_function_decl(const char *name, ASTNode *body);
ASTNode *ast_create_struct_decl(const char *name);

// Utility list modifiers
void ast_program_add_statement(ASTNode *program, ASTNode *stmt);
void ast_block_add_statement(ASTNode *block, ASTNode *stmt);
void ast_call_add_arg(ASTNode *call, ASTNode *arg);
void ast_function_add_param(ASTNode *func, const char *param);
void ast_struct_add_field(ASTNode *struc, const char *field);
void ast_if_add_elif(ASTNode *if_node, ASTNode *cond, ASTNode *cons);
void ast_match_add_case(ASTNode *match_node, ASTNode *case_expr,
                        ASTNode *consequence);
void ast_array_add_element(ASTNode *array_node, ASTNode *element);
void ast_dict_add_pair(ASTNode *dict_node, ASTNode *key, ASTNode *value);

// Cleanup
void ast_free(ASTNode *node);

#endif // AST_H
