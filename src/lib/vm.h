#ifndef VM_H
#define VM_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef enum{ND_PROG,ND_NUM,ND_STR,ND_IDENT,ND_BINOP,ND_PRINTNL,ND_PRINT,ND_IF,ND_ELSE,ND_ENDIF,ND_WHILE,ND_WEND,ND_FOR,ND_ASSIGN,ND_ARITH,ND_QUIT,ND_ERROR,ND_LABEL,ND_JUMP,ND_SWITCH,ND_CASE,ND_DEF,ND_ENDSW,ND_FUNC,ND_RETURN,ND_ENDFN,ND_TRY,ND_CATCH,ND_ENDTRY,ND_DELAY,ND_INPUT,ND_IMPORT,ND_STRREP,ND_EXEC,ND_CEXEC,ND_CLASS,ND_DEL,ND_CALL,ND_STRMETH,NT_COUNT}NT;
typedef struct Node{NT t;char*v;struct Node**c;int n;int cap;int l;}Node;
typedef enum{VAL_NONE,VAL_INT,VAL_FLOAT,VAL_STR,VAL_BOOL}VT;
typedef struct{VT t;union{int64_t i;double f;bool b;};char*s;}V;
typedef struct VM{void*v,*l,*fu,*m;Node*p;int ip,nc;V rv;int*is;int il,ic;int*ls;int ll,lc;struct LM{char*v;V e,s;}*lm;int lml,lmc;int*ts;int tl,tc;struct SF{V v;bool m;}*ss;int sl,sc;int*me,*mel,*st;}VM;
VM*nv(Node*);void vf(VM*);void vp(VM*);int vr(VM*);
V vn(void);V vi(int64_t);V vf2(double);V vb(bool);V vs(const char*);V vc(V);void vfree(V*);bool vt(const V*);char*vr2(const V*);const char*vtn(const V*);
Node*parse(const char*);void ft(Node*);void pt(Node*,int);
#endif
