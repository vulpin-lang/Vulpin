/* Include parser.c — single translation unit. */
#include "lib/vm.h"
#include "parser.c"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#ifndef strndup
static char *strndup(const char *s, size_t n) {
    size_t l = 0;
    while (l < n && s[l]) l++;
    char *d = malloc(l + 1);
    if (d) { memcpy(d, s, l); d[l] = 0; }
    return d;
}
#endif
#ifndef getcwd
static char *g_win_getcwd(char *b, size_t n) { return _getcwd(b, (int)n); }
#define getcwd g_win_getcwd
#endif
#ifndef getline
#include <sys/types.h>
static ssize_t g_win_getline(char **line, size_t *cap, FILE *fp) {
    if (*line) { free(*line); *line = NULL; }
    char c;
    *cap = 0;
    while (fread(&c, 1, 1, fp) == 1) {
        char *t = realloc(*line, *cap + 2);
        if (!t) return -1;
        *line = t;
        (*line)[(*cap)++] = c;
        if (c == '\n') break;
    }
    if (*cap == 0 && feof(fp)) return -1;
    (*line)[*cap] = '\0';
    return (ssize_t)*cap;
}
#define getline g_win_getline
#endif
#else
#include <unistd.h>
#endif

/* ================================================================
   internal helpers: dynamic arrays
   ================================================================ */
#define DA_INIT 8

static void *da_grow(void *data, int *cap, size_t esz) {
    int nc = *cap ? *cap * 2 : DA_INIT;
    void *nd = realloc(data, nc * esz);
    *cap = nc;
    return nd;
}

/* ================================================================
   hash-map helpers
   ================================================================ */
static uint64_t h_fnv(const char *s) {
    uint64_t h = 14695981039346656037ULL;

    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }

    return h;
}

/* ---- VarMap ---- */
typedef struct { char *key; Value v; int st; } VE;
typedef struct { VE *e; int cap, len, tombstones; } VarMap;

static VarMap *vmk(void) {
    VarMap *m = calloc(1, sizeof(*m));
    m->cap = 16;
    m->e = calloc(m->cap, sizeof(VE));
    return m;
}

static void vmfree(VarMap *m) {
    if (!m) return;

    for (int i = 0; i < m->cap; i++)
        if (m->e[i].st == 2) {
            free(m->e[i].key);
            val_free(&m->e[i].v);
        }

    free(m->e);
    free(m);
}

static void vresize(VarMap *m, int nc) {
    VE *old = m->e;
    int oc = m->cap;

    m->e = calloc(nc, sizeof(VE));
    m->cap = nc;
    m->len = 0;

    for (int i = 0; i < oc; i++)
        if (old[i].st == 2) {
            int idx = h_fnv(old[i].key) % nc;

            while (m->e[idx].st == 2)
                idx = (idx+1) % nc;

            m->e[idx] = old[i];
            m->len++;
        }

    m->tombstones = 0;

    free(old);
}

static Value *vget(VarMap *m, const char *k) {
    int idx = h_fnv(k) % m->cap;

    while (m->e[idx].st) {
        if (m->e[idx].st == 2 && !strcmp(m->e[idx].key, k))
            return &m->e[idx].v;

        idx = (idx+1) % m->cap;
    }

    return NULL;
}

static void vput(VarMap *m, const char *k, Value v) {
    if (m->len + m->tombstones >= m->cap * 3 / 4)
        vresize(m, m->cap*2);

    int idx = h_fnv(k) % m->cap;
    int tomb = -1;

    while (m->e[idx].st) {
        if (m->e[idx].st == 1 && tomb == -1) {
            tomb = idx;
        } else if (m->e[idx].st == 2 && !strcmp(m->e[idx].key, k)) {
            val_free(&m->e[idx].v);
            m->e[idx].v = v;
            return;
        }

        idx = (idx+1) % m->cap;
    }

    if (tomb != -1)
        idx = tomb;

    m->e[idx].key = strdup(k);
    m->e[idx].v = v;
    m->e[idx].st = 2;
    m->len++;
}

static void vextend(VarMap *dst, VarMap *src) {
    for (int i = 0; i < src->cap; i++)
        if (src->e[i].st == 2)
            vput(dst, src->e[i].key, val_clone(src->e[i].v));
}

/* ---- LabelMap ---- */
typedef struct { char *key; int v; int st; } LE;
typedef struct { LE *e; int cap, len, tombstones; } LMap;

static LMap *lmk(void) {
    LMap *m = calloc(1, sizeof(*m));
    m->cap = 16;
    m->e = calloc(m->cap, sizeof(LE));
    return m;
}

static void lmfree(LMap *m) {
    if (!m) return;

    for (int i = 0; i < m->cap; i++)
        if (m->e[i].st == 2)
            free(m->e[i].key);

    free(m->e);
    free(m);
}

static void lresize(LMap *m, int nc) {
    LE *old = m->e;
    int oc = m->cap;

    m->e = calloc(nc, sizeof(LE));
    m->cap = nc;
    m->len = 0;

    for (int i = 0; i < oc; i++)
        if (old[i].st == 2) {
            int idx = h_fnv(old[i].key) % nc;

            while (m->e[idx].st == 2)
                idx = (idx+1) % nc;

            m->e[idx] = old[i];
            m->len++;
        }

    m->tombstones = 0;

    free(old);
}

static int *lget(LMap *m, const char *k) {
    int idx = h_fnv(k) % m->cap;

    while (m->e[idx].st) {
        if (m->e[idx].st == 2 && !strcmp(m->e[idx].key, k))
            return &m->e[idx].v;

        idx = (idx+1) % m->cap;
    }

    return NULL;
}

static void lput(LMap *m, const char *k, int v) {
    if (m->len + m->tombstones >= m->cap * 3 / 4)
        lresize(m, m->cap*2);

    int idx = h_fnv(k) % m->cap;
    int tomb = -1;

    while (m->e[idx].st) {
        if (m->e[idx].st == 1 && tomb == -1) {
            tomb = idx;
        } else if (m->e[idx].st == 2 && !strcmp(m->e[idx].key, k)) {
            m->e[idx].v = v;
            return;
        }

        idx = (idx+1) % m->cap;
    }

    if (tomb != -1)
        idx = tomb;

    m->e[idx].key = strdup(k);
    m->e[idx].v = v;
    m->e[idx].st = 2;
    m->len++;
}

/* ---- FuncMap ---- */
typedef struct {
    int   start_ip;
    int   end_ip;
    char **params;
    int    param_count;
} FuncInfo;

typedef struct { char *key; FuncInfo v; int st; } FE;
typedef struct { FE *e; int cap, len, tombstones; } FMap;

static FMap *fmk(void) {
    FMap *m = calloc(1, sizeof(*m));
    m->cap = 16;
    m->e = calloc(m->cap, sizeof(FE));
    return m;
}

static void fmfree(FMap *m) {
    if (!m) return;

    for (int i = 0; i < m->cap; i++) {
        if (m->e[i].st == 2) {
            free(m->e[i].key);

            for (int j = 0; j < m->e[i].v.param_count; j++)
                free(m->e[i].v.params[j]);

            free(m->e[i].v.params);
        }
    }

    free(m->e);
    free(m);
}

static void fresize(FMap *m, int nc) {
    FE *old = m->e;
    int oc = m->cap;

    m->e = calloc(nc, sizeof(FE));
    m->cap = nc;
    m->len = 0;

    for (int i = 0; i < oc; i++)
        if (old[i].st == 2) {
            int idx = h_fnv(old[i].key) % nc;

            while (m->e[idx].st == 2)
                idx = (idx+1) % nc;

            m->e[idx] = old[i];
            m->len++;
        }

    m->tombstones = 0;

    free(old);
}

