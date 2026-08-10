#include"lib/vm.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<ctype.h>

/* ================================================================
   Tokenizer
   ================================================================ */
typedef enum{
    TOK_NUM, TOK_STR, TOK_IDENT, TOK_BINOP,
    TOK_EQ, TOK_EOF, TOK_UNK,
    TOK_LPAREN, TOK_RPAREN, TOK_COMMA, TOK_DEREF,
    TOK_STRMETH, TOK_DOT
}TokType;

typedef struct{TokType type;char*val;}Token;

static const char*lexeme;
static Token tok;

static Token mk(TokType t,const char*start,const char*end){
    Token tk;
    tk.type=t;

    int len=end-start;

    tk.val=malloc(len+1);
    memcpy(tk.val,start,len);
    tk.val[len]=0;

    return tk;
}

static void advance(void){
    /* skip whitespace and comments */
    while(1) {
        while(*lexeme==' '||*lexeme=='\t'||*lexeme=='\n'||*lexeme=='\r')
            lexeme++;

        if(*lexeme == '#') {
            while(*lexeme && *lexeme != '\n' && *lexeme != '\r')
                lexeme++;

            continue;
        }

        break;
    }

    if(!*lexeme){
        tok.type=TOK_EOF;
        tok.val=strdup("");
        return;
    }

    const char*start=lexeme;
    char c=*lexeme++;

    /* number */
    if(isdigit(c)||(c=='.'&&isdigit(*lexeme))){
        while(isdigit(*lexeme)||*lexeme=='.')
            lexeme++;

        tok=mk(TOK_NUM,start,lexeme);
        return;
    }

    /* string */
    if(c=='"'){
        start=lexeme;

        while(*lexeme&&*lexeme!='"')
            lexeme++;

        tok=mk(TOK_STR,start,lexeme);

        if(*lexeme=='"')
            lexeme++;

        return;
    }

    /* identifier / keyword */
    if(isalpha(c)||c=='_'){
        while(isalnum(*lexeme)||*lexeme=='_')
            lexeme++;

        tok=mk(TOK_IDENT,start,lexeme);
        return;
    }

    char next = *lexeme;
    if ((c=='<' || c=='>' || c=='!') && next=='=') {
      lexeme++;  /* consume = */
      tok=mk(TOK_BINOP,start,lexeme);
      return;
    }
    if (c=='=' && next=='=') {
      lexeme++;  /* consume second = */
      tok=mk(TOK_BINOP,start,lexeme);
      return;
    }

    switch(c){
    case'=':
        tok=mk(TOK_EQ,start,lexeme);
        break;

    case'+':
    case'-':
    case'*':
    case'/':
    case'%':
        tok=mk(TOK_BINOP,start,lexeme);
        break;

    case'<':
    case'>':
        tok=mk(TOK_BINOP,start,lexeme);
        break;

    case'(':
        tok=mk(TOK_LPAREN,start,lexeme);
        break;

    case')':
        tok=mk(TOK_RPAREN,start,lexeme);
        break;

    case',':
        tok=mk(TOK_COMMA,start,lexeme);
        break;

    case'$':
        tok=mk(TOK_DEREF,start,lexeme);
        break;

    case'.':
        if(*lexeme=='U'||*lexeme=='L'||*lexeme=='S'||*lexeme=='T'||*lexeme=='C'){
            tok=mk(TOK_STRMETH,lexeme,lexeme+1);
            lexeme++;
        }else if(isalpha(*lexeme)){
            tok=mk(TOK_DOT,start,lexeme);
        }else{
            tok=mk(TOK_UNK,start,lexeme);
        }
        break;

    default:
        tok=mk(TOK_UNK,start,lexeme);
        break;
    }
}

static void initLexer(const char*src){
    lexeme=src;
    advance();
}

/* ================================================================
   AST helpers
   ================================================================ */
static Node *mkNode(NT t,char*v){
    Node*n=malloc(sizeof(Node));

    n->type=t;
    n->val=v;
    n->nc=0;
    n->cap=0;
    n->ch=NULL;
    n->line=0;

    return n;
}

