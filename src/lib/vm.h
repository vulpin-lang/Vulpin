#ifndef VM_H
#define VM_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef enum{ND_PROG,ND_NUM,ND_STR,ND_IDENT,ND_BINOP,ND_PRINTNL,ND_PRINT,ND_IF,ND_ELSE,ND_ENDIF,ND_WHILE,ND_WEND,ND_FOR,ND_ASSIGN,ND_ARITH,ND_QUIT,ND_ERROR,ND_LABEL,ND_JUMP,ND_SWITCH,ND_CASE,ND_DEF,ND_ENDSW,ND_FUNC,ND_RETURN,ND_ENDFN,ND_TRY,ND_CATCH,ND_ENDTRY,ND_DELAY,ND_INPUT,ND_IMPORT,ND_STRREP,ND_EXEC,ND_CEXEC,ND_CLASS,ND_DEL,ND_CALL,ND_STRMETH,ND_LIST,ND_DICT,ND_SET,ND_INDEX,ND_IDXASSIGN,ND_SLICE,ND_LISTCOMP,ND_DICTCOMP,ND_FMT,ND_MAP,ND_FILTER,ND_ENUMERATE,ND_ZIP,ND_READ,ND_WRITE,ND_INSPECT,ND_TERNARY,ND_TRYEXPR,ND_ELIF,NT_COUNT}NT;
typedef struct Node{NT t;char*v;struct Node**c;int n;int cap;int l;int col;}Node;
typedef enum{VAL_NONE,VAL_INT,VAL_FLOAT,VAL_STR,VAL_BOOL,VAL_LIST,VAL_DICT,VAL_SET}VT;
typedef struct{VT t;union{int64_t i;double f;bool b;};char*s;void*o;}V;
typedef struct LM{char*v;V e,s;}LM;
typedef struct SF{V v;bool m;}SF;
typedef struct EM{char*iv;char*vv;V src;int pos;}EM;
typedef struct VM{void*v,*l,*fu,*m;Node*p;int ip,nc;V rv;int*is;int il,ic;int*ls;int ll,lc;LM*lm;int lml,lmc;int*ts;int tl,tc;SF*ss;int sl,sc;int*me,*mel,*st;EM*em;int eml,emc;char*source;int parse_failed;}VM;
VM*nv(Node*);void vf(VM*);void vp(VM*);int vr(VM*);
V vn(void);V vi(int64_t);V vf2(double);V vb(bool);V vs(const char*);V vc(V);void vfree(V*);bool vt(const V*);char*vr2(const V*);const char*vtn(const V*);
Node*parse(const char*);void ft(Node*);void pt(Node*,int);
#endif