static FuncInfo *fget(FMap *m, const char *k) {
    int idx = h_fnv(k) % m->cap;

    while (m->e[idx].st) {
        if (m->e[idx].st == 2 && !strcmp(m->e[idx].key, k))
            return &m->e[idx].v;

        idx = (idx+1) % m->cap;
    }

    return NULL;
}

static void fput(FMap *m, const char *k, FuncInfo v) {
    if (m->len + m->tombstones >= m->cap * 3 / 4)
        fresize(m, m->cap*2);

    int idx = h_fnv(k) % m->cap;
    int tomb = -1;

    while (m->e[idx].st) {
        if (m->e[idx].st == 1 && tomb == -1) {
            tomb = idx;
        } else if (m->e[idx].st == 2 && !strcmp(m->e[idx].key, k)) {
            for (int j = 0; j < m->e[idx].v.param_count; j++)
                free(m->e[idx].v.params[j]);

            free(m->e[idx].v.params);

            m->e[idx].v = v;
            return;
        }

        idx = (idx+1) % m->cap;
    }

    if (tomb != -1)
        idx = tomb;

    m->e[idx].key = strdup(k);
    m->e[idx].v = v;
    m->e[idx].st = 2;
    m->len++;
}

/* ---- ModuleMap ---- */
typedef struct {
    char *name;
    Node *prog;
    VM *vm;
} ModuleEnt;

typedef struct {
    ModuleEnt *e;
    int len, cap;
} ModMap;

static ModMap *mmk(void) {
    ModMap *m = calloc(1, sizeof(*m));
    return m;
}

static void mmfree(ModMap *m) {
    if (!m) return;

    for (int i = 0; i < m->len; i++) {
        free(m->e[i].name);

        if (m->e[i].vm)
            vm_free(m->e[i].vm);

        if (m->e[i].prog)
            freeTree(m->e[i].prog);
    }

    free(m->e);
    free(m);
}

static ModuleEnt *mget(ModMap *m, const char *name) {
    for (int i = 0; i < m->len; i++) {
        if (!strcmp(m->e[i].name, name))
            return &m->e[i];
    }

    return NULL;
}

static void mput(ModMap *m, const char *name, Node *prog, VM *modvm) {
    if (m->len >= m->cap) {
        m->cap = m->cap ? m->cap * 2 : 8;
        m->e = realloc(m->e, m->cap * sizeof(ModuleEnt));
    }

    m->e[m->len].name = strdup(name);
    m->e[m->len].prog = prog;
    m->e[m->len].vm = modvm;
    m->len++;
}

static char *read_whole_file(const char *path) {
    FILE *f = fopen(path, "rb");

    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *b = malloc(sz + 1);

    if (!b) {
        fclose(f);
        return NULL;
    }

    size_t rd = fread(b, 1, sz, f);
    b[rd] = 0;

    fclose(f);

    return b;
}

static char *module_name_from_path(const char *path) {
    const char *base = strrchr(path, '/');

#ifdef _WIN32
    const char *base2 = strrchr(path, '\\');
    if (base2 && (!base || base2 > base))
        base = base2;
#endif

    base = base ? base + 1 : path;

    char *name = strdup(base);

    char *dot = strstr(name, ".vul");
    if (dot) *dot = 0;

    return name;
}

/* ================================================================
   Value constructors
   ================================================================ */
Value val_none(void) {
    Value v = {.type=VAL_NONE, .str_val=NULL, .int_val=0};
    return v;
}

Value val_int(int64_t i) {
    Value v = {.type=VAL_INT, .str_val=NULL, .int_val=i};
    return v;
}

Value val_float(double f) {
    Value v = {.type=VAL_FLOAT, .str_val=NULL, .float_val=f};
    return v;
}

Value val_bool(bool b) {
    Value v = {.type=VAL_BOOL, .str_val=NULL, .bool_val=b};
    return v;
}

Value val_str(const char *s) {
    Value v = {.type=VAL_STR, .str_val=strdup(s?s:""), .int_val=0};
    return v;
}

Value val_clone(Value v) {
    if (v.type == VAL_STR && v.str_val) {
        Value c = v;
        c.str_val = strdup(v.str_val);
        return c;
    }

    return v;
}

void val_free(Value *v) {
    if (v->type == VAL_STR) {
        free(v->str_val);
        v->str_val = NULL;
    }
}

bool val_truthy(const Value *v) {
    switch (v->type) {
    case VAL_BOOL:
        return v->bool_val;
    case VAL_INT:
        return v->int_val != 0;
    case VAL_FLOAT:
        return v->float_val != 0.0;
    case VAL_STR:
        return v->str_val && v->str_val[0];
    default:
        return false;
    }
}

const char *val_typename(const Value *v) {
    switch (v->type) {
    case VAL_INT:   return "Int";
    case VAL_FLOAT: return "Float";
    case VAL_STR:   return "Str";
    case VAL_BOOL:  return "Bool";
    default:        return "None";
    }
}

char *val_repr(const Value *v) {
    char buf[256];

    switch (v->type) {
    case VAL_INT:
        snprintf(buf, sizeof(buf), "%lld", (long long)v->int_val);
        break;

    case VAL_FLOAT: {
        double d = v->float_val;

        if (d == (int64_t)d)
            snprintf(buf, sizeof(buf), "%.1f", d);
        else
            snprintf(buf, sizeof(buf), "%g", d);

        break;
    }

    case VAL_STR:
        return strdup(v->str_val ? v->str_val : "");

    case VAL_BOOL:
        return strdup(v->bool_val ? "true" : "false");

    default:
        return strdup("None");
    }

    return strdup(buf);
}

static char *str_method(const char *s, char m) {
    size_t n = strlen(s), i;
    char *r = malloc(n + 1);

    switch (m) {
    case 'U':
        for (i=0;i<n;i++) r[i]=toupper(s[i]);
        r[n]=0;
        return r;

    case 'L':
        for (i=0;i<n;i++) r[i]=tolower(s[i]);
        r[n]=0;
        return r;

    case 'S': {
        size_t a=0, b=n;

        while (a<b && isspace(s[a])) a++;
        while (b>a && isspace(s[b-1])) b--;

        for (i=0;i<b-a;i++) r[i]=s[a+i];

        r[b-a]=0;

        return r;
    }

    case 'T': {
        int up=1;

        for (i=0;i<n;i++) {
            if (isspace(s[i])) {
                r[i]=s[i];
                up=1;
            } else {
                r[i]=up?toupper(s[i]):tolower(s[i]);
                up=0;
            }
        }

        r[n]=0;

        return r;
    }

    case 'C': {
        for (i=0;i<n;i++) r[i]=tolower(s[i]);

        if (n) r[0]=toupper(r[0]);

        r[n]=0;

        return r;
    }

    }

    return r;
}

/* ================================================================
   VM lifecycle
   ================================================================ */
VM *vm_new(Node *prog) {
    VM *vm = calloc(1, sizeof(VM));

    vm->vars   = vmk();
    vm->labels = lmk();
    vm->funcs  = fmk();
    vm->modules = mmk();

    vm->prog   = prog;
    vm->node_count = prog->nc;
    vm->ip = 0;
    vm->return_value = val_none();

    return vm;
}

