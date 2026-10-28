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
  AST_CLASS_DECL,
  AST_IMPORT_NATIVE,
  AST_C_FUNCTION_DECL,
  AST_TRY_CATCH,
  AST_THROW,
  AST_ASM_BLOCK,
  AST_GLOBAL_DECL,
  AST_CONST_DECL,
  AST_STATIC_DECL,
  AST_PASS,

  // Expressions
  AST_IDENTIFIER,
  AST_NUMBER_LITERAL,
  AST_FLOAT_LITERAL,
  AST_STRING_LITERAL,
  AST_BOOLEAN_LITERAL,
  AST_NULL_LITERAL,
  AST_BINARY_OP,
  AST_UNARY_OP,
  AST_CALL_EXPR,
  AST_ARRAY_LITERAL,
  AST_DICT_LITERAL,
  AST_MEMBER_ACCESS, // p.name or arr at 0 or dict["key"]
  AST_SLICE,         // obj[start:end]
  AST_NEW_EXPR,
  AST_THIS,
  AST_SUPER,
  AST_LAMBDA,  // anonymous function (body in as.lambda.func)
  AST_TERNARY, // a if cond else b
} ASTNodeType;

typedef struct ASTNode ASTNode;

// --- Expressions ---

typedef struct {
  char *value;
} IdentifierNode;

typedef struct {
  char *value; // decimal text (hex/binary literals are normalised by lexer)
} NumberLiteralNode;

typedef struct {
  char *value;
  int length; // may contain embedded NULs
} StringLiteralNode;

typedef struct {
  int value;
} BooleanLiteralNode;

typedef struct {
  TokenType op;
  ASTNode *left;
  ASTNode *right;
} BinaryOpNode;

typedef struct {
  TokenType op;
  ASTNode *right;
} UnaryOpNode;

typedef struct {
  ASTNode *function;
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
  ASTNode *property; // identifier (dot) or expression (at, [])
  int is_computed;   // 0 for dot access, 1 for 'at' or '[]'
} MemberAccessNode;

typedef struct {
  ASTNode *object;
  ASTNode *start; // may be NULL
  ASTNode *end;   // may be NULL
} SliceNode;

typedef struct {
  ASTNode *class_name;
  ASTNode **args;
  int arg_count;
} NewExprNode;

typedef struct {
  ASTNode *func; // AST_FUNCTION_DECL with generated name
} LambdaNode;

typedef struct {
  ASTNode *cond;
  ASTNode *then_expr;
  ASTNode *else_expr;
} TernaryNode;

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
  char *type_annotation;
  TokenType op; // TOKEN_ASSIGN or binary operator for compound assignment
} AssignmentNode;

typedef struct {
  ASTNode *condition;
  ASTNode *consequence;
  ASTNode **elif_conditions;
  ASTNode **elif_consequences;
  int elif_count;
  ASTNode *alternative; // can be NULL
} IfStmtNode;

typedef struct {
  ASTNode *condition;
  ASTNode *body;
} WhileStmtNode;

typedef struct {
  char *iterator_name;
  ASTNode *start_expr;
  ASTNode *end_expr;
  ASTNode *step_expr; // Can be NULL (defaults to 1)
  ASTNode *body;
} LoopStmtNode;

typedef struct {
  char *iterator_name;
  char *value_name; // `for k, v in dict` (NULL otherwise)
  ASTNode *iterable;
  ASTNode *body;
} ForStmtNode;

typedef struct {
  ASTNode *condition;
  ASTNode **cases;
  ASTNode **consequences;
  int case_count;
  ASTNode *default_consequence;
} MatchStmtNode;

typedef struct {
  ASTNode *return_value; // Can be NULL
} ReturnStmtNode;

typedef struct {
  char *module_name; // stdlib module name or path in quotes
  int is_path;
} ImportStmtNode;

typedef struct {
  char *name;
  char **parameters;
  char **param_types;
  ASTNode **param_defaults; // entries may be NULL
  int param_count;
  ASTNode *body;
  char *return_type;
  int is_interrupt; // 1 if declared with 'interrupt function'
  int is_lambda;
  char *class_name; // owning class for methods (NULL otherwise)
} FunctionDeclNode;

typedef struct {
  char *code; // raw assembly text (verbatim content of asm { ... })
} ASTAsmBlockNode;

typedef struct {
  char *name;
  char **fields;
  int field_count;
} StructDeclNode;

typedef struct {
  char *name;
  char *parent; // NULL if no 'extends'
  ASTNode **methods;
  int method_count;
} ClassDeclNode;

typedef struct {
  char *dll_name;
} ASTImportNative;

typedef struct {
  char *name;
  char **param_types;
  int param_count;
  char *return_type;
} ASTCFunctionDecl;

typedef struct {
  ASTNode *try_block;
  char *catch_var;
  ASTNode *catch_block;
} ASTTryCatch;

typedef struct {
  ASTNode *exception_value;
} ASTThrow;

typedef struct {
  char **names;
  int count;
} GlobalDeclNode;

typedef struct {
  char *name;
  ASTNode *value;
} ConstDeclNode;

typedef struct {
  char *name;
  ASTNode *size;  // byte count (constant expression)
  ASTNode *align; // optional alignment (constant expression)
} StaticDeclNode;

typedef struct {
  ASTNode **statements;
  int statement_count;
} ProgramNode;

struct ASTNode {
  ASTNodeType type;
  int line;
  const char *file; // source file (for diagnostics)
  // Filled by the semantic pass
  int ty;    // inferred static type of expression (see semantic.h)
  void *sym; // resolved symbol for identifiers / calls
  union {
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
    ClassDeclNode class_decl;
    GlobalDeclNode global_decl;
    ConstDeclNode const_decl;
    StaticDeclNode static_decl;

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
    SliceNode slice;
    NewExprNode new_expr;
    LambdaNode lambda;
    TernaryNode ternary;
    ASTImportNative import_native;
    ASTCFunctionDecl c_function_decl;
    ASTTryCatch try_catch_stmt;
    ASTThrow throw_stmt;
    ASTAsmBlockNode asm_block;
  } as;
};

// File name recorded in every node created from now on.
extern const char *ast_current_file;

// Generic constructor: zero-initialised node of the given type.
ASTNode *ast_new(ASTNodeType type, int line);

ASTNode *ast_create_identifier(const char *value, int line);
ASTNode *ast_create_number(const char *value, int line);
ASTNode *ast_create_string(const char *value, int length, int line);
ASTNode *ast_create_binary_op(TokenType op, ASTNode *left, ASTNode *right,
                              int line);
ASTNode *ast_create_call(ASTNode *function, int line);
ASTNode *ast_create_member_access(ASTNode *object, ASTNode *property,
                                  int is_computed, int line);

// Generic dynamic-array append: *arr has *count elements.
void ast_list_push(void ***arr, int *count, void *item);

#define AST_PUSH(arr, count, item) ast_list_push((void ***)&(arr), &(count), (item))

void ast_free(ASTNode *node);
char *ast_strdup(const char *s);

#endif // AST_H