static void addChild(Node*p,Node*c){
    if(p->nc>=p->cap){
        p->cap=p->cap?p->cap*2:4;
        p->ch=realloc(p->ch,p->cap*sizeof(Node*));
    }

    p->ch[p->nc++]=c;
}

/* forward declarations */
static Node *parseExpr(void);
static Node *parsePrimary(void);
static Node *parseTerm(void);

/* ================================================================
   Parser
   ================================================================ */

static Node *parsePrimary(void){
    /* unary + / - */
    if (tok.type == TOK_BINOP && tok.val[1] == 0 &&
        (tok.val[0] == '-' || tok.val[0] == '+')) {
        char *op = strdup(tok.val);
        advance();

        Node *operand = parseTerm();
        Node *b = mkNode(ND_BINOP, op);

        addChild(b, operand);

        return b;
    }

    switch(tok.type){

    case TOK_NUM:{
        Node*n=mkNode(ND_NUM,strdup(tok.val));
        advance();
        return n;
    }

    case TOK_STR:{
        Node*n=mkNode(ND_STR,strdup(tok.val));
        advance();
        return n;
    }

    case TOK_IDENT:{
        Node*n=mkNode(ND_IDENT,strdup(tok.val));
        advance();

        /* postfix function call */
        if(tok.type==TOK_LPAREN){
            advance();

            Node*call=mkNode(ND_CALL,strdup(n->val));
            freeTree(n);

            if(tok.type!=TOK_RPAREN){
                addChild(call,parseExpr());

                while(tok.type==TOK_COMMA){
                    advance();
                    addChild(call,parseExpr());
                }
            }

            if(tok.type!=TOK_RPAREN){
                fprintf(stderr,"error: expected ')'\n");
            }else{
                advance();
            }

            return call;
        }

        return n;
    }

    case TOK_DEREF:{
        advance();

        if(tok.type!=TOK_IDENT){
            fprintf(stderr,"error: expected identifier after $\n");
            return mkNode(ND_IDENT,strdup(""));
        }

        Node*n=mkNode(ND_IDENT,strdup(tok.val));
        advance();

        if(tok.type==TOK_LPAREN){
            advance();

            Node*call=mkNode(ND_CALL,strdup(n->val));
            freeTree(n);

            if(tok.type!=TOK_RPAREN){
                addChild(call,parseExpr());

                while(tok.type==TOK_COMMA){
                    advance();
                    addChild(call,parseExpr());
                }
            }

            if(tok.type!=TOK_RPAREN){
                fprintf(stderr,"error: expected ')'\n");
            }else{
                advance();
            }

            return call;
        }

        return n;
    }

    case TOK_LPAREN:{
        advance();

        Node*n=parseExpr();

        if(tok.type!=TOK_RPAREN){
            fprintf(stderr,"error: expected ')'\n");
        }else{
            advance();
        }

        return n;
    }

    default:
        fprintf(stderr,"error: unexpected token '%s'\n",tok.val);
        advance();
        return mkNode(ND_NUM,strdup("0"));
    }
}

static Node *parseTerm(void){
    Node*n=parsePrimary();

    /* module/member: ident.ident */
    while(tok.type==TOK_DOT){
        advance();

        if(tok.type!=TOK_IDENT){
            fprintf(stderr,"error: expected identifier after '.'\n");
            break;
        }

        size_t len=strlen(n->val)+strlen(tok.val)+2;
        char *full=malloc(len);

        sprintf(full,"%s.%s",n->val,tok.val);

        free(n->val);
        n->val=full;

        advance();
    }

    /* chained calls */
    while(tok.type==TOK_LPAREN){
        advance();

        Node*call=mkNode(ND_CALL,n->val?strdup(n->val):strdup(""));
        freeTree(n);

        if(tok.type!=TOK_RPAREN){
            addChild(call,parseExpr());

            while(tok.type==TOK_COMMA){
                advance();
                addChild(call,parseExpr());
            }
        }

        if(tok.type!=TOK_RPAREN){
            fprintf(stderr,"error: expected ')'\n");
        }else{
            advance();
        }

        n=call;
    }

    while(tok.type==TOK_STRMETH){
        Node*m=mkNode(ND_STRMETH,strdup(tok.val));
        advance();

        addChild(m,n);

        n=m;
    }

    return n;
}