void vm_free(VM *vm) {
    if (!vm) return;

    vmfree((VarMap*)vm->vars);
    lmfree((LMap*)vm->labels);
    fmfree((FMap*)vm->funcs);
    mmfree((ModMap*)vm->modules);

    free(vm->if_stack);
    free(vm->loop_stack);

    for (int i = 0; i < vm->loop_meta_len; i++) {
        free(vm->loop_meta[i].var);
        val_free(&vm->loop_meta[i].end);
        val_free(&vm->loop_meta[i].step);
    }

    free(vm->loop_meta);
    free(vm->try_stack);

    for (int i = 0; i < vm->switch_len; i++)
        val_free(&vm->switch_stack[i].val);

    free(vm->switch_stack);
    free(vm->matching_end);
    free(vm->matching_else);
    free(vm->skip_to);

    val_free(&vm->return_value);

    free(vm);
}

/* ================================================================
   evaluate binary operation
   ================================================================ */
static int eval_binop(const Value *l, const char *opstr, const Value *r, Value *out) {
    char op = opstr[0];

    if (l->type == VAL_NONE || r->type == VAL_NONE) {
        if (op == '=') {
            *out = val_bool(l->type == r->type);
            return 0;
        }

        fprintf(stderr, "Error: cannot apply '%s' to None\n", opstr);
        return 1;
    }

    if (l->type == VAL_BOOL && r->type == VAL_BOOL) {
        if (op == '=') {
            *out = val_bool(l->bool_val == r->bool_val);
            return 0;
        }

        fprintf(stderr, "Error: cannot apply '%s' to Bool\n", opstr);
        return 1;
    }

    if (l->type == VAL_INT && r->type == VAL_INT) {
        int64_t a = l->int_val, b = r->int_val;

        if (strcmp(opstr, "==") == 0 || strcmp(opstr, "=") == 0) {
            *out = val_bool(a==b);
            return 0;
        }

        if (strcmp(opstr, "!=") == 0) {
            *out = val_bool(a!=b);
            return 0;
        }

        if (strcmp(opstr, ">=") == 0) {
            *out = val_bool(a>=b);
            return 0;
        }

        if (strcmp(opstr, "<=") == 0) {
            *out = val_bool(a<=b);
            return 0;
        }

        switch(op) {
        case '+': *out = val_int(a+b); return 0;
        case '-': *out = val_int(a-b); return 0;
        case '*': *out = val_int(a*b); return 0;
        case '/':
            if (b == 0) {
                fprintf(stderr, "Error: division by zero\n");
                return 1;
            }
            *out = val_int(a/b);
            return 0;
        case '>': *out = val_bool(a>b); return 0;
        case '<': *out = val_bool(a<b); return 0;
	case '%':
	  if (b == 0) { fprintf(stderr, "Error: modulo by zero\n"); return 1; }
	  *out = val_int(a % b);
	  return 0;
        default:
            fprintf(stderr, "Error: unknown operator '%s'\n", opstr);
            return 1;
        }
    }

    if (l->type == VAL_FLOAT && r->type == VAL_FLOAT) {
        double a = l->float_val, b = r->float_val;

        if (strcmp(opstr, "==") == 0 || strcmp(opstr, "=") == 0) {
            *out = val_bool(a==b);
            return 0;
        }

        if (strcmp(opstr, "!=") == 0) {
            *out = val_bool(a!=b);
            return 0;
        }

        if (strcmp(opstr, ">=") == 0) {
            *out = val_bool(a>=b);
            return 0;
        }

        if (strcmp(opstr, "<=") == 0) {
            *out = val_bool(a<=b);
            return 0;
        }

        switch(op) {
        case '+': *out = val_float(a+b); return 0;
        case '-': *out = val_float(a-b); return 0;
        case '*': *out = val_float(a*b); return 0;
        case '/':
            if (b == 0.0) {
                fprintf(stderr, "Error: division by zero\n");
                return 1;
            }
            *out = val_float(a/b);
            return 0;
        case '>': *out = val_bool(a>b); return 0;
        case '<': *out = val_bool(a<b); return 0;
	case '%':
	  fprintf(stderr, "Error: modulo not supported for floats\n");
	  return 1;
	default:
            fprintf(stderr, "Error: unknown operator '%s'\n", opstr);
            return 1;
        }
    }

    if (l->type == VAL_INT && r->type == VAL_FLOAT) {
        Value lf = val_float((double)l->int_val);
        int rc = eval_binop(&lf, opstr, r, out);
        val_free(&lf);
        return rc;
    }

    if (l->type == VAL_FLOAT && r->type == VAL_INT) {
        Value rf = val_float((double)r->int_val);
        int rc = eval_binop(l, opstr, &rf, out);
        val_free(&rf);
        return rc;
    }

    if (l->type == VAL_STR && r->type == VAL_STR) {
        if (op == '+') {
            char *buf = malloc(strlen(l->str_val)+strlen(r->str_val)+1);

            strcpy(buf, l->str_val);
            strcat(buf, r->str_val);

            *out = (Value){.type=VAL_STR, .str_val=buf};

            return 0;
        }

        if (strcmp(opstr, "==") == 0 || strcmp(opstr, "=") == 0) {
            *out = val_bool(!strcmp(l->str_val, r->str_val));
            return 0;
        }

        if (strcmp(opstr, "!=") == 0) {
            *out = val_bool(strcmp(l->str_val, r->str_val)!=0);
            return 0;
        }

        if (op == '>') {
            *out = val_bool(strcmp(l->str_val, r->str_val)>0);
            return 0;
        }

        if (op == '<') {
            *out = val_bool(strcmp(l->str_val, r->str_val)<0);
            return 0;
        }

	 if (op == '%') {
	   fprintf(stderr, "Error: modulo not supported for strings\n");
	   return 1;
	 }
        fprintf(stderr, "Error: cannot apply '%s' to strings\n", opstr);
        return 1;
    }

    if (op != '+' && (l->type == VAL_STR) != (r->type == VAL_STR)) {
        char *end;

        int lstr = l->type == VAL_STR;

        Value ln = lstr ? (Value){0} : *l;
        Value rn = lstr ? *r : (Value){0};

        Value *sv = lstr ? &ln : &rn;
        const char *s = lstr ? l->str_val : r->str_val;

        double d = strtod(s, &end);

        if (end != s && *end == '\0') {
            if (strchr(s, '.') || strchr(s, 'e') || strchr(s, 'E'))
                *sv = val_float(d);
            else
                *sv = val_int((int64_t)d);

            return eval_binop(&ln, opstr, &rn, out);
        }
    }

    if (op == '+') {
        char *ls = val_repr(l);
        char *rs = val_repr(r);

        char *buf = malloc(strlen(ls)+strlen(rs)+1);

        strcpy(buf, ls);
        strcat(buf, rs);

        free(ls);
        free(rs);

        *out = (Value){.type=VAL_STR, .str_val=buf};

        return 0;
    }

    fprintf(stderr, "Error: type mismatch for '%s' between %s and %s\n",
            opstr, val_typename(l), val_typename(r));

    return 1;
}

/* ================================================================
   value equality
   ================================================================ */
static bool val_eq(const Value *a, const Value *b) {
    Value out;

    if (eval_binop(a, "==", b, &out))
        return false;

    bool r = out.type == VAL_BOOL && out.bool_val;

    val_free(&out);

    return r;
}

/* ================================================================
   for-loop range check
   ================================================================ */
