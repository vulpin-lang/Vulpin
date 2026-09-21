#define _POSIX_C_SOURCE 200809L
#include"lib/vm.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<ctype.h>
typedef enum{TOK_NUM,TOK_STR,TOK_IDENT,TOK_BINOP,TOK_EQ,TOK_EOF,TOK_EOL,TOK_UNK,TOK_LPAREN,TOK_RPAREN,TOK_COMMA,TOK_DEREF,TOK_STRMETH,TOK_FMTSTR,TOK_DOT,TOK_QUESTION,TOK_AT,TOK_LBRACK,TOK_RBRACK,TOK_LBRACE,TOK_RBRACE,TOK_COLON,TOK_SEMI}TT2;
typedef struct{TT2 t;char*v;}Tk2;
static const char*lx;
static const char*tokStart;
static Tk2 tk;
static int isStrMeth(const char*);
static int barStartsComp(void);
static int parse_error;
static char parse_error_msg[256];
static const char*parser_source;
static char*parser_source_owned;
static const char*parser_error_pos;
static int parser_error_line;
static int parser_error_col;
static void errorLocation(const char*pos,int*lineOut,int*colOut){
int line=1,col=1;
if(parser_source&&pos&&pos>=parser_source){
for(const char*p=parser_source;p<pos&&*p;p++){
if(*p=='\n'){line++;col=1;}else col++;
}
}
if(lineOut)*lineOut=line;
if(colOut)*colOut=col;
}
static void showErrorContext(const char*pos,int line,int col){
if(!parser_source||!pos){
fprintf(stderr,"  --> line %d, column %d\n",line,col);
return;
}
const char*ls=pos;
while(ls>parser_source&&ls[-1]!='\n')ls--;
const char*le=pos;
while(*le&&*le!='\n'&&*le!='\r')le++;
size_t len=(size_t)(le-ls);
if(len>240)len=240;
fprintf(stderr,"  --> line %d, column %d\n",line,col);
fprintf(stderr,"   |\n%2d | %.*s\n   | ",line,(int)len,ls);
for(int i=1;i<col;i++){
if(ls[i-1]=='\t')fputc('\t',stderr);else fputc(' ',stderr);
}
fprintf(stderr,"^\n");
}
static void perr_at(const char*pos,const char*msg){
if(parse_error)return;
parse_error=1;
snprintf(parse_error_msg,sizeof(parse_error_msg),"%s",msg?msg:"syntax error");
parser_error_pos=pos?pos:lx;
errorLocation(parser_error_pos,&parser_error_line,&parser_error_col);
fprintf(stderr,"error: %s\n",parse_error_msg);
showErrorContext(parser_error_pos,parser_error_line,parser_error_col);
}
void parser_show_location_source(const char*,int,int);
static void perr(const char*msg){perr_at(tokStart?tokStart:lx,msg);}
static void perr_node(const Node*n,const char*msg){
if(parse_error)return;
parse_error=1;
snprintf(parse_error_msg,sizeof(parse_error_msg),"%s",msg?msg:"syntax error");
parser_error_line=n&&n->l?n->l:1;
parser_error_col=n&&n->col?n->col:1;
fprintf(stderr,"error: %s\n",parse_error_msg);
parser_show_location_source(parser_source,parser_error_line,parser_error_col);
}
static Tk2 mk2(TT2 t,const char*s,const char*e){
Tk2 r;
r.t=t;
int n=(int)(e-s);
r.v=malloc((size_t)n+1);
if(!r.v){fprintf(stderr,"out of memory\n");exit(1);}
memcpy(r.v,s,(size_t)n);
r.v[n]=0;
return r;
}
static void freetk(void){free(tk.v);tk.v=NULL;}
static void scanString(char quote,const char**startOut,const char**endOut){
const char*s=lx;
int triple=(lx[0]==quote&&lx[1]==quote&&lx[2]==quote);
if(triple)lx+=3;
else lx++;
const char*start=lx;
for(;;){
if(!*lx)break;
if(*lx=='\\'&&lx[1]){
lx+=2;
continue;
}
if(triple){
if(lx[0]==quote&&lx[1]==quote&&lx[2]==quote){
*startOut=start;
*endOut=lx;
lx+=3;
return;
}
}else if(*lx==quote){
*startOut=start;
*endOut=lx;
lx++;
return;
}
lx++;
}
*startOut=start;
*endOut=lx;
perr("unterminated string");
(void)s;
}
static void adv(void){
freetk();
while(1){
while(*lx==' '||*lx=='\t'||*lx=='\r')lx++;
if(*lx=='#'){while(*lx&&*lx!='\n'&&*lx!='\r')lx++;continue;}
break;
}
tokStart=lx;
if(!*lx){tk.t=TOK_EOF;tk.v=strdup("");return;}
if(*lx=='\n'){lx++;tk=mk2(TOK_EOL,tokStart,lx);return;}
const char*s=lx;char c=*lx++;
if(isdigit((unsigned char)c)||(c=='.'&&isdigit((unsigned char)*lx))){
int dots=c=='.';
while(isdigit((unsigned char)*lx)||*lx=='.'){if(*lx=='.')dots++;lx++;}
if(*lx=='e'||*lx=='E'){
lx++;if(*lx=='+'||*lx=='-')lx++;
if(!isdigit((unsigned char)*lx)){while(isalnum((unsigned char)*lx)||*lx=='.')lx++;tk=mk2(TOK_UNK,s,lx);perr("invalid numeric literal");return;}
while(isdigit((unsigned char)*lx))lx++;
}
tk=mk2(TOK_NUM,s,lx);if(dots>1){tk.t=TOK_UNK;perr("invalid numeric literal");}return;
}
if(c=='\''||c=='"'||c=='`'){
const char*bs,*be;lx=s;scanString(c,&bs,&be);tk=mk2(TOK_STR,bs,be);return;
}
if((c=='r'||c=='R')&&(*lx=='\''||*lx=='"'||*lx=='`')){
const char q=*lx;const char*bs,*be;scanString(q,&bs,&be);tk=mk2(TOK_STR,bs,be);return;
}
if(c=='$'){
if(*lx=='\''||*lx=='"'||*lx=='`'){
const char q=*lx;const char*bs,*be;scanString(q,&bs,&be);tk=mk2(TOK_FMTSTR,bs,be);return;
}
tk=mk2(TOK_DEREF,s,lx);return;
}
if(isalpha((unsigned char)c)||c=='_'){while(isalnum((unsigned char)*lx)||*lx=='_')lx++;tk=mk2(TOK_IDENT,s,lx);return;}
char n=*lx;
if((c=='<'||c=='>'||c=='!'||c=='+'||c=='-'||c=='*'||c=='/'||c=='%')&&n=='='){lx++;tk=mk2(TOK_BINOP,s,lx);return;}
if(c=='='&&n=='='){lx++;tk=mk2(TOK_BINOP,s,lx);return;}
if((c=='&'||c=='|')&&n==c){lx++;tk=mk2(TOK_BINOP,s,lx);return;}
if(c=='<'&&n=='-'){lx++;tk=mk2(TOK_BINOP,s,lx);return;}
switch(c){
case'=':tk=mk2(TOK_EQ,s,lx);break;
case'+':case'-':case'*':case'/':case'%':case'<':case'>':case'!':case'&':case'|':tk=mk2(TOK_BINOP,s,lx);break;
case'(':tk=mk2(TOK_LPAREN,s,lx);break;
case')':tk=mk2(TOK_RPAREN,s,lx);break;
case',':tk=mk2(TOK_COMMA,s,lx);break;
case'[':tk=mk2(TOK_LBRACK,s,lx);break;
case']':tk=mk2(TOK_RBRACK,s,lx);break;
case'{':tk=mk2(TOK_LBRACE,s,lx);break;
case'}':tk=mk2(TOK_RBRACE,s,lx);break;
case':':tk=mk2(TOK_COLON,s,lx);break;
case';':tk=mk2(TOK_SEMI,s,lx);break;
case'?':tk=mk2(TOK_QUESTION,s,lx);break;
case'@':tk=mk2(TOK_AT,s,lx);break;
case'.':{
const char*ms=lx;while(isalpha((unsigned char)*lx))lx++;
size_t ml=(size_t)(lx-ms);char mb[32];size_t z=ml<sizeof(mb)-1?ml:sizeof(mb)-1;
if(z){memcpy(mb,ms,z);mb[z]=0;if(isStrMeth(mb)){tk=mk2(TOK_STRMETH,ms,lx);return;}}
lx=s+1;tk=mk2(TOK_DOT,s,lx);break;
}
default:tk=mk2(TOK_UNK,s,lx);break;
}
}
static void initLx(const char*s){lx=s?s:"";tokStart=lx;freetk();adv();}
static Node*MN(NT t,char*v){
Node*n=malloc(sizeof(Node));
if(!n){fprintf(stderr,"out of memory\n");exit(1);}
n->t=t;n->v=v;n->n=0;n->cap=0;n->c=NULL;n->l=0;
return n;
}
static void ac(Node*p,Node*c){
if(p->n>=p->cap){
p->cap=p->cap?p->cap*2:4;
p->c=realloc(p->c,(size_t)p->cap*sizeof(Node*));
}
p->c[p->n++]=c;
}
static int isWordOp(const char*s){
return s&&(!strcmp(s,"in")||!strcmp(s,"l")||!strcmp(s,"m"));
}
static int isIdentStart(char c){return isalpha((unsigned char)c)||c=='_';}
static int isIdentChar(char c){return isalnum((unsigned char)c)||c=='_';}
static Node*PE(void);static Node*PP(void);static Node*PT(void);
static int isStrMeth(const char*s){return s&&(!strcmp(s,"U")||!strcmp(s,"L")||!strcmp(s,"S")||!strcmp(s,"T")||!strcmp(s,"C")||!strcmp(s,"R")||!strcmp(s,"SP")||!strcmp(s,"J")||!strcmp(s,"F"));}
static int barStartsComp(void){if(tk.t!=TOK_BINOP||strcmp(tk.v,"|"))return 0;const char*p=lx;while(*p==' '||*p=='\t')p++;if(!isalpha((unsigned char)*p)&&*p!='_')return 0;while(isalnum((unsigned char)*p)||*p=='_')p++;while(*p==' '||*p=='\t')p++;return p[0]=='<'&&p[1]=='-';}
void ft(Node*n);
static Node*PP(void){
if(tk.t==TOK_BINOP&&tk.v[1]==0&&(tk.v[0]=='-'||tk.v[0]=='+'||tk.v[0]=='!')){char*op=strdup(tk.v);adv();Node*o=PP();Node*b=MN(ND_BINOP,op);ac(b,o);return b;}
switch(tk.t){
case TOK_NUM:{Node*n=MN(ND_NUM,strdup(tk.v));adv();return n;}
case TOK_STR:{Node*n=MN(ND_STR,strdup(tk.v));adv();return n;}
case TOK_FMTSTR:{Node*n=MN(ND_FMT,strdup(tk.v));adv();return n;}
case TOK_AT:{
Node*n=MN(ND_TRYEXPR,strdup(""));adv();
if(tk.t!=TOK_LPAREN){perr("@ try-expression expects '(expr, default)'");return n;}
adv();if(tk.t==TOK_RPAREN){perr("@ try-expression expects two arguments");adv();return n;}
ac(n,PE());if(tk.t!=TOK_COMMA){perr("@ try-expression expects two arguments");return n;}
adv();ac(n,PE());if(tk.t!=TOK_RPAREN)perr("expected ')'");else adv();return n;
}
case TOK_IDENT:{
Node*n=MN(ND_IDENT,strdup(tk.v));adv();
if(tk.t==TOK_LPAREN){
adv();Node*c=MN(ND_CALL,strdup(n->v));ft(n);
if(tk.t!=TOK_RPAREN){ac(c,PE());while(tk.t==TOK_COMMA){adv();ac(c,PE());}}
if(tk.t!=TOK_RPAREN)perr("expected ')'");else adv();return c;
}
if(!strcmp(n->v,"o")&&tk.t==TOK_STR){Node*c=MN(ND_CALL,strdup("o"));ac(c,MN(ND_STR,strdup(tk.v)));adv();ft(n);return c;}
return n;
}
case TOK_DEREF:{
adv();if(tk.t!=TOK_IDENT){perr("expected identifier after $");return MN(ND_IDENT,strdup(""));}
size_t ln=strlen(tk.v)+2;char*dv=malloc(ln);snprintf(dv,ln,"$%s",tk.v);adv();
if(tk.t==TOK_LPAREN){adv();Node*c=MN(ND_CALL,dv);if(tk.t!=TOK_RPAREN){ac(c,PE());while(tk.t==TOK_COMMA){adv();ac(c,PE());}}if(tk.t!=TOK_RPAREN)perr("expected ')'");else adv();return c;}
return MN(ND_IDENT,dv);
}
case TOK_LPAREN:{adv();Node*n=PE();if(tk.t!=TOK_RPAREN)perr("expected ')'");else adv();return n;}
case TOK_LBRACK:{
adv();Node*n=MN(ND_LIST,strdup(""));if(tk.t==TOK_RBRACK){adv();return n;}
Node*first=PE();
if(tk.t==TOK_BINOP&&!strcmp(tk.v,"|")&&barStartsComp()){
adv();if(tk.t!=TOK_IDENT){perr("list comprehension expects a loop variable");return n;}
Node*var=MN(ND_IDENT,strdup(tk.v));adv();
if(tk.t!=TOK_BINOP||strcmp(tk.v,"<-")){perr("list comprehension expects '<-'");return n;}
adv();Node*iter=PE();if(tk.t!=TOK_RBRACK)perr("expected ']'");else adv();
Node*c=MN(ND_LISTCOMP,strdup(""));ac(c,first);ac(c,var);ac(c,iter);free(n->c);free(n->v);free(n);return c;
}
ac(n,first);while(tk.t==TOK_COMMA){adv();ac(n,PE());}
if(tk.t!=TOK_RBRACK)perr("expected ']'");else adv();return n;
}
case TOK_LBRACE:{
adv();if(tk.t==TOK_RBRACE){adv();return MN(ND_DICT,strdup(""));}
Node*first=PE();
if(tk.t==TOK_COLON){
adv();Node*val=PE();
if(tk.t==TOK_BINOP&&!strcmp(tk.v,"|")&&barStartsComp()){
adv();if(tk.t!=TOK_IDENT){perr("dict comprehension expects a loop variable");return val;}
Node*var=MN(ND_IDENT,strdup(tk.v));adv();
if(tk.t!=TOK_BINOP||strcmp(tk.v,"<-")){perr("dict comprehension expects '<-'");return val;}
adv();Node*iter=PE();if(tk.t!=TOK_RBRACE)perr("expected '}'");else adv();
Node*c=MN(ND_DICTCOMP,strdup(""));ac(c,first);ac(c,val);ac(c,var);ac(c,iter);return c;
}
Node*n=MN(ND_DICT,strdup(""));ac(n,first);ac(n,val);while(tk.t==TOK_COMMA){adv();Node*k=PE();if(tk.t!=TOK_COLON)perr("expected ':' in dict literal");else adv();ac(n,k);ac(n,PE());}if(tk.t!=TOK_RBRACE)perr("expected '}'");else adv();return n;
}
Node*n=MN(ND_SET,strdup(""));ac(n,first);while(tk.t==TOK_COMMA){adv();ac(n,PE());}if(tk.t!=TOK_RBRACE)perr("expected '}'");else adv();return n;
}
default:{char msg[320];snprintf(msg,sizeof(msg),"unexpected token '%s'",tk.v?tk.v:"");perr(msg);adv();return MN(ND_NUM,strdup("0"));}
}
}
static Node*PT(void){
Node*n=PP();
for(;;){
if(tk.t==TOK_LBRACK){
adv();Node*start=NULL,*end=NULL;int hs=0,he=0;
if(tk.t!=TOK_COLON&&tk.t!=TOK_RBRACK){start=PE();hs=1;}
if(tk.t==TOK_COLON){
adv();if(tk.t!=TOK_RBRACK){end=PE();he=1;}
if(tk.t!=TOK_RBRACK)perr("expected ']'");else adv();
Node*sl=MN(ND_SLICE,strdup(hs?(he?"11":"10"):(he?"01":"00")));ac(sl,n);if(hs)ac(sl,start);if(he)ac(sl,end);n=sl;continue;
}
if(tk.t!=TOK_RBRACK)perr("expected ']'");else adv();Node*ix=MN(ND_INDEX,strdup(""));ac(ix,n);ac(ix,start);n=ix;continue;
}
if(tk.t==TOK_DOT){
adv();if(tk.t==TOK_STRMETH){Node*m=MN(ND_STRMETH,strdup(tk.v));adv();ac(m,n);if(tk.t==TOK_LPAREN){adv();if(tk.t!=TOK_RPAREN){ac(m,PE());while(tk.t==TOK_COMMA){adv();ac(m,PE());}}if(tk.t!=TOK_RPAREN)perr("expected ')'");else adv();}n=m;continue;}
if(tk.t!=TOK_IDENT){perr("expected identifier after '.'");break;}
size_t l=strlen(n->v)+strlen(tk.v)+2;char*f=malloc(l);snprintf(f,l,"%s.%s",n->v,tk.v);free(n->v);n->v=f;adv();continue;
}
if(tk.t==TOK_STRMETH){Node*m=MN(ND_STRMETH,strdup(tk.v));adv();ac(m,n);if(tk.t==TOK_LPAREN){adv();if(tk.t!=TOK_RPAREN){ac(m,PE());while(tk.t==TOK_COMMA){adv();ac(m,PE());}}if(tk.t!=TOK_RPAREN)perr("expected ')'");else adv();}n=m;continue;}
if(tk.t==TOK_LPAREN){adv();Node*c=MN(ND_CALL,n->v?strdup(n->v):strdup(""));ft(n);if(tk.t!=TOK_RPAREN){ac(c,PE());while(tk.t==TOK_COMMA){adv();ac(c,PE());}}if(tk.t!=TOK_RPAREN)perr("expected ')'");else adv();n=c;continue;}
break;
}
return n;
}
static Node*PU(void){if(tk.t==TOK_BINOP&&tk.v[1]==0&&(tk.v[0]=='-'||tk.v[0]=='+'||tk.v[0]=='!')){char*op=strdup(tk.v);adv();Node*rhs=PU();Node*b=MN(ND_BINOP,op);ac(b,rhs);return b;}return PT();}
static Node*PM(void){Node*n=PU();while(tk.t==TOK_BINOP&&tk.v[1]==0&&(tk.v[0]=='*'||tk.v[0]=='/'||tk.v[0]=='%')){char*op=strdup(tk.v);adv();Node*r=PU();Node*b=MN(ND_BINOP,op);ac(b,n);ac(b,r);n=b;}return n;}
static Node*PA(void){Node*n=PM();while(tk.t==TOK_BINOP&&tk.v[1]==0&&(tk.v[0]=='+'||tk.v[0]=='-')){char*op=strdup(tk.v);adv();Node*r=PM();Node*b=MN(ND_BINOP,op);ac(b,n);ac(b,r);n=b;};return n;}
static int isCmpOpToken(void){if(tk.t==TOK_EQ)return 1;if(tk.t!=TOK_BINOP)return 0;return!strcmp(tk.v,"<")||!strcmp(tk.v,">")||!strcmp(tk.v,"<=")||!strcmp(tk.v,">=")||!strcmp(tk.v,"==")||!strcmp(tk.v,"!=");}
static Node*PC(void){Node*n=PA();while(isCmpOpToken()||(tk.t==TOK_IDENT&&isWordOp(tk.v))){char*op=strdup(tk.v);adv();Node*r=PA();Node*b=MN(ND_BINOP,op);ac(b,n);ac(b,r);n=b;}return n;}
static Node*PAND(void){Node*n=PC();while(tk.t==TOK_BINOP&&(!strcmp(tk.v,"&&")||!strcmp(tk.v,"&"))){char*op=strdup(tk.v);adv();Node*r=PC();Node*b=MN(ND_BINOP,op);ac(b,n);ac(b,r);n=b;}return n;}
static Node*PE(void){Node*n=PAND();while(tk.t==TOK_BINOP&&(!strcmp(tk.v,"||")||(!strcmp(tk.v,"|")&&!barStartsComp()))){char*op=strdup(tk.v);adv();Node*r=PAND();Node*b=MN(ND_BINOP,op);ac(b,n);ac(b,r);n=b;}if(tk.t==TOK_QUESTION){adv();Node*y=PE();if(tk.t!=TOK_COLON)perr("ternary operator expects ':'");else adv();Node*z=PE();Node*q=MN(ND_TERNARY,strdup(""));ac(q,n);ac(q,y);ac(q,z);n=q;}return n;}
static const char*afterParsedStatement(void){
if(tk.t!=TOK_EOF&&tk.t!=TOK_EOL&&tk.t!=TOK_SEMI&&tk.t!=TOK_COLON){
char msg[320];snprintf(msg,sizeof(msg),"unexpected token '%s'",tk.v?tk.v:"");perr(msg);
while(tk.t!=TOK_EOF&&tk.t!=TOK_EOL&&tk.t!=TOK_SEMI&&tk.t!=TOK_COLON)adv();
}
return(tk.t==TOK_EOF||tk.t==TOK_EOL||tk.t==TOK_SEMI||tk.t==TOK_COLON)?tokStart:lx;
}
static Node*quotedNodeAt(const char*p,const char**next){
if(!p||!*p)return NULL;
char q=*p;
if(q!='\''&&q!='"'&&q!='`')return NULL;
int triple=(p[0]==q&&p[1]==q&&p[2]==q);
p+=triple?3:1;
const char*s=p;
while(*p){
if(*p=='\\'&&p[1]){p+=2;continue;}
if(triple){
if(p[0]==q&&p[1]==q&&p[2]==q){
Node*n=MN(ND_STR,strndup(s,(size_t)(p-s)));
p+=3;
if(next){*next=p;}
return n;
}
}else if(*p==q){
Node*n=MN(ND_STR,strndup(s,(size_t)(p-s)));
p++;
if(next){*next=p;}
return n;
}
p++;
}
Node*n=MN(ND_STR,strndup(s,(size_t)(p-s)));
perr_at(s?s:p,"unterminated string");
if(next){*next=p;}
return n;
}
static Node*parseCompactIf(const char*rest,const char**next){
if(!rest||!isIdentStart(rest[0]))return NULL;
if((rest[1]!='l'&&rest[1]!='m')||!rest[2])return NULL;
const char*p=rest+2;Node*right=NULL;
if(isIdentStart(*p)){
const char*s=p;while(isIdentChar(*p))p++;right=MN(ND_IDENT,strndup(s,(size_t)(p-s)));
}else if(*p=='\''||*p=='"'||*p=='`'){
right=quotedNodeAt(p,&p);
if(parse_error)return NULL;
}else if(*p=='+'||*p=='-'||isdigit((unsigned char)*p)||(*p=='.'&&isdigit((unsigned char)p[1]))){
const char*s=p;int dots=0;int digits=0;
if(*p=='+'||*p=='-')p++;
while(isdigit((unsigned char)*p)||*p=='.'){if(isdigit((unsigned char)*p))digits++;else dots++;p++;}
if(*p=='e'||*p=='E'){
p++;if(*p=='+'||*p=='-')p++;if(!isdigit((unsigned char)*p))return NULL;while(isdigit((unsigned char)*p))p++;
}
if(!digits||dots>1)return NULL;
right=MN(ND_NUM,strndup(s,(size_t)(p-s)));
}else return NULL;
if(*p&&*p!=' '&&*p!='\t'&&*p!='\n'&&*p!='\r'&&*p!=';'&&*p!=':'&&*p!='#'){
ft(right);
perr("compact IF requires a statement separator after the right operand");
return NULL;
}
Node*left=MN(ND_IDENT,strndup(rest,1));char opbuf[2]={rest[1],0};Node*b=MN(ND_BINOP,strdup(opbuf));ac(b,left);ac(b,right);Node*n=MN(ND_IF,strdup(""));ac(n,b);if(next){*next=p;}return n;
}
static Node*rawCommandNode(const char*rest,const char**next){
const char*p=rest;while(*p==' '||*p=='\t')p++;
if(*p=='\''||*p=='"'||*p=='`'){
Node*n=quotedNodeAt(p,&p);
if(parse_error){if(next){*next=p;}return n;}
if(*p&&*p!='\n'&&*p!='\r'&&*p!=';'&&*p!=':'&&*p!='#'){
while(*p&&*p!='\n'&&*p!='\r'&&*p!=';'&&*p!=':')p++;
perr("unexpected text after command string");
}
if(next){*next=p;}return n;
}
const char*st=p;int quoted=0;char q=0;
while(*p){
if(!quoted&&(*p==';'||*p==':'||*p=='\n'||*p=='\r'||*p=='#'))break;
if(*p=='\''||*p=='"'||*p=='`'){
if(!quoted){quoted=1;q=*p;}else if(q==*p&&!(p>st&&p[-1]=='\\'))quoted=0;
}
p++;
}
while(p>st&&(p[-1]==' '||p[-1]=='\t'))p--;
Node*n=MN(ND_STR,strndup(st,(size_t)(p-st)));if(next){*next=p;}return n;
}
static Node*parseStmtAt(const char*line,const char**next){
if(next)*next=line;if(!line||!*line)return MN(ND_IDENT,strdup(""));
const char*raw=line;while(*raw==' '||*raw=='\t'||*raw=='\n'||*raw=='\r')raw++;if(!*raw){if(next)*next=raw;return MN(ND_IDENT,strdup(""));}
char cmd=*raw;const char*rest=raw+1;while(*rest==' '||*rest=='\t')rest++;
if(cmd=='#'){while(*rest&&*rest!='\n'&&*rest!='\r')rest++;if(next)*next=rest;return MN(ND_IDENT,strdup(""));}
if(cmd==';'){if(next)*next=raw+1;return MN(ND_ENDIF,strdup(""));}
if(cmd==':'&&*rest=='?'){initLx(rest+1);Node*n=MN(ND_ELIF,strdup(""));Node*e=PE();if(e)ac(n,e);if(next)*next=afterParsedStatement();return n;}
if(cmd=='?'){const char*cn=NULL;Node*n=parseCompactIf(rest,&cn);if(n){if(next)*next=cn;return n;}initLx(rest);n=MN(ND_IF,strdup(""));Node*e=PE();if(e)ac(n,e);if(next)*next=afterParsedStatement();return n;}
if(cmd=='!'){initLx(rest);Node*n=MN(ND_CEXEC,strdup(""));Node*e=rawCommandNode(rest,next);if(e)ac(n,e);return n;}
if(cmd=='o'||islower((unsigned char)cmd)||cmd=='_'||cmd=='\''||cmd=='"'||cmd=='`'||cmd=='$'||isdigit((unsigned char)cmd)||cmd=='('||cmd=='['||cmd=='{')initLx(raw);else initLx(rest);
if(cmd==':'||cmd=='&'||cmd=='~'||cmd=='Q'||cmd=='N'||cmd=='Y'){
if(next)*next=raw+1;switch(cmd){case':':return MN(ND_ELSE,strdup(""));case'&':return MN(ND_WEND,strdup(""));case'~':return MN(ND_ENDFN,strdup(""));case'Q':return MN(ND_QUIT,strdup(""));case'N':return MN(ND_DEF,strdup(""));default:return MN(ND_ENDTRY,strdup(""));}
}
switch(cmd){
case'G':{Node*n=MN(ND_PRINTNL,strdup(""));Node*e=PE();if(e)ac(n,e);if(next)*next=afterParsedStatement();return n;}
case'P':{Node*n=MN(ND_PRINT,strdup(""));Node*e=PE();if(e)ac(n,e);if(next)*next=afterParsedStatement();return n;}
case'X':{Node*n=MN(ND_EXEC,strdup(""));Node*e=rawCommandNode(rest,next);if(e)ac(n,e);return n;}
case'E':{
if(tk.t==TOK_IDENT){const char*save=tokStart;char*first=strdup(tk.v);adv();if(tk.t==TOK_COMMA){adv();if(tk.t==TOK_IDENT){char*second=strdup(tk.v);adv();Node*n=MN(ND_ENUMERATE,strdup(""));ac(n,MN(ND_IDENT,strdup(first)));ac(n,MN(ND_IDENT,strdup(second)));free(first);free(second);if(tk.t!=TOK_EOF&&tk.t!=TOK_EOL&&tk.t!=TOK_SEMI){Node*e=PE();if(e)ac(n,e);}else perr("E: expected list expression");if(next)*next=afterParsedStatement();return n;}free(first);perr("E: expected item variable after ','");if(next)*next=afterParsedStatement();return MN(ND_ERROR,strdup(""));}free(first);initLx(save);}
if(tk.t==TOK_STR){Node*n=MN(ND_ERROR,strdup(""));ac(n,MN(ND_STR,strdup(tk.v)));adv();if(next)*next=afterParsedStatement();return n;}
if(tk.t==TOK_IDENT){Node*n=MN(ND_ASSIGN,strdup(""));ac(n,MN(ND_IDENT,strdup(tk.v)));adv();if(tk.t==TOK_EQ)adv();if(tk.t!=TOK_EOF&&tk.t!=TOK_EOL&&tk.t!=TOK_SEMI){Node*e=PE();if(e)ac(n,e);}if(next)*next=afterParsedStatement();return n;}
perr("E: expected assignment, error message, or enumerate");if(next)*next=afterParsedStatement();return MN(ND_ERROR,strdup(""));
}
case'D':{if(tk.t==TOK_STR){Node*n=MN(ND_DEL,strdup(tk.v));adv();if(next)*next=afterParsedStatement();return n;}Node*n=MN(ND_DELAY,strdup(""));Node*e=PE();if(e)ac(n,e);if(next)*next=afterParsedStatement();return n;}
case'K':{Node*n=MN(ND_INPUT,strdup(""));while(tk.t==TOK_IDENT||tk.t==TOK_STR){ac(n,tk.t==TOK_IDENT?MN(ND_IDENT,strdup(tk.v)):MN(ND_STR,strdup(tk.v)));adv();if(n->n>=3)break;}if(next)*next=afterParsedStatement();return n;}
case'A':{Node*n=MN(ND_ARITH,strdup("+"));if(tk.t==TOK_IDENT||tk.t==TOK_STR){ac(n,tk.t==TOK_STR?MN(ND_STR,strdup(tk.v)):MN(ND_IDENT,strdup(tk.v)));adv();}else perr("A: expected variable name");if(tk.t==TOK_BINOP&&tk.v[1]==0&&strchr("+-*/%",tk.v[0])){free(n->v);n->v=strdup(tk.v);adv();}else perr("A: expected arithmetic operator");if(tk.t!=TOK_EOF&&tk.t!=TOK_EOL&&tk.t!=TOK_SEMI){Node*e=PE();if(e)ac(n,e);}if(next)*next=afterParsedStatement();return n;}
case'@':{
if(tk.t==TOK_LPAREN){
Node*n=MN(ND_TRYEXPR,strdup(""));
adv();
if(tk.t==TOK_RPAREN){perr("@ try-expression expects two arguments");if(next)*next=afterParsedStatement();return n;}
ac(n,PE());
if(tk.t!=TOK_COMMA){perr("@ try-expression expects two arguments");if(next)*next=afterParsedStatement();return n;}
adv();
ac(n,PE());
if(tk.t!=TOK_RPAREN)perr("expected ')'");else adv();
if(next)*next=afterParsedStatement();
return n;
}
Node*n=MN(ND_WHILE,strdup(""));
Node*e=PE();
if(e)ac(n,e);
if(next)*next=afterParsedStatement();
return n;
}
case'M':{if(tk.t!=TOK_IDENT){perr("M: expected function name");if(next)*next=afterParsedStatement();return MN(ND_MAP,strdup(""));}Node*n=MN(ND_MAP,strdup(tk.v));adv();if(tk.t==TOK_EOF||tk.t==TOK_EOL||tk.t==TOK_SEMI)perr("M: expected list expression");else{Node*e=PE();if(e)ac(n,e);}if(next)*next=afterParsedStatement();return n;}
case'T':{if(tk.t==TOK_EOF||tk.t==TOK_EOL||tk.t==TOK_SEMI||tk.t==TOK_COLON){if(next)*next=afterParsedStatement();return MN(ND_TRY,strdup(""));}if(tk.t!=TOK_IDENT){perr("T: expected function name or bare TRY");if(next)*next=afterParsedStatement();return MN(ND_TRY,strdup(""));}Node*n=MN(ND_FILTER,strdup(tk.v));adv();if(tk.t==TOK_EOF||tk.t==TOK_EOL||tk.t==TOK_SEMI)perr("T: expected list expression");else{Node*e=PE();if(e)ac(n,e);}if(next)*next=afterParsedStatement();return n;}
case'F':{
if(tk.t!=TOK_IDENT){perr("F: expected function name");if(next)*next=afterParsedStatement();return MN(ND_FUNC,strdup(""));}
Node*n=MN(ND_FUNC,strdup(tk.v));adv();
if(tk.t==TOK_LPAREN){
adv();
int optionalSeen=0;
while(tk.t!=TOK_RPAREN&&tk.t!=TOK_EOF&&tk.t!=TOK_EOL&&tk.t!=TOK_SEMI){
int optional=0;
if(tk.t==TOK_BINOP&&tk.v[0]=='!'&&tk.v[1]==0){
optional=1;adv();
if(tk.t!=TOK_IDENT){perr("F: expected parameter name after '!'");break;}
}else if(tk.t!=TOK_IDENT){
perr("F: expected parameter name");break;
}
if(optionalSeen&&!optional){
perr("F: required parameter cannot follow optional parameter");
break;
}
if(optional)optionalSeen=1;
if(optional){
size_t z=strlen(tk.v)+2;char*pv=malloc(z);snprintf(pv,z,"!%s",tk.v);ac(n,MN(ND_IDENT,pv));
}else ac(n,MN(ND_IDENT,strdup(tk.v)));
adv();
if(tk.t==TOK_COMMA){adv();continue;}
break;
}
if(tk.t==TOK_RPAREN)adv();else if(!parse_error)perr("F: expected ')' after parameter list");
}
if(next)*next=afterParsedStatement();
return n;
}
case'R':{if(tk.t==TOK_IDENT&&!strcmp(tk.v,"file")){adv();Node*n=MN(ND_READ,strdup(""));if(tk.t!=TOK_STR)perr("R file: expected path string");else{ac(n,MN(ND_STR,strdup(tk.v)));adv();}if(next)*next=afterParsedStatement();return n;}if(tk.t==TOK_IDENT){const char*save=tokStart;char*name=strdup(tk.v);adv();if(tk.t==TOK_STR){Node*n=MN(ND_READ,name);ac(n,MN(ND_STR,strdup(tk.v)));adv();if(next)*next=afterParsedStatement();return n;}free(name);initLx(save);}Node*n=MN(ND_RETURN,strdup(""));initLx(rest);if(tk.t!=TOK_EOF&&tk.t!=TOK_EOL&&tk.t!=TOK_SEMI){Node*e=PE();if(e)ac(n,e);}if(next)*next=afterParsedStatement();return n;}
case'L':{Node*n=MN(ND_LABEL,strdup(""));if(tk.t==TOK_IDENT){free(n->v);n->v=strdup(tk.v);adv();}else perr("L: expected label name");if(next)*next=afterParsedStatement();return n;}
case'J':{Node*n=MN(ND_JUMP,strdup(""));if(tk.t==TOK_IDENT){free(n->v);n->v=strdup(tk.v);adv();}else perr("J: expected label name");if(next)*next=afterParsedStatement();return n;}
case'W':{if(tk.t==TOK_IDENT&&!strcmp(tk.v,"file")){adv();Node*n=MN(ND_WRITE,strdup(""));if(tk.t!=TOK_STR)perr("W file: expected path string");else{ac(n,MN(ND_STR,strdup(tk.v)));adv();}if(tk.t!=TOK_STR)perr("W file: expected content string");else{ac(n,MN(ND_STR,strdup(tk.v)));adv();}if(next)*next=afterParsedStatement();return n;}Node*n=MN(ND_SWITCH,strdup(""));if(tk.t==TOK_EOF||tk.t==TOK_EOL||tk.t==TOK_SEMI)perr("W: expected switch expression");else{Node*e=PE();if(e)ac(n,e);}if(next)*next=afterParsedStatement();return n;}
case'V':{Node*n=MN(ND_CASE,strdup(""));Node*e=PE();if(e)ac(n,e);if(next)*next=afterParsedStatement();return n;}
case'C':{Node*n=MN(ND_CATCH,strdup(""));if(tk.t==TOK_IDENT||tk.t==TOK_STR){free(n->v);n->v=strdup(tk.v);adv();}if(next)*next=afterParsedStatement();return n;}
case'Z':{if(tk.t==TOK_EOF||tk.t==TOK_EOL||tk.t==TOK_SEMI){if(next)*next=afterParsedStatement();return MN(ND_ENDSW,strdup(""));}Node*n=MN(ND_ZIP,strdup(""));Node*e=PE();if(e)ac(n,e);if(tk.t!=TOK_EOF&&tk.t!=TOK_EOL&&tk.t!=TOK_SEMI&&tk.t!=TOK_COLON){e=PE();if(e)ac(n,e);}if(next)*next=afterParsedStatement();return n;}
case'I':{Node*n=MN(ND_INSPECT,strdup(""));if(tk.t!=TOK_EOF&&tk.t!=TOK_EOL&&tk.t!=TOK_SEMI){Node*e=PE();if(e)ac(n,e);}if(next)*next=afterParsedStatement();return n;}
case'O':{Node*n=MN(ND_FOR,strdup(""));if(tk.t!=TOK_IDENT){perr("O: expected loop variable");}else{ac(n,MN(ND_IDENT,strdup(tk.v)));adv();for(int i=0;i<2;i++){if(tk.t==TOK_EOF||tk.t==TOK_EOL||tk.t==TOK_SEMI||tk.t==TOK_COLON){perr("O: expected range expression");break;}ac(n,PT());}if(tk.t!=TOK_EOF&&tk.t!=TOK_EOL&&tk.t!=TOK_SEMI&&tk.t!=TOK_COLON)ac(n,PT());}if(next)*next=afterParsedStatement();return n;}
case'U':{Node*n=MN(ND_IMPORT,strdup(""));if(tk.t==TOK_STR||tk.t==TOK_IDENT){free(n->v);n->v=strdup(tk.v);adv();}else perr("U: expected module name");if(next)*next=afterParsedStatement();return n;}
case'S':{Node*n=MN(ND_STRREP,strdup(""));if(tk.t==TOK_IDENT||tk.t==TOK_STR){ac(n,tk.t==TOK_IDENT?MN(ND_IDENT,strdup(tk.v)):MN(ND_STR,strdup(tk.v)));adv();}for(int i=0;i<2;i++){if(tk.t==TOK_STR){ac(n,MN(ND_STR,strdup(tk.v)));adv();}else perr("S: expected string argument");}if(next)*next=afterParsedStatement();return n;}
default:break;
}
if(tk.t==TOK_IDENT){const char*save=tokStart;char namebuf[256];snprintf(namebuf,sizeof(namebuf),"%s",tk.v);adv();
if(tk.t==TOK_EQ||(tk.t==TOK_BINOP&&(!strcmp(tk.v,"+=")||!strcmp(tk.v,"-=")||!strcmp(tk.v,"*=")||!strcmp(tk.v,"/=")||!strcmp(tk.v,"%=")))){
if(tk.t==TOK_EQ){Node*n=MN(ND_ASSIGN,strdup(""));ac(n,MN(ND_IDENT,strdup(namebuf)));adv();if(tk.t!=TOK_EOF&&tk.t!=TOK_EOL&&tk.t!=TOK_SEMI){Node*e=PE();if(e)ac(n,e);}if(next)*next=afterParsedStatement();return n;}
char op[2]={tk.v[0],0};Node*n=MN(ND_ARITH,strdup(op));ac(n,MN(ND_IDENT,strdup(namebuf)));adv();if(tk.t!=TOK_EOF&&tk.t!=TOK_EOL&&tk.t!=TOK_SEMI){Node*e=PE();if(e)ac(n,e);}if(next)*next=afterParsedStatement();return n;
}
if(tk.t==TOK_LBRACK){lx=save;tokStart=save;adv();Node*target=PT();if(tk.t==TOK_EQ&&target->t==ND_INDEX){adv();Node*val=PE();Node*n=MN(ND_IDXASSIGN,strdup(""));ac(n,target->c[0]);ac(n,target->c[1]);ac(n,val);free(target->c);free(target->v);free(target);if(next)*next=afterParsedStatement();return n;}if(next)*next=afterParsedStatement();return target;}
lx=save;tokStart=save;adv();
}
Node*n=PE();if(next)*next=afterParsedStatement();return n;
}
Node*parseStmt(const char*line){
const char*next=NULL;
return parseStmtAt(line,&next);
}
struct IfFrameTmp{int body;int hasElse;};
static int countNewlines(const char*a,const char*b){
int n=0;
for(const char*p=a;p&&p<b;p++)if(*p=='\n')n++;
return n;
}
static const char*skipTrivia(const char*p){
for(;;){
while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;
if(*p=='#'){while(*p&&*p!='\n'&&*p!='\r')p++;continue;}
return p;
}
}
Node*parse(const char*src){
parse_error=0;parse_error_msg[0]=0;free(parser_source_owned);parser_source_owned=src?strdup(src):NULL;parser_source=parser_source_owned;parser_error_pos=NULL;parser_error_line=0;parser_error_col=0;
Node*p=MN(ND_PROG,strdup(""));
if(!src||!*src)return p;
const char*q=parser_source;
int line=1;
struct IfFrameTmp*ifs=NULL;
int ifn=0,ifcap=0;
while(*q){
while(*q==' '||*q=='\t'||*q=='\n'||*q=='\r'){
if(*q=='\n')line++;
q++;
}
if(!*q)break;
const char*next=NULL;
Node*n=parseStmtAt(q,&next);
if(!next||next<=q)next=q+1;
if(n&&n->t==ND_ENDIF&&*q==';'){
const char*afterSemi=skipTrivia(next);
int branchFollows=0;
if(afterSemi[0]==':'&&afterSemi[1]=='?')branchFollows=1;
else if(afterSemi[0]==':')branchFollows=1;
if(ifn>0&&ifs[ifn-1].body&&!branchFollows){
n->l=line;
ac(p,n);
ifn--;
if(ifn==0){free(ifs);ifs=NULL;ifcap=0;}
}else if(ifn>0){
ft(n);
}else{
ft(n);
perr("ENDIF without IF");
}
q=next;
continue;
}
if(n&&n->t!=ND_IDENT){
n->l=line;
int dummyLine=0;errorLocation(q,&dummyLine,&n->col);
ac(p,n);
if(n->t==ND_IF){
if(n->v&&n->v[0]){
if(ifn>0)ifs[ifn-1].body=1;
}else{
if(ifn>0)ifs[ifn-1].body=1;
if(ifn>=ifcap){
ifcap=ifcap?ifcap*2:8;
ifs=realloc(ifs,(size_t)ifcap*sizeof(*ifs));
}
ifs[ifn].body=0;
ifs[ifn].hasElse=0;
ifn++;
}
}else if(n->t==ND_ELSE||n->t==ND_ELIF){
if(ifn>0){
ifs[ifn-1].hasElse=1;
ifs[ifn-1].body=0;
}else{
perr_node(n,"ELSE/ELIF without IF");
}
}else if(n->t==ND_ENDIF){
if(ifn>0)ifn--;
}else if(ifn>0){
ifs[ifn-1].body=1;
}
}else if(n){
ft(n);
}
line+=countNewlines(q,next);
q=next;
}
free(ifs);
{
NT*stack=NULL;
int sn=0,scap=0;
#define VPUSH(x) do{if(sn>=scap){scap=scap?scap*2:16;stack=realloc(stack,(size_t)scap*sizeof(*stack));}stack[sn++]=(x);}while(0)
#define VPOP() do{if(sn>0)sn--;}while(0)
for(int i=0;i<p->n;i++){
Node*nd=p->c[i];
switch(nd->t){
case ND_IF:if(nd->v[0])break;VPUSH(ND_IF);break;
case ND_ELSE:case ND_ELIF:if(sn==0||stack[sn-1]!=ND_IF)perr_node(nd,"ELSE/ELIF without IF");break;
case ND_ENDIF:if(sn==0||stack[sn-1]!=ND_IF)perr_node(nd,"ENDIF without IF");else VPOP();break;
case ND_WHILE:case ND_FOR:case ND_ENUMERATE:VPUSH(nd->t);break;
case ND_WEND:if(sn==0||(stack[sn-1]!=ND_WHILE&&stack[sn-1]!=ND_FOR&&stack[sn-1]!=ND_ENUMERATE))perr_node(nd,"WEND without WHILE/FOR/ENUMERATE");else VPOP();break;
case ND_SWITCH:VPUSH(ND_SWITCH);break;
case ND_CASE:case ND_DEF:if(sn==0||stack[sn-1]!=ND_SWITCH)perr_node(nd,"CASE/DEFAULT outside SWITCH");break;
case ND_ENDSW:if(sn==0||stack[sn-1]!=ND_SWITCH)perr_node(nd,"ENDSWITCH without SWITCH");else VPOP();break;
case ND_TRY:VPUSH(ND_TRY);break;
case ND_CATCH:if(sn==0||stack[sn-1]!=ND_TRY)perr_node(nd,"CATCH without TRY");break;
case ND_ENDTRY:if(sn==0||stack[sn-1]!=ND_TRY)perr_node(nd,"ENDTRY without TRY");else VPOP();break;
case ND_FUNC:VPUSH(ND_FUNC);break;
case ND_RETURN:{
int found=0;
for(int j=sn-1;j>=0;j--)if(stack[j]==ND_FUNC){found=1;break;}
if(!found)perr_node(nd,"RETURN outside function");
break;
}
case ND_ENDFN:if(sn==0||stack[sn-1]!=ND_FUNC)perr_node(nd,"ENDFN without FUNCTION");else VPOP();break;
default:break;
}
}
if(sn>0){
NT top=stack[sn-1];
int opener=-1;
for(int j=p->n-1;j>=0;j--){
NT t=p->c[j]->t;
int match=(top==ND_IF&&t==ND_IF)||(top==ND_WHILE&&t==ND_WHILE)||(top==ND_FOR&&t==ND_FOR)||(top==ND_ENUMERATE&&t==ND_ENUMERATE)||(top==ND_SWITCH&&t==ND_SWITCH)||(top==ND_TRY&&t==ND_TRY)||(top==ND_FUNC&&t==ND_FUNC);
if(match){opener=j;break;}
}
const char*msg=top==ND_IF?"unterminated IF block":top==ND_WHILE?"unterminated WHILE block":top==ND_FOR?"unterminated FOR block":top==ND_ENUMERATE?"unterminated ENUMERATE block":top==ND_SWITCH?"unterminated SWITCH block":top==ND_TRY?"unterminated TRY block":"unterminated FUNCTION block";
if(opener>=0)perr_node(p->c[opener],msg);else perr(msg);
}
free(stack);
#undef VPUSH
#undef VPOP
}
freetk();
return p;
}
const char*nn(NT t){switch(t){case ND_PROG:return"PROGRAM";case ND_NUM:return"NUM";case ND_STR:return"STR";case ND_IDENT:return"IDENT";case ND_BINOP:return"BINOP";case ND_PRINTNL:return"PRINT_NL";case ND_PRINT:return"PRINT";case ND_IF:return"IF";case ND_ELSE:return"ELSE";case ND_ENDIF:return"ENDIF";case ND_WHILE:return"WHILE";case ND_WEND:return"WEND";case ND_FOR:return"FOR";case ND_ASSIGN:return"ASSIGN";case ND_QUIT:return"QUIT";case ND_LABEL:return"LABEL";case ND_JUMP:return"JUMP";case ND_SWITCH:return"SWITCH";case ND_CASE:return"CASE";case ND_DEF:return"DEFAULT";case ND_ENDSW:return"ENDSWITCH";case ND_FUNC:return"FUNC";case ND_RETURN:return"RETURN";case ND_ENDFN:return"ENDFN";case ND_ERROR:return"ERROR";case ND_DELAY:return"DELAY";case ND_INPUT:return"INPUT";case ND_IMPORT:return"IMPORT";case ND_STRREP:return"STRREP";case ND_TRY:return"TRY";case ND_CATCH:return"CATCH";case ND_ENDTRY:return"ENDTRY";case ND_EXEC:return"EXEC";case ND_CEXEC:return"CEXEC";case ND_CLASS:return"CLASS";case ND_ARITH:return"ARITH";case ND_DEL:return"DEL";case ND_CALL:return"CALL";case ND_STRMETH:return"STRMETH";case ND_LIST:return"LIST";case ND_DICT:return"DICT";case ND_SET:return"SET";case ND_INDEX:return"INDEX";case ND_IDXASSIGN:return"IDXASSIGN";case ND_SLICE:return"SLICE";case ND_LISTCOMP:return"LISTCOMP";case ND_DICTCOMP:return"DICTCOMP";case ND_FMT:return"FMT";case ND_MAP:return"MAP";case ND_FILTER:return"FILTER";case ND_ENUMERATE:return"ENUMERATE";case ND_ZIP:return"ZIP";case ND_READ:return"READ";case ND_WRITE:return"WRITE";case ND_INSPECT:return"INSPECT";case ND_TERNARY:return"TERNARY";case ND_TRYEXPR:return"TRYEXPR";case ND_ELIF:return"ELIF";default:return"???";}}
void pt(Node*n,int d){if(!n)return;for(int i=0;i<d;i++)printf("  ");printf("%s",nn(n->t));if(n->v&&*n->v)printf(" [%s]",n->v);printf("\n");for(int i=0;i<n->n;i++)pt(n->c[i],d+1);}
void ft(Node*n){if(!n)return;for(int i=0;i<n->n;i++)ft(n->c[i]);free(n->c);free(n->v);free(n);}
void parser_show_location_source(const char*src,int line,int col){
if(!src||line<=0){fprintf(stderr,"  --> line %d, column %d\n",line,col);return;}
const char*p=src;int ln=1;while(*p&&ln<line){if(*p++=='\n')ln++;}
const char*ls=p;const char*le=p;while(*le&&*le!='\n'&&*le!='\r')le++;size_t len=(size_t)(le-ls);if(len>240)len=240;
fprintf(stderr,"  --> line %d, column %d\n",line,col);
fprintf(stderr,"   |\n%2d | %.*s\n   | ",line,(int)len,ls);
int maxcol=(int)len+1;if(col>maxcol)col=maxcol;
for(int i=1;i<col;i++)fputc(ls[i-1]=='\t'?'\t':' ',stderr);
fprintf(stderr,"^\n");
}
void parser_show_location(int line,int col){parser_show_location_source(parser_source,line,col);}
int parser_had_error(void){return parse_error;}
const char*parser_error(void){return parse_error_msg;}