static Node *parseExpr(void){
    Node*n=parseTerm();

    while(tok.type==TOK_BINOP){
        char* op = strdup(tok.val);
        advance();

        Node*r=parseTerm();
        Node*b=mkNode(ND_BINOP,op);

        addChild(b,n);
        addChild(b,r);

        n=b;
    }

    return n;
}

/* ================================================================
   Parse a single Vulpin line
   ================================================================ */
Node *parseStatement(const char*line){
    if(!line||!*line)
        return mkNode(ND_IDENT,strdup(""));

    const char*raw=line;

    while(*raw==' '||*raw=='\t')
        raw++;

    if(!*raw)
        return mkNode(ND_IDENT,strdup(""));

    char cmd=*raw;

    const char*rest=raw+1;

    while(*rest==' '||*rest=='\t')
        rest++;

    initLexer(rest);

    switch(cmd){

    case'#':
        return mkNode(ND_IDENT,strdup(""));

    case'G':{
        Node*n=mkNode(ND_PRINTNL,strdup(""));
        Node*e=parseExpr();

        if(e)addChild(n,e);

        return n;
    }

    case'P':{
        Node*n=mkNode(ND_PRINT,strdup(""));
        Node*e=parseExpr();

        if(e)addChild(n,e);

        return n;
    }

    case'Q':
        return mkNode(ND_QUIT,strdup(""));

    case'X':{
        Node*n=mkNode(ND_ERROR,strdup(""));

        if(tok.type!=TOK_EOF){
            Node*e=parseExpr();
            if(e)addChild(n,e);
        }

        return n;
    }

    case'E':{
        if(tok.type==TOK_IDENT){
            Node*n=mkNode(ND_ASSIGN,strdup(""));
            Node*id=mkNode(ND_IDENT,strdup(tok.val));

            addChild(n,id);

            advance();

            if(tok.type!=TOK_EOF){
                if(tok.type==TOK_EQ)
                    advance();

                Node*e=parseExpr();

                if(e)addChild(n,e);
            }

            return n;
        }

        return parseExpr();
    }

    case'D':{
        if(tok.type==TOK_STR){
            Node*n=mkNode(ND_DEL,strdup(tok.val));
            advance();
            return n;
        }

        Node*n=mkNode(ND_DELAY,strdup(""));
        Node*e=parseExpr();

        if(e)addChild(n,e);

        return n;
    }

    case'K':{
        Node*n=mkNode(ND_INPUT,strdup(""));

        if(tok.type==TOK_IDENT){
            addChild(n,mkNode(ND_IDENT,strdup(tok.val)));
            advance();

            if(tok.type==TOK_STR){
                addChild(n,mkNode(ND_STR,strdup(tok.val)));
                advance();

                if(tok.type==TOK_IDENT){
                    addChild(n,mkNode(ND_STR,strdup(tok.val)));
                    advance();
                }
            }
        }else if(tok.type==TOK_STR){
            addChild(n,mkNode(ND_STR,strdup(tok.val)));
            advance();

            if(tok.type==TOK_IDENT){
                addChild(n,mkNode(ND_STR,strdup(tok.val)));
                advance();
            }
        }

        return n;
    }

    case'A':{
        Node*n=mkNode(ND_ARITH,strdup(""));

        if(tok.type==TOK_STR){
            addChild(n,mkNode(ND_STR,strdup(tok.val)));
            advance();
        }

        if(tok.type==TOK_BINOP){
            n->val=strdup(tok.val);
            advance();
        }

        if(tok.type!=TOK_EOF){
            Node*e=parseExpr();

            if(e)addChild(n,e);
        }

        return n;
    }

    case'?':{
        Node*n=mkNode(ND_IF,strdup(""));
        Node*e=parseExpr();

        if(e)addChild(n,e);

        /* Conditional jump: ? expr J label */
        if(tok.type==TOK_IDENT && strcmp(tok.val,"J")==0){
            advance();

            if(tok.type==TOK_IDENT){
                free(n->val);
                n->val=strdup(tok.val);
                advance();
            }
        }

        return n;
    }

    case':':
        return mkNode(ND_ELSE,strdup(""));

    case';':
        return mkNode(ND_ENDIF,strdup(""));

    case'@':{
        Node*n=mkNode(ND_WHILE,strdup(""));
        Node*e=parseExpr();

        if(e)addChild(n,e);

        return n;
    }

    case'&':
        return mkNode(ND_WEND,strdup(""));

    case'F':{
        if(tok.type==TOK_IDENT){
            char*fname=strdup(tok.val);
            advance();

            Node*n=mkNode(ND_FUNC,fname);

            if(tok.type==TOK_LPAREN){
                advance();

                while(tok.type==TOK_IDENT){
                    addChild(n,mkNode(ND_IDENT,strdup(tok.val)));
                    advance();

                    if(tok.type==TOK_COMMA)
                        advance();
                }

                if(tok.type==TOK_RPAREN)
                    advance();
            }

            return n;
        }

        return mkNode(ND_FUNC,strdup(""));
    }

    case'~':
        return mkNode(ND_ENDFN,strdup(""));

    case'R':{
        Node*n=mkNode(ND_RETURN,strdup(""));

        if(tok.type!=TOK_EOF){
            Node*e=parseExpr();

            if(e)addChild(n,e);
        }

        return n;
    }

    case'L':{
        Node*n=mkNode(ND_LABEL,strdup(""));

        if(tok.type==TOK_IDENT){
            free(n->val);
            n->val=strdup(tok.val);
            advance();
        }

        return n;
    }

    case'J':{
        Node*n=mkNode(ND_JUMP,strdup(""));

        if(tok.type==TOK_IDENT){
            free(n->val);
            n->val=strdup(tok.val);
            advance();
        }

        return n;
    }

    case'W':{
        Node*n=mkNode(ND_SWITCH,strdup(""));
        Node*e=parseExpr();

        if(e)addChild(n,e);

        return n;
    }

    case'V':{
        Node*n=mkNode(ND_CASE,strdup(""));
        Node*e=parseExpr();

        if(e)addChild(n,e);

        return n;
    }

    case'N':
        return mkNode(ND_DEF,strdup(""));

    case'Z':
        return mkNode(ND_ENDSW,strdup(""));

    case'T':
        return mkNode(ND_TRY,strdup(""));

    case'C':{
        Node*n=mkNode(ND_CATCH,strdup(""));

        if(tok.type==TOK_IDENT){
            free(n->val);
            n->val=strdup(tok.val);
            advance();
        }else if(tok.type==TOK_STR){
            free(n->val);
            n->val=strdup(tok.val);
            advance();
        }

        return n;
    }

    case'Y':
        return mkNode(ND_ENDTRY,strdup(""));

    case'O':{
        Node*n=mkNode(ND_FOR,strdup(""));

        if(tok.type==TOK_IDENT){
            addChild(n,mkNode(ND_IDENT,strdup(tok.val)));
            advance();
        }

        if(tok.type!=TOK_EOF){
            addChild(n,parseTerm()); /* start */

            if(tok.type!=TOK_EOF){
                addChild(n,parseTerm()); /* end */

                if(tok.type!=TOK_EOF){
                    addChild(n,parseTerm()); /* step */
                }
            }
        }

        return n;
    }

    case'U':{
        Node*n=mkNode(ND_IMPORT,strdup(""));

        if(tok.type==TOK_STR){
            free(n->val);
            n->val=strdup(tok.val);
            advance();
        }else if(tok.type==TOK_IDENT){
            free(n->val);
            n->val=strdup(tok.val);
            advance();
        }

        return n;
    }

    case'S':{
        Node*n=mkNode(ND_STRREP,strdup(""));

        for(int i=0;i<3;i++){
            if(tok.type==TOK_STR){
                addChild(n,mkNode(ND_STR,strdup(tok.val)));
                advance();
            }
        }

        return n;
    }

    default:{
        initLexer(line);

        if(tok.type==TOK_IDENT){
            const char *saved_lexeme = lexeme;
            Token saved_tok = tok;

            advance();

            if(tok.type==TOK_EQ){
                Node*n=mkNode(ND_ASSIGN,strdup(""));

                addChild(n,mkNode(ND_IDENT,strdup(saved_tok.val)));

                advance();

                if(tok.type!=TOK_EOF){
                    Node*e=parseExpr();

                    if(e)addChild(n,e);
                }

                return n;
            }

            lexeme = saved_lexeme;
            tok = saved_tok;
        }

        return parseExpr();
    }

    }
}