static bool in_range(const Value *cur, const Value *end, const Value *step) {
    if (cur->type == VAL_INT && end->type == VAL_INT && step->type == VAL_INT)
        return step->int_val > 0 ? cur->int_val < end->int_val
                                 : cur->int_val > end->int_val;

    if (cur->type == VAL_FLOAT && end->type == VAL_FLOAT && step->type == VAL_FLOAT)
        return step->float_val > 0.0 ? cur->float_val < end->float_val
                                     : cur->float_val > end->float_val;

    return false;
}

static bool val_is_zero(const Value *v) {
    if (v->type == VAL_INT)
        return v->int_val == 0;

    if (v->type == VAL_FLOAT)
        return v->float_val == 0.0;

    return false;
}

/* ================================================================
   forward declarations
   ================================================================ */
static int eval_expr(VM *vm, Node *n, Value *out);
static int exec_stmt(VM *vm, Node *n);
static int call_func(VM *vm, Node *callnode, Value *out);
static int builtin_call(VM *vm, const char *mod, const char *method,
                        Value *args, int argc, Value *out);
static int call_user_func(VM *target, const char *fname,
                          Value *args, int argc, Value *out);

/* ================================================================
   evaluate expression
   ================================================================ */
static int eval_expr(VM *vm, Node *n, Value *out) {
    if (!n) {
        *out = val_none();
        return 0;
    }

    switch (n->type) {

    case ND_NUM: {
        char *end;
        double d = strtod(n->val, &end);

        if (strchr(n->val, '.') || (*end && *end != 'f'))
            *out = val_float(d);
        else
            *out = val_int((int64_t)d);

        return 0;
    }

    case ND_STR:
        *out = val_str(n->val);
        return 0;

    case ND_IDENT: {
        Value *v = vget((VarMap*)vm->vars, n->val);

        if (v) {
            *out = val_clone(*v);
            return 0;
        }

        const char *dot = strchr(n->val, '.');

        if (dot && dot != n->val) {
            char *mod = strndup(n->val, dot-n->val);
            const char *method = dot+1;

            int rc = builtin_call(vm, mod, method, NULL, 0, out);

            free(mod);

            return rc;
        }

        *out = val_none();

        return 0;
    }

    case ND_BINOP: {
        const char *op = n->val;

        /* unary plus/minus */
        if (n->nc == 1 && (op[0] == '-' || op[0] == '+')) {
            Value r;

            if (eval_expr(vm, n->ch[0], &r))
                return 1;

            if (op[0] == '+') {
                *out = r;
                return 0;
            }

            if (r.type == VAL_INT) {
                *out = val_int(-r.int_val);
                val_free(&r);
                return 0;
            }

            if (r.type == VAL_FLOAT) {
                *out = val_float(-r.float_val);
                val_free(&r);
                return 0;
            }

            fprintf(stderr, "Error: cannot negate %s\n", val_typename(&r));

            val_free(&r);

            return 1;
        }

        if (n->nc < 2) {
            *out = val_none();
            return 0;
        }

        Value l, r;

        if (eval_expr(vm, n->ch[0], &l))
            return 1;

        if (eval_expr(vm, n->ch[1], &r)) {
            val_free(&l);
            return 1;
        }

        int rc = eval_binop(&l, op, &r, out);

        val_free(&l);
        val_free(&r);

        return rc;
    }

    case ND_CALL:
        return call_func(vm, n, out);

    case ND_STRMETH: {
        Value v;

        if (eval_expr(vm, n->ch[0], &v))
            return 1;

        if (v.type != VAL_STR) {
            fprintf(stderr, "Error: string method on %s\n", val_typename(&v));
            val_free(&v);
            return 1;
        }

        *out = val_str(str_method(v.str_val, n->val[0]));

        val_free(&v);

        return 0;
    }

    default:
        fprintf(stderr, "Error: unexpected node type in expression\n");
        *out = val_none();
        return 1;
    }
}

/* ================================================================
   built-in modules
   ================================================================ */
static int builtin_call(VM *vm, const char *mod, const char *method,
                        Value *args, int argc, Value *out) {
    (void)vm;

    if (!strcmp(mod, "math")) {

        if (!strcmp(method, "sqrt")) {
            if (argc != 1) {
                fprintf(stderr, "math.sqrt: expected 1 arg\n");
                return 1;
            }

            double val = (args[0].type==VAL_INT) ? (double)args[0].int_val :
                         (args[0].type==VAL_FLOAT) ? args[0].float_val : 0;

            *out = val_float(sqrt(val));

            return 0;
        }

        if (!strcmp(method, "pi")) {
            *out = val_float(M_PI);
            return 0;
        }

        if (!strcmp(method, "e")) {
            *out = val_float(M_E);
            return 0;
        }

        if (!strcmp(method, "floor")) {
            if (argc != 1) {
                fprintf(stderr, "math.floor: expected 1 arg\n");
                return 1;
            }

            if (args[0].type==VAL_INT) {
                *out = val_clone(args[0]);
                return 0;
            }

            if (args[0].type==VAL_FLOAT) {
                *out = val_int((int64_t)floor(args[0].float_val));
                return 0;
            }

            fprintf(stderr, "math.floor: expected number\n");

            return 1;
        }

        if (!strcmp(method, "ceil")) {
            if (argc != 1) {
                fprintf(stderr, "math.ceil: expected 1 arg\n");
                return 1;
            }

            if (args[0].type==VAL_INT) {
                *out = val_clone(args[0]);
                return 0;
            }

            if (args[0].type==VAL_FLOAT) {
                *out = val_int((int64_t)ceil(args[0].float_val));
                return 0;
            }

            fprintf(stderr, "math.ceil: expected number\n");

            return 1;
        }

        if (!strcmp(method, "abs")) {
            if (argc != 1) {
                fprintf(stderr, "math.abs: expected 1 arg\n");
                return 1;
            }

            if (args[0].type==VAL_INT) {
                *out = val_int(llabs(args[0].int_val));
                return 0;
            }

            if (args[0].type==VAL_FLOAT) {
                *out = val_float(fabs(args[0].float_val));
                return 0;
            }

            fprintf(stderr, "math.abs: expected number\n");

            return 1;
        }

        fprintf(stderr, "math.%s: unknown method\n", method);

        return 1;
    }

    if (!strcmp(mod, "os")) {

        if (!strcmp(method, "name")) {
#ifdef _WIN32
            *out = val_str("nt");
#else
            *out = val_str("posix");
#endif
            return 0;
        }

        if (!strcmp(method, "getcwd")) {
            char cwd[4096];

            *out = val_str(getcwd(cwd, sizeof(cwd)) ? cwd : "");

            return 0;
        }

        if (!strcmp(method, "system")) {
            if (argc != 1 || args[0].type != VAL_STR) {
                fprintf(stderr, "os.system: expected string arg\n");
                return 1;
            }

            int code = system(args[0].str_val);

            *out = val_int(code);

            return 0;
        }

        fprintf(stderr, "os.%s: unknown method\n", method);

        return 1;
    }

    if (!strcmp(mod, "random")) {

        if (!strcmp(method, "randint")) {
            if (argc != 2 || args[0].type!=VAL_INT || args[1].type!=VAL_INT) {
                fprintf(stderr, "random.randint: expected 2 int args\n");
                return 1;
            }

            int64_t a = args[0].int_val;
            int64_t b = args[1].int_val;

            if (a > b) {
                int64_t t = a;
                a = b;
                b = t;
            }

            uint64_t range = (uint64_t)(b - a) + 1;

            *out = val_int(a + (int64_t)(rand() % range));

            return 0;
        }

        fprintf(stderr, "random.%s: unknown method\n", method);

        return 1;
    }

    fprintf(stderr, "Unknown module '%s'\n", mod);

    return 1;
}

