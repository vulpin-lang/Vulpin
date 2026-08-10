#ifndef VM_H
#define VM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ================================================================
   AST Node types
   ================================================================ */
typedef enum {
    ND_PROG,
    ND_NUM, ND_STR, ND_IDENT, ND_BINOP,
    ND_PRINTNL, ND_PRINT,
    ND_IF, ND_ELSE, ND_ENDIF,
    ND_WHILE, ND_WEND,
    ND_FOR,
    ND_ASSIGN, ND_ARITH,
    ND_QUIT, ND_ERROR,
    ND_LABEL, ND_JUMP,
    ND_SWITCH, ND_CASE, ND_DEF, ND_ENDSW,
    ND_FUNC, ND_RETURN, ND_ENDFN,
    ND_TRY, ND_CATCH, ND_ENDTRY,
    ND_DELAY, ND_INPUT,
    ND_IMPORT, ND_STRREP,
    ND_EXEC, ND_CEXEC,
    ND_CLASS, ND_DEL, ND_CALL,
    ND_STRMETH,
    NT_COUNT
} NT;

/* ================================================================
   AST Node
   ================================================================ */
typedef struct Node {
    NT     type;
    char  *val;
    struct Node **ch;
    int    nc;
    int    cap;
    int    line;
} Node;

/* ================================================================
   Value — tagged union for all runtime values
   ================================================================ */
typedef enum {
    VAL_NONE,
    VAL_INT,
    VAL_FLOAT,
    VAL_STR,
    VAL_BOOL,
} ValueType;

typedef struct {
    ValueType type;

    union {
        int64_t  int_val;
        double   float_val;
        bool     bool_val;
    };

    char *str_val;
} Value;

/* ================================================================
   VM struct — runtime state
   ================================================================ */
typedef struct VM {
    void *vars;       /* string → Value   */
    void *labels;     /* string → int     */
    void *funcs;      /* string → FuncInfo */
    void *modules;    /* string → loaded Vulpin module */

    Node *prog;
    int   ip;
    int   node_count;

    Value return_value;

    /* control-flow stacks */
    int  *if_stack;
    int   if_len, if_cap;

    int  *loop_stack;
    int   loop_len, loop_cap;

    struct LoopMeta {
        char *var;
        Value end;
        Value step;
    } *loop_meta;
    int   loop_meta_len, loop_meta_cap;

    int  *try_stack;
    int   try_len, try_cap;

    struct SwitchFrame {
        Value val;
        bool  matched;
    } *switch_stack;
    int   switch_len, switch_cap;

    int  *matching_end;
    int  *matching_else;
    int  *skip_to;
} VM;

/* ================================================================
   Lifecycle
   ================================================================ */
VM  *vm_new(Node *prog);
void vm_free(VM *vm);
void vm_precompute(VM *vm);
int  vm_run(VM *vm);

/* ================================================================
   Value helpers
   ================================================================ */
Value       val_none(void);
Value       val_int(int64_t i);
Value       val_float(double f);
Value       val_bool(bool b);
Value       val_str(const char *s);
Value       val_clone(Value v);
void        val_free(Value *v);
bool        val_truthy(const Value *v);
char       *val_repr(const Value *v);
const char *val_typename(const Value *v);

/* ================================================================
   Parser / debug
   ================================================================ */
Node       *parse(const char *source);
void        freeTree(Node *n);
void        printTree(Node *n, int depth);

#endif /* VM_H */