/* ================================================================
   Parse full program
   ================================================================ */
Node *parse(const char*src){
    Node*prog=mkNode(ND_PROG,strdup(""));

    char*buf=strdup(src);
    char*text=buf;

    int line=1;

    char*lineptr=strtok(text,"\n");

    while(lineptr){
        char*end=lineptr+strlen(lineptr);

        while(end>lineptr&&(end[-1]=='\r'))
            *--end=0;

        if(*lineptr){
            Node*n=parseStatement(lineptr);

            if(n){
                n->line=line;
                addChild(prog,n);
            }
        }

        line++;

        lineptr=strtok(NULL,"\n");
    }

    free(buf);

    return prog;
}

/* ================================================================
   Debug/print helpers
   ================================================================ */
const char*nodeName(NT t){
    switch(t){
    case ND_PROG:   return"PROGRAM";
    case ND_NUM:    return"NUM";
    case ND_STR:    return"STR";
    case ND_IDENT:  return"IDENT";
    case ND_BINOP:  return"BINOP";
    case ND_PRINTNL:return"PRINT_NL";
    case ND_PRINT:  return"PRINT";
    case ND_IF:     return"IF";
    case ND_ELSE:   return"ELSE";
    case ND_ENDIF:  return"ENDIF";
    case ND_WHILE:  return"WHILE";
    case ND_WEND:   return"WEND";
    case ND_FOR:    return"FOR";
    case ND_ASSIGN: return"ASSIGN";
    case ND_QUIT:   return"QUIT";
    case ND_LABEL:  return"LABEL";
    case ND_JUMP:   return"JUMP";
    case ND_SWITCH: return"SWITCH";
    case ND_CASE:   return"CASE";
    case ND_DEF:    return"DEFAULT";
    case ND_ENDSW:  return"ENDSWITCH";
    case ND_FUNC:   return"FUNC";
    case ND_RETURN: return"RETURN";
    case ND_ENDFN:  return"ENDFN";
    case ND_ERROR:  return"ERROR";
    case ND_DELAY:  return"DELAY";
    case ND_INPUT:  return"INPUT";
    case ND_IMPORT: return"IMPORT";
    case ND_STRREP: return"STRREP";
    case ND_TRY:    return"TRY";
    case ND_CATCH:  return"CATCH";
    case ND_ENDTRY: return"ENDTRY";
    case ND_EXEC:   return"EXEC";
    case ND_CEXEC:  return"CEXEC";
    case ND_CLASS:  return"CLASS";
    case ND_ARITH:  return"ARITH";
    case ND_DEL:    return"DEL";
    case ND_CALL:   return"CALL";
    case ND_STRMETH:return"STRMETH";
    default:        return"???";
    }
}

void printTree(Node*n,int depth){
    if(!n)return;

    for(int i=0;i<depth;i++)
        printf("  ");

    printf("%s",nodeName(n->type));

    if(n->val&&*n->val)
        printf(" [%s]",n->val);

    printf("\n");

    for(int i=0;i<n->nc;i++)
        printTree(n->ch[i],depth+1);
}

void freeTree(Node*n){
    if(!n)return;

    for(int i=0;i<n->nc;i++)
        freeTree(n->ch[i]);

    free(n->ch);
    free(n->val);
    free(n);
}