/* ================================================================
   call user function inside a target VM
   ================================================================ */
static int call_user_func(VM *target, const char *fname,
                          Value *args, int argc, Value *out) {
    FuncInfo *fi = fget((FMap*)target->funcs, fname);

    if (!fi) {
        fprintf(stderr, "Error: unknown function '%s'\n", fname);
        *out = val_none();
        return 1;
    }

    if (argc != fi->param_count) {
        fprintf(stderr, "Error: %s expects %d arguments, got %d\n",
                fname, fi->param_count, argc);
        *out = val_none();
        return 1;
    }

    VarMap *local_vars = vmk();
    vextend(local_vars, (VarMap*)target->vars);

    for (int i = 0; i < argc; i++) {
        char *pname = fi->params ? fi->params[i] : NULL;

        if (pname)
            vput(local_vars, pname, val_clone(args[i]));
    }

    void *old_vars = target->vars;
    int saved_ip = target->ip;

    target->vars = local_vars;
    target->ip = fi->start_ip + 1;

    val_free(&target->return_value);
    target->return_value = val_none();

    int rc = 0;

    while (target->ip < target->node_count && target->ip <= fi->end_ip) {
        if (target->skip_to[target->ip] != -1) {
            target->ip = target->skip_to[target->ip];
            continue;
        }

        Node *nd = target->prog->ch[target->ip];

        target->ip++;

        rc = exec_stmt(target, nd);

        if (rc == 2) {
            rc = 0;
            break;
        }

        if (rc == 1) {
            if (target->try_len > 0) {
                int t = target->try_stack[target->try_len-1];
                int c = target->matching_else[t];

                if (c != -1) {
                    target->try_len--;

                    Node *catch_node = target->prog->ch[c];

                    if (catch_node->val[0])
                        vput((VarMap*)target->vars, catch_node->val, val_str("error"));

                    target->ip = c + 1;

                    rc = 0;

                    continue;
                }
            }

            break;
        }
    }

    target->vars = old_vars;
    target->ip = saved_ip;

    if (rc == 1) {
        val_free(&target->return_value);
        target->return_value = val_none();

        vmfree(local_vars);

        *out = val_none();

        return 1;
    }

    Value ret = val_clone(target->return_value);

    val_free(&target->return_value);
    target->return_value = val_none();

    vmfree(local_vars);

    *out = ret;

    return 0;
}

/* ================================================================
   execute a function call
   ================================================================ */
static int call_func(VM *vm, Node *callnode, Value *out) {
    const char *fname = callnode->val;
    int argc = callnode->nc;

    Value *args = NULL;

    if (argc > 0) {
        args = malloc(argc * sizeof(Value));

        for (int i = 0; i < argc; i++) {
            if (eval_expr(vm, callnode->ch[i], &args[i])) {
                for (int j = 0; j < i; j++)
                    val_free(&args[j]);

                free(args);

                return 1;
            }
        }
    }

    const char *dot = strchr(fname, '.');

    if (dot) {
        char *mod = strdup(fname);
        char *method = strchr(mod, '.');

        if (method) {
            *method++ = '\0';

            int rc;

            if (!*method) {
                fprintf(stderr, "Error: invalid member call '%s'\n", fname);
                *out = val_none();
                rc = 1;
            } else if (!strcmp(mod, "math") ||
                       !strcmp(mod, "os") ||
                       !strcmp(mod, "random")) {
                rc = builtin_call(vm, mod, method, args, argc, out);
            } else {
                ModuleEnt *me = mget((ModMap*)vm->modules, mod);

                if (me) {
                    rc = call_user_func(me->vm, method, args, argc, out);
                } else {
                    fprintf(stderr, "Error: unknown module '%s'\n", mod);
                    *out = val_none();
                    rc = 1;
                }
            }

            free(mod);

            for (int i = 0; i < argc; i++)
                val_free(&args[i]);

            free(args);

            return rc;
        }

        free(mod);
    }

    int rc = call_user_func(vm, fname, args, argc, out);

    for (int i = 0; i < argc; i++)
        val_free(&args[i]);

    free(args);

    return rc;
}

/* ================================================================
   execute statement
   ================================================================ */
static int exec_stmt(VM *vm, Node *n) {
    if (!n) return 0;

    switch (n->type) {

    case ND_PRINTNL: {
        if (n->nc >= 1) {
            Value v;

            if (eval_expr(vm, n->ch[0], &v))
                return 1;

            char *s = val_repr(&v);

            printf("%s\n", s);

            free(s);

            val_free(&v);
        } else {
            printf("\n");
        }

        return 0;
    }

    case ND_PRINT: {
        if (n->nc >= 1) {
            Value v;

            if (eval_expr(vm, n->ch[0], &v))
                return 1;

            char *s = val_repr(&v);

            printf("%s", s);
            fflush(stdout);

            free(s);

            val_free(&v);
        }

        return 0;
    }

    case ND_QUIT:
        exit(0);

    case ND_ERROR:
        if (n->nc >= 1) {
            Value v;

            if (eval_expr(vm, n->ch[0], &v))
                return 1;

            char *s = val_repr(&v);

            fprintf(stderr, "Error: %s\n", s);

            free(s);

            val_free(&v);
        } else {
            fprintf(stderr, "Error: %s\n", n->val[0] ? n->val : "unspecified");
        }

        exit(1);

    case ND_ASSIGN: {
        if (n->nc < 1)
            return 0;

        Node *id = n->ch[0];

        Value v;

        if (n->nc >= 2) {
            if (eval_expr(vm, n->ch[1], &v))
                return 1;
        } else {
            v = val_none();
        }

        vput((VarMap*)vm->vars, id->val, v);

        return 0;
    }

    case ND_ARITH: {
        if (n->nc < 2)
            return 0;

        const char *var_name = n->ch[0]->val;

        Value *cur = vget((VarMap*)vm->vars, var_name);
        Value l = cur ? val_clone(*cur) : val_int(0);

        Value r;

        if (eval_expr(vm, n->ch[1], &r)) {
            val_free(&l);
            return 1;
        }

        Value res;

        const char *op = n->val[0] ? n->val : "+";

        if (eval_binop(&l, op, &r, &res)) {
            val_free(&l);
            val_free(&r);
            return 1;
        }

        vput((VarMap*)vm->vars, var_name, res);

        val_free(&l);
        val_free(&r);

        return 0;
    }

    case ND_DELAY: {
        Value v;

        if (n->nc >= 1) {
            if (eval_expr(vm, n->ch[0], &v))
                return 1;
        } else {
            v = val_int(0);
        }

        int64_t ms = 0;

        if (v.type == VAL_INT)
            ms = v.int_val * 1000;

        if (v.type == VAL_FLOAT)
            ms = (int64_t)(v.float_val * 1000.0);

        val_free(&v);

#ifdef _WIN32
        Sleep((DWORD)ms);
#else
        usleep((useconds_t)(ms * 1000));
#endif

        return 0;
    }

    case ND_INPUT: {
        Node *var_node    = n->nc >= 1 ? n->ch[0] : NULL;
        Node *prompt_node = n->nc >= 2 ? n->ch[1] : NULL;
        Node *type_node   = n->nc >= 3 ? n->ch[2] : NULL;

        const char *msg = "? ";
        char type = 'S';

        if (var_node && var_node->type == ND_STR) {
            msg = var_node->val;

            if (prompt_node && prompt_node->type == ND_STR)
                type = prompt_node->val[0];
        } else {
            if (prompt_node && prompt_node->type == ND_STR)
                msg = prompt_node->val;

            if (type_node && type_node->type == ND_STR && type_node->val[0])
                type = type_node->val[0];
        }

        printf("%s", msg);
        fflush(stdout);

        char *line = NULL;
        size_t len = 0;

        ssize_t rd = getline(&line, &len, stdin);

        if (rd > 0) {
            size_t l = strlen(line);

            while (l > 0 && (line[l-1]=='\n'||line[l-1]=='\r'))
                line[--l] = '\0';
        }

        Value val;

        switch (type) {

        case 'I':
        case 'i':
            val = val_int(atoll(line ? line : "0"));
            break;

        case 'F':
        case 'f':
            val = val_float(atof(line ? line : "0.0"));
            break;

        case 'N':
        case 'n': {
            char *end;

            int64_t iv = strtoll(line ? line : "0", &end, 10);

            if (*end == '\0') {
                val = val_int(iv);
            } else {
                double dv = strtod(line ? line : "0.0", &end);
                val = (*end == '\0') ? val_float(dv) : val_int(0);
            }

            break;
        }

        case 'L':
        case 'l': {
            char tmp[2] = {line && line[0] ? line[0] : 0, 0};
            val = val_str(tmp);
            break;
        }

        case 'W':
        case 'w': {
            char *res = malloc(strlen(line ? line : "") + 1);
            int j=0;

            for (int i=0; line && line[i]; i++)
                if (isalpha(line[i]))
                    res[j++] = line[i];

            res[j]=0;

            val = val_str(res);

            free(res);

            break;
        }

        case 'E':
        case 'e': {
            char *res = malloc(strlen(line ? line : "") + 1);
            int j=0;

            for (int i=0; line && line[i]; i++)
                if (isalpha(line[i]) && islower(line[i]))
                    res[j++] = line[i];

            res[j]=0;

            val = val_str(res);

            free(res);

            break;
        }

        case 'U':
        case 'u': {
            char *res = malloc(strlen(line ? line : "") + 1);
            int j=0;

            for (int i=0; line && line[i]; i++)
                if (isalpha(line[i]) && isupper(line[i]))
                    res[j++] = line[i];

            res[j]=0;

            val = val_str(res);

            free(res);

            break;
        }

        case 'A':
        case 'a': {
            char *res = malloc(strlen(line ? line : "") + 1);
            int j=0;

            for (int i=0; line && line[i]; i++)
                if (isalpha(line[i]) || line[i]==' ')
                    res[j++] = line[i];

            res[j]=0;

            val = val_str(res);

            free(res);

            break;
        }

        case 'P':
        case 'p': {
            char *res = malloc(strlen(line ? line : "") + 1);
            int j=0;

            for (int i=0; line && line[i]; i++)
                if (isalnum(line[i]) || line[i]==' ')
                    res[j++] = line[i];

            res[j]=0;

            val = val_str(res);

            free(res);

            break;
        }

        default:
            val = val_str(line ? line : "");
            break;
        }

        free(line);

        if (var_node && var_node->type == ND_IDENT)
            vput((VarMap*)vm->vars, var_node->val, val);
        else
            val_free(&val);

        return 0;
    }

    case ND_STRREP: {
        if (n->nc < 3)
            return 0;

        Node *var = n->ch[0];
        Node *from = n->ch[1];
        Node *to   = n->ch[2];

        Value *cur = vget((VarMap*)vm->vars, var->val);

        if (!cur || cur->type != VAL_STR)
            return 0;

        const char *src = cur->str_val;
        const char *fs = from->val;
        const char *ts = to->val;

        size_t fl = strlen(fs);
        size_t tl = strlen(ts);

        if (fl == 0)
            return 0;

        size_t cap = 256;
        size_t rl = 0;

        char *res = malloc(cap);

        const char *p = src;

        while (*p) {
            if (!strncmp(p, fs, fl)) {
                while (rl + tl >= cap) {
                    cap *= 2;
                    res = realloc(res, cap);
                }

                memcpy(res+rl, ts, tl);

                rl += tl;
                p += fl;
            } else {
                if (rl+1 >= cap) {
                    cap *= 2;
                    res = realloc(res, cap);
                }

                res[rl++] = *p++;
            }
        }

        res[rl] = 0;

        vput((VarMap*)vm->vars, var->val,
             (Value){.type=VAL_STR,.str_val=res});

        return 0;
    }

    case ND_DEL: {
        VarMap *m = (VarMap*)vm->vars;

        int idx = h_fnv(n->val) % m->cap;

        while (m->e[idx].st) {
            if (m->e[idx].st == 2 && !strcmp(m->e[idx].key, n->val)) {
                free(m->e[idx].key);
                val_free(&m->e[idx].v);

                m->e[idx].st = 1;
                m->len--;
                m->tombstones++;

                return 0;
            }

            idx = (idx+1) % m->cap;
        }

        return 0;
    }

    case ND_IDENT: {
        Value v;

        if (eval_expr(vm, n, &v))
            return 1;

        val_free(&v);

        return 0;
    }

    case ND_IF: {
        Value cond;

        bool has_cond = (n->nc >= 1);

        if (has_cond && eval_expr(vm, n->ch[0], &cond))
            return 1;

        /* Conditional jump */
        if (n->val && n->val[0]) {
            if (has_cond && val_truthy(&cond)) {
                int *t = lget((LMap*)vm->labels, n->val);

                if (!t) {
                    fprintf(stderr, "Error: undefined label '%s'\n", n->val);

                    if (has_cond)
                        val_free(&cond);

                    return 1;
                }

                vm->ip = *t;
            }

            if (has_cond)
                val_free(&cond);

            return 0;
        }

        /* Block if */
        if (has_cond && val_truthy(&cond)) {
            if (vm->if_len >= vm->if_cap)
                vm->if_stack = da_grow(vm->if_stack, &vm->if_cap, sizeof(int));

            vm->if_stack[vm->if_len++] = vm->ip - 1;
        } else {
            int me = vm->matching_else[vm->ip-1];
            int mend = vm->matching_end[vm->ip-1];

            vm->ip = (me != -1 ? me : mend);
        }

        if (has_cond)
            val_free(&cond);

        return 0;
    }

    case ND_ELSE: {
        if (vm->if_len > 0) {
            int start = vm->if_stack[--vm->if_len];
            vm->ip = vm->matching_end[start];
        }

        return 0;
    }

    case ND_ENDIF: {
        if (vm->if_len > 0 &&
            vm->matching_end[vm->if_stack[vm->if_len-1]] == vm->ip-1) {
            vm->if_len--;
        }

        return 0;
    }

    case ND_WHILE: {
        Value cond;

        if (n->nc < 1 || eval_expr(vm, n->ch[0], &cond))
            return 1;

        if (val_truthy(&cond)) {
            if (vm->loop_len == 0 ||
                vm->loop_stack[vm->loop_len-1] != vm->ip - 1) {

                if (vm->loop_len >= vm->loop_cap)
                    vm->loop_stack = da_grow(vm->loop_stack, &vm->loop_cap, sizeof(int));

                vm->loop_stack[vm->loop_len++] = vm->ip - 1;
            }
        } else {
            if (vm->loop_len > 0 &&
                vm->loop_stack[vm->loop_len-1] == vm->ip - 1) {
                vm->loop_len--;
            }

            vm->ip = vm->matching_end[vm->ip-1] + 1;
        }

        val_free(&cond);

        return 0;
    }

    case ND_WEND: {
        if (vm->loop_len == 0)
            return 0;

        int start = vm->loop_stack[vm->loop_len-1];
        Node *start_nd = vm->prog->ch[start];

        if (start_nd->type == ND_FOR) {
            if (vm->loop_meta_len > 0) {
                struct LoopMeta *lm = &vm->loop_meta[vm->loop_meta_len-1];

                Value *cur = vget((VarMap*)vm->vars, lm->var);
                Value current = cur ? val_clone(*cur) : val_int(0);

                Value next;

                if (!eval_binop(&current, "+", &lm->step, &next)) {
                    vput((VarMap*)vm->vars, lm->var, next);

                    Value *updated = vget((VarMap*)vm->vars, lm->var);

                    if (in_range(updated, &lm->end, &lm->step)) {
                        val_free(&current);
                        vm->ip = start + 1;
                    } else {
                        val_free(&current);

                        vm->loop_len--;

                        free(lm->var);
                        val_free(&lm->end);
                        val_free(&lm->step);

                        vm->loop_meta_len--;

                        vm->ip = vm->matching_end[start] + 1;
                    }
                } else {
                    val_free(&current);

                    vm->loop_len--;

                    free(lm->var);
                    val_free(&lm->end);
                    val_free(&lm->step);

                    vm->loop_meta_len--;

                    return 1;
                }
            } else {
                vm->loop_len--;
                vm->ip = vm->matching_end[start] + 1;
            }
        } else {
            vm->ip = start;
        }

        return 0;
    }

    case ND_FOR: {
        if (n->nc < 3)
            return 0;

        Node *var = n->ch[0];

        Value start, end;

        if (eval_expr(vm, n->ch[1], &start))
            return 1;

        if (eval_expr(vm, n->ch[2], &end)) {
            val_free(&start);
            return 1;
        }

        Value step;

        if (n->nc >= 4) {
            if (eval_expr(vm, n->ch[3], &step)) {
                val_free(&start);
                val_free(&end);
                return 1;
            }
        } else {
            step = val_int(1);
        }

        vput((VarMap*)vm->vars, var->val, val_clone(start));

        if (val_is_zero(&step)) {
            val_free(&start);
            val_free(&end);
            val_free(&step);

            vm->ip = vm->matching_end[vm->ip-1] + 1;

            return 0;
        }

        if (in_range(&start, &end, &step)) {
            if (vm->loop_len >= vm->loop_cap)
                vm->loop_stack = da_grow(vm->loop_stack, &vm->loop_cap, sizeof(int));

            vm->loop_stack[vm->loop_len++] = vm->ip - 1;

            if (vm->loop_meta_len >= vm->loop_meta_cap)
                vm->loop_meta = da_grow(vm->loop_meta, &vm->loop_meta_cap,
                                        sizeof(*vm->loop_meta));

            vm->loop_meta[vm->loop_meta_len].var  = strdup(var->val);
            vm->loop_meta[vm->loop_meta_len].end  = val_clone(end);
            vm->loop_meta[vm->loop_meta_len].step = val_clone(step);

            vm->loop_meta_len++;
        } else {
            vm->ip = vm->matching_end[vm->ip-1] + 1;
        }

        val_free(&start);
        val_free(&end);
        val_free(&step);

        return 0;
    }

    case ND_LABEL:
        lput((LMap*)vm->labels, n->val, vm->ip - 1);
        return 0;

    case ND_JUMP: {
        int *t = lget((LMap*)vm->labels, n->val);

        if (!t) {
            fprintf(stderr, "Error: undefined label '%s'\n", n->val);
            return 1;
        }

        vm->ip = *t;

        return 0;
    }

    case ND_SWITCH: {
        Value sw;

        if (n->nc >= 1 && eval_expr(vm, n->ch[0], &sw))
            return 1;

        if (vm->switch_len >= vm->switch_cap)
            vm->switch_stack = da_grow(vm->switch_stack, &vm->switch_cap,
                                       sizeof(*vm->switch_stack));

        vm->switch_stack[vm->switch_len].val     = (n->nc>=1) ? sw : val_none();
        vm->switch_stack[vm->switch_len].matched = false;
        vm->switch_len++;

        return 0;
    }

    case ND_CASE: {
        if (vm->switch_len == 0)
            return 0;

        struct SwitchFrame *sf = &vm->switch_stack[vm->switch_len-1];

        if (sf->matched) {
            int i = vm->ip;
            int depth = 0;

            while (i < vm->node_count) {
                NT t = vm->prog->ch[i]->type;

                if (t == ND_SWITCH) {
                    depth++;
                } else if (t == ND_ENDSW) {
                    if (depth == 0) {
                        vm->ip = i;
                        return 0;
                    }

                    depth--;
                }

                i++;
            }

            vm->ip = vm->node_count;

            return 0;
        }

        Value cv;

        if (n->nc >= 1 && eval_expr(vm, n->ch[0], &cv))
            return 1;

        if (val_eq(&sf->val, &cv))
            sf->matched = true;

        if (n->nc >= 1)
            val_free(&cv);

        if (!sf->matched) {
            int i = vm->ip;
            int depth = 0;

            while (i < vm->node_count) {
                NT t = vm->prog->ch[i]->type;

                if (t == ND_SWITCH) {
                    depth++;
                } else if (t == ND_ENDSW) {
                    if (depth == 0) {
                        vm->ip = i;
                        return 0;
                    }

                    depth--;
                } else if (depth == 0 && (t == ND_CASE || t == ND_DEF)) {
                    vm->ip = i;
                    return 0;
                }

                i++;
            }

            vm->ip = vm->node_count;

            return 0;
        }

        return 0;
    }

    case ND_DEF: {
        if (vm->switch_len == 0)
            return 0;

        struct SwitchFrame *sf = &vm->switch_stack[vm->switch_len-1];

        if (sf->matched) {
            int i = vm->ip;
            int depth = 0;

            while (i < vm->node_count) {
                NT t = vm->prog->ch[i]->type;

                if (t == ND_SWITCH) {
                    depth++;
                } else if (t == ND_ENDSW) {
                    if (depth == 0) {
                        vm->ip = i;
                        return 0;
                    }

                    depth--;
                }

                i++;
            }

            vm->ip = vm->node_count;

            return 0;
        }

        sf->matched = true;

        return 0;
    }

    case ND_ENDSW:
        if (vm->switch_len > 0) {
            val_free(&vm->switch_stack[vm->switch_len-1].val);
            vm->switch_len--;
        }

        return 0;

    case ND_TRY: {
        if (vm->try_len >= vm->try_cap)
            vm->try_stack = da_grow(vm->try_stack, &vm->try_cap, sizeof(int));

        vm->try_stack[vm->try_len++] = vm->ip - 1;

        return 0;
    }

    case ND_CATCH: {
        if (vm->try_len > 0) {
            int t = vm->try_stack[--vm->try_len];
            vm->ip = vm->matching_end[t] + 1;
        }

        return 0;
    }

    case ND_ENDTRY:
        return 0;

    case ND_FUNC:
        return 0;

    case ND_RETURN:
        if (n->nc >= 1) {
            if (eval_expr(vm, n->ch[0], &vm->return_value))
                return 1;
        } else {
            vm->return_value = val_none();
        }

        return 2;

    case ND_ENDFN:
        return 2;

    case ND_EXEC:
        if (n->val[0])
            system(n->val);

        return 0;

    case ND_CEXEC:
        fprintf(stderr, "Warning: C exec (!) — not supported, already running as C\n");
        return 0;

    case ND_IMPORT: {
        const char *target = n->val;

        /* Built-in modules */
        if (!strcmp(target, "math") ||
            !strcmp(target, "os") ||
            !strcmp(target, "random")) {
            vput((VarMap*)vm->vars, target,
                 (Value){.type=VAL_STR, .str_val=strdup(target)});
            return 0;
        }

        /* Vulpin module file */
        char *file;

        if (strstr(target, ".vul")) {
            file = strdup(target);
        } else {
            file = malloc(strlen(target) + 5);
            sprintf(file, "%s.vul", target);
        }

        char *modname = module_name_from_path(file);

        ModMap *mods = (ModMap*)vm->modules;

        if (mget(mods, modname)) {
            free(file);
            free(modname);
            return 0;
        }

        char *src = read_whole_file(file);

        if (!src) {
            fprintf(stderr, "Warning: unknown module '%s'\n", target);
            free(file);
            free(modname);
            return 0;
        }

        Node *sub_prog = parse(src);

        free(src);

        VM *modvm = vm_new(sub_prog);

        vm_precompute(modvm);

        mput(mods, modname, sub_prog, modvm);

        int rc = vm_run(modvm);

        if (rc) {
            fprintf(stderr, "Error: import '%s' failed\n", target);
            free(file);
            free(modname);
            return 1;
        }

        free(file);
        free(modname);

        return 0;
    }

    case ND_CLASS:
        if (n->val[0])
            vput((VarMap*)vm->vars, n->val, val_str("class"));

        return 0;

    default: {
        Value v;

        int rc = eval_expr(vm, n, &v);

        if (!rc)
            val_free(&v);

        return rc;
    }

    }
}

/* ================================================================
   precompute block matching & labels
   ================================================================ */
void vm_precompute(VM *vm) {
    int n = vm->node_count;
    Node *prog = vm->prog;

    free(vm->matching_end);
    vm->matching_end = malloc(n * sizeof(int));

    free(vm->matching_else);
    vm->matching_else = malloc(n * sizeof(int));

    for (int i = 0; i < n; i++) {
        vm->matching_end[i]  = n;
        vm->matching_else[i] = -1;
    }

    typedef struct { int idx; int type; } SE;

    SE *stack = NULL;
    int sc = 0, scap = 0;

#define PUSH(i,t) do { \
    if (sc >= scap) { \
        scap = scap ? scap*2 : 16; \
        stack = realloc(stack, scap*sizeof(SE)); \
    } \
    stack[sc].idx = (i); \
    stack[sc].type = (t); \
    sc++; \
} while(0)

    for (int i = 0; i < n; i++) {
        Node *nd = prog->ch[i];

        switch (nd->type) {

        case ND_IF:
            PUSH(i, ND_IF);
            break;

        case ND_ELSE:
            for (int j = sc-1; j >= 0; j--) {
                if (stack[j].type == ND_IF &&
                    vm->matching_else[stack[j].idx] == -1) {
                    vm->matching_else[stack[j].idx] = i;
                    break;
                }
            }
            break;

        case ND_ENDIF:
            for (int j = sc-1; j >= 0; j--) {
                if (stack[j].type == ND_IF) {
                    int s = stack[j].idx;

                    memmove(&stack[j], &stack[j+1], (sc-j-1)*sizeof(SE));
                    sc--;

                    vm->matching_end[s] = i;

                    break;
                }
            }
            break;

        case ND_WHILE:
        case ND_FOR:
        case ND_SWITCH:
        case ND_TRY:
        case ND_FUNC:
            PUSH(i, nd->type);
            break;

        case ND_WEND: {
            int match = ND_WHILE;

            for (int j = sc-1; j >= 0; j--) {
                if (stack[j].type == ND_FOR) {
                    match = ND_FOR;
                    break;
                }

                if (stack[j].type == ND_WHILE)
                    break;
            }

            for (int j = sc-1; j >= 0; j--) {
                if (stack[j].type == match) {
                    int s = stack[j].idx;

                    memmove(&stack[j], &stack[j+1], (sc-j-1)*sizeof(SE));
                    sc--;

                    vm->matching_end[s] = i;
                    vm->matching_end[i] = s;

                    break;
                }
            }

            break;
        }

        case ND_ENDSW:
        case ND_ENDTRY:
        case ND_ENDFN: {
            int match = nd->type == ND_ENDSW  ? ND_SWITCH :
                        nd->type == ND_ENDTRY ? ND_TRY : ND_FUNC;

            for (int j = sc-1; j >= 0; j--) {
                if (stack[j].type == match) {
                    int s = stack[j].idx;

                    memmove(&stack[j], &stack[j+1], (sc-j-1)*sizeof(SE));
                    sc--;

                    vm->matching_end[s] = i;
                    vm->matching_end[i] = s;

                    break;
                }
            }

            break;
        }

        case ND_CATCH:
            for (int j = sc-1; j >= 0; j--) {
                if (stack[j].type == ND_TRY &&
                    vm->matching_else[stack[j].idx] == -1) {
                    vm->matching_else[stack[j].idx] = i;
                    break;
                }
            }
            break;

        case ND_LABEL:
            if (nd->val[0])
                lput((LMap*)vm->labels, nd->val, i);
            break;

        default:
            break;
        }
    }

    free(stack);

    free(vm->skip_to);

    vm->skip_to = malloc(n * sizeof(int));

    for (int i = 0; i < n; i++)
        vm->skip_to[i] = -1;

    for (int i = 0; i < n; i++) {
        Node *nd = prog->ch[i];

        if (nd->type == ND_FUNC && vm->matching_end[i] < n) {
            vm->skip_to[i] = vm->matching_end[i] + 1;

            FuncInfo fi;

            fi.start_ip = i;
            fi.end_ip = vm->matching_end[i];
            fi.param_count = nd->nc;
            fi.params = malloc(fi.param_count * sizeof(char*));

            for (int j = 0; j < fi.param_count; j++) {
                if (nd->ch[j] && nd->ch[j]->type == ND_IDENT)
                    fi.params[j] = strdup(nd->ch[j]->val);
                else
                    fi.params[j] = strdup("");
            }

            if (nd->val[0])
                fput((FMap*)vm->funcs, nd->val, fi);
        }
    }
}

/* ================================================================
   run
   ================================================================ */
int vm_run(VM *vm) {
    Node *prog = vm->prog;

    while (vm->ip < vm->node_count) {

        if (vm->skip_to[vm->ip] != -1) {
            vm->ip = vm->skip_to[vm->ip];
            continue;
        }

        Node *nd = prog->ch[vm->ip];

        vm->ip++;

        int rs = exec_stmt(vm, nd);

        if (rs == 2) {
            fprintf(stderr, "Error: return/endfn outside function\n");
            return 1;
        }

        if (rs == 1) {
            if (vm->try_len > 0) {
                int t = vm->try_stack[vm->try_len-1];
                int c = vm->matching_else[t];

                if (c != -1) {
                    vm->try_len--;

                    Node *catch_node = prog->ch[c];

                    if (catch_node->val[0])
                        vput((VarMap*)vm->vars, catch_node->val, val_str("error"));

                    vm->ip = c + 1;

                    continue;
                }
            }

            if (nd->line)
                fprintf(stderr, "Error on line %d\n", nd->line);

            return 1;
        }
    }

    return 0;
}
