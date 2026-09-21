#define _POSIX_C_SOURCE 200809L
#ifndef M_PI
#define M_PI 3.141592653589793238462643383279502884
#endif
#ifndef M_E
#define M_E 2.718281828459045235360287471352662498
#endif
#include "lib/vm.h"
#include "parser.c"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#ifndef strndup
static char*strndup(const char*s,size_t n){size_t l=0;while(l<n&&s[l])l++;char*d=malloc(l+1);if(d){memcpy(d,s,l);d[l]=0;}return d;}
#endif
#ifndef getcwd
static char*g_win_getcwd(char*b,size_t n){return _getcwd(b,(int)n);}
#define getcwd g_win_getcwd
#endif
#ifndef getline
#include <sys/types.h>
static ssize_t g_win_getline(char**line,size_t*cap,FILE*fp){if(*line){free(*line);*line=NULL;}char c;*cap=0;while(fread(&c,1,1,fp)==1){char*t=realloc(*line,*cap+2);if(!t)return-1;*line=t;(*line)[(*cap)++]=c;if(c=='\n')break;}if(*cap==0&&feof(fp))return-1;(*line)[*cap]='\0';return(ssize_t)*cap;}
#define getline g_win_getline
#endif
#else
#include <unistd.h>
#include <sys/wait.h>
#endif
static int quiet_errors=0;
static Node*active_error_node=NULL;
static VM*active_error_vm=NULL;
static void verr(const char*fmt,...){
if(quiet_errors)return;
va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);
if(active_error_node&&active_error_node->l)parser_show_location_source(active_error_vm?active_error_vm->source:NULL,active_error_node->l,active_error_node->col>0?active_error_node->col:1);
}
#define VERR(...) do{verr(__VA_ARGS__);}while(0)
#define DA_INIT 8
static int shellStatus(int status){
#ifdef _WIN32
return status;
#else
if(status<0)return-1;
if(WIFEXITED(status))return WEXITSTATUS(status);
if(WIFSIGNALED(status))return 128+WTERMSIG(status);
return status;
#endif
}
static void*dg(void*d,int*cap,size_t esz){int nc=*cap?*cap*2:DA_INIT;void*nd=realloc(d,nc*esz);*cap=nc;return nd;}
static uint64_t hf(const char*s){uint64_t h=14695981039346656037ULL;while(*s){h^=(unsigned char)*s++;h*=1099511628211ULL;}return h;}
typedef struct{char*k;V v;int st;}VE;typedef struct{VE*e;int cap,len,tombs;}VM2;
static VM2*vmk2(void){VM2*m=calloc(1,sizeof(*m));m->cap=16;m->e=calloc(m->cap,sizeof(VE));return m;}
static void vmfree2(VM2*m){if(!m)return;for(int i=0;i<m->cap;i++)if(m->e[i].st==2){free(m->e[i].k);vfree(&m->e[i].v);}free(m->e);free(m);}
static void vres(VM2*m,int nc){VE*old=m->e;int oc=m->cap;m->e=calloc(nc,sizeof(VE));m->cap=nc;m->len=0;for(int i=0;i<oc;i++)if(old[i].st==2){int idx=hf(old[i].k)%nc;while(m->e[idx].st==2)idx=(idx+1)%nc;m->e[idx]=old[i];m->len++;}m->tombs=0;free(old);}
static V*vget2(VM2*m,const char*k){int idx=hf(k)%m->cap;while(m->e[idx].st){if(m->e[idx].st==2&&!strcmp(m->e[idx].k,k))return&m->e[idx].v;idx=(idx+1)%m->cap;}return NULL;}
static void vput2(VM2*m,const char*k,V v){if(m->len+m->tombs>=m->cap*3/4)vres(m,m->cap*2);int idx=hf(k)%m->cap;int tomb=-1;while(m->e[idx].st){if(m->e[idx].st==1&&tomb==-1)tomb=idx;else if(m->e[idx].st==2&&!strcmp(m->e[idx].k,k)){vfree(&m->e[idx].v);m->e[idx].v=v;return;}idx=(idx+1)%m->cap;}if(tomb!=-1){idx=tomb;m->tombs--;}m->e[idx].k=strdup(k);m->e[idx].v=v;m->e[idx].st=2;m->len++;}
static void vext(VM2*d,VM2*s){for(int i=0;i<s->cap;i++)if(s->e[i].st==2)vput2(d,s->e[i].k,vc(s->e[i].v));}
typedef struct{V*a;int len,cap,rc;}LST;
typedef struct{VM2*m;int rc;}DCT;
static LST*lstnew(int cap){LST*l=malloc(sizeof(LST));l->cap=cap>0?cap:4;l->a=malloc(l->cap*sizeof(V));l->len=0;l->rc=1;return l;}
static void lstpush(LST*l,V v){if(l->len>=l->cap){l->cap*=2;l->a=realloc(l->a,l->cap*sizeof(V));}l->a[l->len++]=v;}
static DCT*dctnew(void){DCT*d=malloc(sizeof(DCT));d->m=vmk2();d->rc=1;return d;}
typedef struct{char*k;int v;int st;}LE;typedef struct{LE*e;int cap,len,tombs;}LM2;
static LM2*lmk2(void){LM2*m=calloc(1,sizeof(*m));m->cap=16;m->e=calloc(m->cap,sizeof(LE));return m;}
static void lmfree2(LM2*m){if(!m)return;for(int i=0;i<m->cap;i++)if(m->e[i].st==2)free(m->e[i].k);free(m->e);free(m);}
static void lres(LM2*m,int nc){LE*old=m->e;int oc=m->cap;m->e=calloc(nc,sizeof(LE));m->cap=nc;m->len=0;for(int i=0;i<oc;i++)if(old[i].st==2){int idx=hf(old[i].k)%nc;while(m->e[idx].st==2)idx=(idx+1)%nc;m->e[idx]=old[i];m->len++;}m->tombs=0;free(old);}
static int*lget2(LM2*m,const char*k){int idx=hf(k)%m->cap;while(m->e[idx].st){if(m->e[idx].st==2&&!strcmp(m->e[idx].k,k))return&m->e[idx].v;idx=(idx+1)%m->cap;}return NULL;}
static void lput2(LM2*m,const char*k,int v){if(m->len+m->tombs>=m->cap*3/4)lres(m,m->cap*2);int idx=hf(k)%m->cap;int tomb=-1;while(m->e[idx].st){if(m->e[idx].st==1&&tomb==-1)tomb=idx;else if(m->e[idx].st==2&&!strcmp(m->e[idx].k,k)){m->e[idx].v=v;return;}idx=(idx+1)%m->cap;}if(tomb!=-1){idx=tomb;m->tombs--;}m->e[idx].k=strdup(k);m->e[idx].v=v;m->e[idx].st=2;m->len++;}
typedef struct{int s,e;char**p;int pc,required;}FI;typedef struct{char*k;FI v;int st;}FE;typedef struct{FE*e;int cap,len,tombs;}FM;
static FM*fmk2(void){FM*m=calloc(1,sizeof(*m));m->cap=16;m->e=calloc(m->cap,sizeof(FE));return m;}
static void fmfree2(FM*m){if(!m)return;for(int i=0;i<m->cap;i++){if(m->e[i].st==2){free(m->e[i].k);for(int j=0;j<m->e[i].v.pc;j++)free(m->e[i].v.p[j]);free(m->e[i].v.p);}}free(m->e);free(m);}
static void fres(FM*m,int nc){FE*old=m->e;int oc=m->cap;m->e=calloc(nc,sizeof(FE));m->cap=nc;m->len=0;for(int i=0;i<oc;i++)if(old[i].st==2){int idx=hf(old[i].k)%nc;while(m->e[idx].st==2)idx=(idx+1)%nc;m->e[idx]=old[i];m->len++;}m->tombs=0;free(old);}
static FI*fget2(FM*m,const char*k){int idx=hf(k)%m->cap;while(m->e[idx].st){if(m->e[idx].st==2&&!strcmp(m->e[idx].k,k))return&m->e[idx].v;idx=(idx+1)%m->cap;}return NULL;}
static void fput2(FM*m,const char*k,FI v){if(m->len+m->tombs>=m->cap*3/4)fres(m,m->cap*2);int idx=hf(k)%m->cap;int tomb=-1;while(m->e[idx].st){if(m->e[idx].st==1&&tomb==-1)tomb=idx;else if(m->e[idx].st==2&&!strcmp(m->e[idx].k,k)){for(int j=0;j<m->e[idx].v.pc;j++)free(m->e[idx].v.p[j]);free(m->e[idx].v.p);m->e[idx].v=v;return;}idx=(idx+1)%m->cap;}if(tomb!=-1){idx=tomb;m->tombs--;}m->e[idx].k=strdup(k);m->e[idx].v=v;m->e[idx].st=2;m->len++;}
typedef struct{char*n;Node*p;VM*v;}ME;typedef struct{ME*e;int len,cap;}MM;
static MM*mmk2(void){return calloc(1,sizeof(MM));}
static void mmfree2(MM*m){if(!m)return;for(int i=0;i<m->len;i++){free(m->e[i].n);if(m->e[i].v)vf(m->e[i].v);if(m->e[i].p)ft(m->e[i].p);}free(m->e);free(m);}
static ME*mget2(MM*m,const char*n){for(int i=0;i<m->len;i++)if(!strcmp(m->e[i].n,n))return&m->e[i];return NULL;}
static void mput2(MM*m,const char*n,Node*p,VM*v){if(m->len>=m->cap){m->cap=m->cap?m->cap*2:8;m->e=realloc(m->e,m->cap*sizeof(ME));}m->e[m->len].n=strdup(n);m->e[m->len].p=p;m->e[m->len].v=v;m->len++;}
static char*rwf(const char*p){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);char*b=malloc(sz+1);if(!b){fclose(f);return NULL;}size_t rd=fread(b,1,sz,f);b[rd]=0;fclose(f);return b;}
static char*mnpath(const char*p){const char*b=strrchr(p,'/');
#ifdef _WIN32
const char*b2=strrchr(p,'\\\\');if(b2&&(!b||b2>b))b=b2;
#endif
b=b?b+1:p;char*n=strdup(b);char*dot=strstr(n,".vul");if(dot)*dot=0;return n;}
V vn(void){V v={.t=VAL_NONE,.s=NULL,.o=NULL,.i=0};return v;}
V vi(int64_t i){V v={.t=VAL_INT,.s=NULL,.o=NULL,.i=i};return v;}
V vf2(double f){V v={.t=VAL_FLOAT,.s=NULL,.o=NULL,.f=f};return v;}
V vb(bool b){V v={.t=VAL_BOOL,.s=NULL,.o=NULL,.b=b};return v;}
V vs(const char*s){V v={.t=VAL_STR,.s=strdup(s?s:""),.o=NULL,.i=0};return v;}
V vc(V v){if(v.t==VAL_STR&&v.s){V c=v;c.s=strdup(v.s);return c;}if(v.t==VAL_LIST&&v.o){((LST*)v.o)->rc++;return v;}if((v.t==VAL_DICT||v.t==VAL_SET)&&v.o){((DCT*)v.o)->rc++;return v;}return v;}
void vfree(V*v){if(v->t==VAL_STR){free(v->s);v->s=NULL;return;}if(v->t==VAL_LIST&&v->o){LST*l=v->o;if(--l->rc<=0){for(int i=0;i<l->len;i++)vfree(&l->a[i]);free(l->a);free(l);}v->o=NULL;return;}if((v->t==VAL_DICT||v->t==VAL_SET)&&v->o){DCT*d=v->o;if(--d->rc<=0){vmfree2(d->m);free(d);}v->o=NULL;return;}}
bool vt(const V*v){switch(v->t){case VAL_BOOL:return v->b;case VAL_INT:return v->i!=0;case VAL_FLOAT:return v->f!=0.0;case VAL_STR:return v->s&&v->s[0];case VAL_LIST:return v->o&&((LST*)v->o)->len>0;case VAL_DICT:case VAL_SET:return v->o&&((DCT*)v->o)->m->len>0;default:return false;}}
const char*vtn(const V*v){switch(v->t){case VAL_INT:return"Int";case VAL_FLOAT:return"Float";case VAL_STR:return"Str";case VAL_BOOL:return"Bool";case VAL_LIST:return"List";case VAL_DICT:return"Dict";case VAL_SET:return"Set";default:return"None";}}
static void vrepr(const V*v,char**b,size_t*len,size_t*cap);
static void sbput(char**b,size_t*len,size_t*cap,const char*t){size_t l=strlen(t);while(*len+l+1>*cap){*cap=*cap?*cap*2:64;*b=realloc(*b,*cap);}memcpy(*b+*len,t,l+1);*len+=l;}
static void vrepr(const V*v,char**b,size_t*len,size_t*cap){if(v->t==VAL_STR){sbput(b,len,cap,"\"");sbput(b,len,cap,v->s?v->s:"");sbput(b,len,cap,"\"");return;}if(v->t==VAL_LIST){LST*l=v->o;sbput(b,len,cap,"[");for(int i=0;i<l->len;i++){if(i)sbput(b,len,cap,", ");vrepr(&l->a[i],b,len,cap);}sbput(b,len,cap,"]");return;}if(v->t==VAL_DICT){DCT*d=v->o;sbput(b,len,cap,"{");int first=1;for(int i=0;i<d->m->cap;i++)if(d->m->e[i].st==2){if(!first)sbput(b,len,cap,", ");first=0;sbput(b,len,cap,"\"");sbput(b,len,cap,d->m->e[i].k);sbput(b,len,cap,"\": ");vrepr(&d->m->e[i].v,b,len,cap);}sbput(b,len,cap,"}");return;}if(v->t==VAL_SET){DCT*d=v->o;sbput(b,len,cap,"{");int first=1;for(int i=0;i<d->m->cap;i++)if(d->m->e[i].st==2){if(!first)sbput(b,len,cap,", ");first=0;sbput(b,len,cap,d->m->e[i].k);}sbput(b,len,cap,"}");return;}char*s=vr2(v);sbput(b,len,cap,s);free(s);}
char*vr2(const V*v){char b[256];switch(v->t){case VAL_INT:snprintf(b,sizeof(b),"%lld",(long long)v->i);break;case VAL_FLOAT:{double d=v->f;if(d==(int64_t)d)snprintf(b,sizeof(b),"%.1f",d);else snprintf(b,sizeof(b),"%g",d);break;}case VAL_STR:return strdup(v->s?v->s:"");case VAL_BOOL:return strdup(v->b?"true":"false");case VAL_LIST:case VAL_DICT:case VAL_SET:{char*buf=NULL;size_t len=0,cap=0;vrepr(v,&buf,&len,&cap);return buf?buf:strdup("");}default:return strdup("None");}return strdup(b);}
static char*sm(const char*s,char m){size_t n=strlen(s),i;char*r=malloc(n+1);if(!r)return NULL;switch(m){case'U':for(i=0;i<n;i++)r[i]=toupper(s[i]);r[n]=0;return r;case'L':for(i=0;i<n;i++)r[i]=tolower(s[i]);r[n]=0;return r;case'S':{size_t a=0,b=n;while(a<b&&isspace(s[a]))a++;while(b>a&&isspace(s[b-1]))b--;for(i=0;i<b-a;i++)r[i]=s[a+i];r[b-a]=0;return r;}case'T':{int up=1;for(i=0;i<n;i++){if(isspace(s[i])){r[i]=s[i];up=1;}else{r[i]=up?toupper(s[i]):tolower(s[i]);up=0;}}r[n]=0;return r;}case'C':{for(i=0;i<n;i++)r[i]=tolower(s[i]);if(n)r[0]=toupper(r[0]);r[n]=0;return r;}default:r[0]=0;return r;}}
VM*nv(Node*p){VM*v=calloc(1,sizeof(VM));v->v=vmk2();v->l=lmk2();v->fu=fmk2();v->m=mmk2();v->p=p;v->nc=p->n;v->ip=0;v->rv=vn();v->source=parser_source?strdup(parser_source):NULL;v->parse_failed=parser_had_error();return v;}
void vf(VM*v){if(!v)return;vmfree2((VM2*)v->v);lmfree2((LM2*)v->l);fmfree2((FM*)v->fu);mmfree2((MM*)v->m);free(v->source);free(v->is);free(v->ls);for(int i=0;i<v->lml;i++){free(v->lm[i].v);vfree(&v->lm[i].e);vfree(&v->lm[i].s);}free(v->lm);for(int i=0;i<v->eml;i++){free(v->em[i].iv);free(v->em[i].vv);vfree(&v->em[i].src);}free(v->em);free(v->ts);for(int i=0;i<v->sc;i++)vfree(&v->ss[i].v);free(v->ss);free(v->me);free(v->mel);free(v->st);vfree(&v->rv);free(v);}
static bool veq(const V*a,const V*b);
static int eb(const V*l,const char*op,const V*r,V*out){
if(!strcmp(op,"&&")||!strcmp(op,"||")||!strcmp(op,"&")||!strcmp(op,"|")){
if(l->t!=VAL_BOOL||r->t!=VAL_BOOL){VERR("Error: logical operator '%s' requires Bool operands\n",op);return 1;}
*out=vb((!strcmp(op,"&&")||!strcmp(op,"&"))?(l->b&&r->b):(l->b||r->b));return 0;
}
if(l->t==VAL_LIST||r->t==VAL_LIST||l->t==VAL_DICT||r->t==VAL_DICT||l->t==VAL_SET||r->t==VAL_SET){
if(!strcmp(op,"in")){
bool found=false;
if(r->t==VAL_LIST){LST*l2=r->o;for(int i=0;i<l2->len;i++)if(veq(l,&l2->a[i])){found=true;break;}}
else if(r->t==VAL_DICT||r->t==VAL_SET){DCT*d=r->o;char*k=vr2(l);found=vget2(d->m,k)!=NULL;free(k);}
else{VERR("ERR: 'in' requires a List, Dict, or Set on the right\n");return 1;}
*out=vb(found);return 0;
}
if(l->t==VAL_LIST&&r->t==VAL_LIST){
if(!strcmp(op,"==")||!strcmp(op,"=")){
LST*a=l->o,*b=r->o;bool eq=a->len==b->len;
for(int i=0;eq&&i<a->len;i++)if(!veq(&a->a[i],&b->a[i]))eq=false;
*out=vb(eq);return 0;
}
if(!strcmp(op,"!=")){V t;eb(l,"==",r,&t);*out=vb(!t.b);vfree(&t);return 0;}
if(op[0]=='+'&&op[1]==0){LST*a=l->o,*b=r->o;LST*nl=lstnew(a->len+b->len);for(int i=0;i<a->len;i++)lstpush(nl,vc(a->a[i]));for(int i=0;i<b->len;i++)lstpush(nl,vc(b->a[i]));*out=(V){.t=VAL_LIST,.s=NULL,.o=nl,.i=0};return 0;}
VERR("ERR: unsupported List operator '%s'\n",op);return 1;
}
if(l->t==VAL_DICT&&r->t==VAL_DICT){
if(!strcmp(op,"==")||!strcmp(op,"=")||!strcmp(op,"!=")){
DCT*a=l->o,*b=r->o;bool eq=a->m->len==b->m->len;
if(eq)for(int i=0;i<a->m->cap;i++)if(a->m->e[i].st==2){V*bv=vget2(b->m,a->m->e[i].k);if(!bv||!veq(&a->m->e[i].v,bv)){eq=false;break;}}
*out=vb(!strcmp(op,"!=")?!eq:eq);return 0;
}
VERR("ERR: unsupported Dict operator '%s'\n",op);return 1;
}
if(l->t==VAL_SET&&r->t==VAL_SET){
DCT*a=l->o,*b=r->o;
if(!strcmp(op,"==")||!strcmp(op,"=")||!strcmp(op,"!=")){
bool eq=a->m->len==b->m->len;if(eq)for(int i=0;i<a->m->cap;i++)if(a->m->e[i].st==2&&!vget2(b->m,a->m->e[i].k)){eq=false;break;}
*out=vb(!strcmp(op,"!=")?!eq:eq);return 0;
}
if(op[0]=='+'&&op[1]==0){DCT*nd=dctnew();for(int i=0;i<a->m->cap;i++)if(a->m->e[i].st==2)vput2(nd->m,a->m->e[i].k,vn());for(int i=0;i<b->m->cap;i++)if(b->m->e[i].st==2)vput2(nd->m,b->m->e[i].k,vn());*out=(V){.t=VAL_SET,.s=NULL,.o=nd,.i=0};return 0;}
if(op[0]=='-'&&op[1]==0){DCT*nd=dctnew();for(int i=0;i<a->m->cap;i++)if(a->m->e[i].st==2&&!vget2(b->m,a->m->e[i].k))vput2(nd->m,a->m->e[i].k,vn());*out=(V){.t=VAL_SET,.s=NULL,.o=nd,.i=0};return 0;}
VERR("ERR: unsupported Set operator '%s'\n",op);return 1;
}
VERR("ERR: type mismatch for '%s' between %s and %s\n",op,vtn(l),vtn(r));return 1;
}
if(l->t==VAL_NONE||r->t==VAL_NONE){
if(!strcmp(op,"==")||!strcmp(op,"=")){*out=vb(l->t==r->t);return 0;}
if(!strcmp(op,"!=")){*out=vb(l->t!=r->t);return 0;}
VERR("Error: cannot apply '%s' to None\n",op);return 1;
}
if(l->t==VAL_BOOL&&r->t==VAL_BOOL){
if(!strcmp(op,"==")||!strcmp(op,"=")){*out=vb(l->b==r->b);return 0;}
if(!strcmp(op,"!=")){*out=vb(l->b!=r->b);return 0;}
VERR("Error: cannot apply '%s' to Bool\n",op);return 1;
}
if(l->t==VAL_INT&&r->t==VAL_INT){
int64_t a=l->i,b=r->i;
if(!strcmp(op,"==")||!strcmp(op,"=")){*out=vb(a==b);return 0;}
if(!strcmp(op,"!=")){*out=vb(a!=b);return 0;}
if(!strcmp(op,">=")){*out=vb(a>=b);return 0;}
if(!strcmp(op,"<=")){*out=vb(a<=b);return 0;}
if(!strcmp(op,"l")){*out=vb(a<=b);return 0;}
if(!strcmp(op,"<")){*out=vb(a<b);return 0;}
if(!strcmp(op,"m")){*out=vb(a>=b);return 0;}
if(!strcmp(op,">")){*out=vb(a>b);return 0;}
switch(op[0]){case'+':*out=vi(a+b);return 0;case'-':*out=vi(a-b);return 0;case'*':*out=vi(a*b);return 0;case'/':if(b==0){VERR("Error: division by zero\n");return 1;}*out=vi(a/b);return 0;case'%':if(b==0){VERR("Error: modulo by zero\n");return 1;}*out=vi(a%b);return 0;default:break;}
}
if(l->t==VAL_FLOAT&&r->t==VAL_FLOAT){
double a=l->f,b=r->f;
if(!strcmp(op,"==")||!strcmp(op,"=")){*out=vb(a==b);return 0;}
if(!strcmp(op,"!=")){*out=vb(a!=b);return 0;}
if(!strcmp(op,">=")){*out=vb(a>=b);return 0;}
if(!strcmp(op,"<=")){*out=vb(a<=b);return 0;}
if(!strcmp(op,"l")){*out=vb(a<=b);return 0;}
if(!strcmp(op,"<")){*out=vb(a<b);return 0;}
if(!strcmp(op,"m")){*out=vb(a>=b);return 0;}
if(!strcmp(op,">")){*out=vb(a>b);return 0;}
switch(op[0]){case'+':*out=vf2(a+b);return 0;case'-':*out=vf2(a-b);return 0;case'*':*out=vf2(a*b);return 0;case'/':if(b==0.0){VERR("Error: division by zero\n");return 1;}*out=vf2(a/b);return 0;default:break;}
if(op[0]=='%'){VERR("ERR: modulo not supported for floats\n");return 1;}
}
if(l->t==VAL_INT&&r->t==VAL_FLOAT){V lf=vf2((double)l->i);int rc=eb(&lf,op,r,out);vfree(&lf);return rc;}
if(l->t==VAL_FLOAT&&r->t==VAL_INT){V rf=vf2((double)r->i);int rc=eb(l,op,&rf,out);vfree(&rf);return rc;}
if(l->t==VAL_STR&&r->t==VAL_STR){
if(op[0]=='+'&&op[1]==0){char*b=malloc(strlen(l->s)+strlen(r->s)+1);strcpy(b,l->s);strcat(b,r->s);*out=(V){.t=VAL_STR,.s=b};return 0;}
if(!strcmp(op,"==")||!strcmp(op,"=")){*out=vb(!strcmp(l->s,r->s));return 0;}
if(!strcmp(op,"!=")){*out=vb(strcmp(l->s,r->s)!=0);return 0;}
if(!strcmp(op,"l")){*out=vb(strcmp(l->s,r->s)<=0);return 0;}
if(!strcmp(op,"<")){*out=vb(strcmp(l->s,r->s)<0);return 0;}
if(!strcmp(op,"m")){*out=vb(strcmp(l->s,r->s)>=0);return 0;}
if(!strcmp(op,">")){*out=vb(strcmp(l->s,r->s)>0);return 0;}
VERR("Error: cannot apply '%s' to strings\n",op);return 1;
}
if(op[0]=='+'&&op[1]==0){char*ls=vr2(l);char*rs=vr2(r);char*b=malloc(strlen(ls)+strlen(rs)+1);strcpy(b,ls);strcat(b,rs);free(ls);free(rs);*out=(V){.t=VAL_STR,.s=b};return 0;}
if((l->t==VAL_STR)!=(r->t==VAL_STR)){
const char*s=(l->t==VAL_STR)?l->s:r->s;char*end=NULL;double d=strtod(s,&end);
if(end&&end!=s&&*end=='\0'){
V ln=*l,rn=*r;
if(l->t==VAL_STR)ln=(strchr(s,'.')||strchr(s,'e')||strchr(s,'E'))?vf2(d):vi((int64_t)d);
else rn=(strchr(s,'.')||strchr(s,'e')||strchr(s,'E'))?vf2(d):vi((int64_t)d);
int rc=eb(&ln,op,&rn,out);if(l->t==VAL_STR)vfree(&ln);else vfree(&rn);return rc;
}
}
VERR("Error: type mismatch for '%s' between %s and %s\n",op,vtn(l),vtn(r));return 1;
}
static bool veq(const V*a,const V*b){V out;if(eb(a,"==",b,&out))return false;bool r=out.t==VAL_BOOL&&out.b;vfree(&out);return r;}
static bool ir(const V*c,const V*e,const V*s){
double cv,ev,sv;
if(c->t==VAL_INT)cv=(double)c->i;else if(c->t==VAL_FLOAT)cv=c->f;else return false;
if(e->t==VAL_INT)ev=(double)e->i;else if(e->t==VAL_FLOAT)ev=e->f;else return false;
if(s->t==VAL_INT)sv=(double)s->i;else if(s->t==VAL_FLOAT)sv=s->f;else return false;
if(sv==0.0)return false;
return sv>0.0?cv<ev:cv>ev;
}
static bool viz(const V*v){if(v->t==VAL_INT)return v->i==0;if(v->t==VAL_FLOAT)return v->f==0.0;return false;}
static int ee(VM*,Node*,V*);static int es(VM*,Node*);static int cf(VM*,Node*,V*);static int cf_named(VM*,Node*,V*);static int bc(VM*,const char*,const char*,V*,int,V*);static int cuf(VM*,const char*,V*,int,V*);
static void vdel2(VM2*m,const char*k){int idx=hf(k)%m->cap;while(m->e[idx].st){if(m->e[idx].st==2&&!strcmp(m->e[idx].k,k)){free(m->e[idx].k);vfree(&m->e[idx].v);m->e[idx].st=1;m->tombs++;m->len--;return;}idx=(idx+1)%m->cap;}}
static char*fmtstr(VM*v,const char*s){size_t cap=64,len=0;char*r=malloc(cap);if(!r)return NULL;r[0]=0;for(size_t i=0;s&&s[i];i++){if(s[i]=='{'&&s[i+1]=='{'){if(len+1>=cap){cap*=2;r=realloc(r,cap);}r[len++]='{';r[len]=0;i++;continue;}if(s[i]=='}'&&s[i+1]=='}'){if(len+1>=cap){cap*=2;r=realloc(r,cap);}r[len++]='}';r[len]=0;i++;continue;}if(s[i]=='{'){size_t j=i+1;while(s[j]&&s[j]!='}')j++;if(s[j]=='}'&&j>i+1){char name[256];size_t n=j-i-1;if(n>=sizeof(name))n=sizeof(name)-1;memcpy(name,s+i+1,n);name[n]=0;if(name[0]=='$')memmove(name,name+1,n);V*x=vget2((VM2*)v->v,name);if(x){char*q=vr2(x);size_t ql=strlen(q);while(len+ql+1>=cap){cap*=2;r=realloc(r,cap);}memcpy(r+len,q,ql);len+=ql;r[len]=0;free(q);i=j;continue;}}}if(len+2>=cap){cap*=2;r=realloc(r,cap);}r[len++]=s[i];r[len]=0;}return r;}
static int sliceInt(int x,int len,int isStart){if(x<0)x+=len;if(isStart){if(x<0)x=0;if(x>len)x=len;}else{if(x<0)x=0;if(x>len)x=len;}return x;}
static int evalSlice(VM*v,Node*n,V*out){if(!n||n->n<1)return 1;V cont;if(ee(v,n->c[0],&cont))return 1;if(cont.t!=VAL_LIST&&cont.t!=VAL_STR){VERR("Error: cannot slice %s\n",vtn(&cont));vfree(&cont);return 1;}int len=cont.t==VAL_STR?(int)strlen(cont.s):((LST*)cont.o)->len;int hs=n->v&&n->v[0]=='1',he=n->v&&n->v[1]=='1';int pos=1;V sv=vn(),ev=vn();if(hs&&ee(v,n->c[pos++],&sv)){vfree(&cont);return 1;}if(he&&ee(v,n->c[pos++],&ev)){vfree(&cont);vfree(&sv);return 1;}int a=0,b=len;if(hs){if(sv.t!=VAL_INT){VERR("Error: slice index must be Int\n");vfree(&cont);vfree(&sv);vfree(&ev);return 1;}a=sliceInt((int)sv.i,len,1);}if(he){if(ev.t!=VAL_INT){VERR("Error: slice index must be Int\n");vfree(&cont);vfree(&sv);vfree(&ev);return 1;}b=sliceInt((int)ev.i,len,0);}if(b<a)b=a;if(cont.t==VAL_STR){char*str=malloc((size_t)(b-a)+1);memcpy(str,cont.s+a,(size_t)(b-a));str[b-a]=0;*out=vs(str);free(str);}else{LST*l=(LST*)cont.o;LST*r=lstnew(b-a);for(int i=a;i<b;i++)lstpush(r,vc(l->a[i]));*out=(V){.t=VAL_LIST,.o=r};}vfree(&cont);vfree(&sv);vfree(&ev);return 0;}
static int callOne(VM*v,const char*fn,const char*tmp,V*arg,V*out){V old=vn();V*pv=vget2((VM2*)v->v,tmp);int had=pv!=NULL;if(had)old=vc(*pv);vput2((VM2*)v->v,tmp,vc(*arg));Node an={.t=ND_IDENT,.v=(char*)tmp,.n=0,.cap=0,.c=NULL,.l=0,.col=0};Node*cp[1]={&an};Node call={.t=ND_CALL,.v=(char*)fn,.n=1,.cap=0,.c=cp,.l=0,.col=0};int rc=cf(v,&call,out);if(had)vput2((VM2*)v->v,tmp,old);else vdel2((VM2*)v->v,tmp);return rc;}
static int mapValue(VM*v,const char*fn,const V*src,V*out,int keep){if(src->t!=VAL_LIST){VERR("%s: expected List\n",keep?"filter":"map");return 1;}LST*in=(LST*)src->o,*r=lstnew(in->len);for(int i=0;i<in->len;i++){V z;if(callOne(v,fn,"__fn_item",&in->a[i],&z)){for(int j=0;j<r->len;j++)vfree(&r->a[j]);free(r->a);free(r);return 1;}if(!keep||vt(&z))lstpush(r,z);else vfree(&z);}*out=(V){.t=VAL_LIST,.o=r};return 0;}
static int enumValue(const V*src,V*out){if(src->t!=VAL_LIST&&src->t!=VAL_STR){VERR("enumerate: expected List or Str\n");return 1;}int len=src->t==VAL_LIST?((LST*)src->o)->len:(int)strlen(src->s);LST*r=lstnew(len);for(int i=0;i<len;i++){LST*p=lstnew(2);lstpush(p,vi(i));if(src->t==VAL_LIST)lstpush(p,vc(((LST*)src->o)->a[i]));else{char c[2]={src->s[i],0};lstpush(p,vs(c));}lstpush(r,(V){.t=VAL_LIST,.o=p});}*out=(V){.t=VAL_LIST,.o=r};return 0;}
static int zipValue(const V*a,const V*b,V*out){if(a->t!=VAL_LIST||b->t!=VAL_LIST){VERR("zip: expected two Lists\n");return 1;}LST*x=a->o,*y=b->o;int len=x->len<y->len?x->len:y->len;LST*r=lstnew(len);for(int i=0;i<len;i++){LST*p=lstnew(2);lstpush(p,vc(x->a[i]));lstpush(p,vc(y->a[i]));lstpush(r,(V){.t=VAL_LIST,.o=p});}*out=(V){.t=VAL_LIST,.o=r};return 0;}
static int writeFile(const char*path,const char*data){FILE*f=fopen(path,"wb");if(!f){VERR("Error: cannot open '%s' for writing\n",path);return 1;}size_t n=strlen(data);int ok=fwrite(data,1,n,f)==n&&fclose(f)==0;if(!ok){VERR("Error: failed writing '%s'\n",path);return 1;}return 0;}
static int readFile(const char*path,V*out){FILE*f=fopen(path,"rb");if(!f){VERR("Error: cannot open '%s' for reading\n",path);*out=vn();return 1;}if(fseek(f,0,SEEK_END)){fclose(f);return 1;}long sz=ftell(f);if(sz<0){fclose(f);return 1;}rewind(f);char*b=malloc((size_t)sz+1);if(!b){fclose(f);return 1;}size_t n=fread(b,1,(size_t)sz,f);fclose(f);b[n]=0;*out=vs(b);free(b);return 0;}
static int strMethod(VM*v,Node*n,V*out){
if(n->n<1)return 1;V base;if(ee(v,n->c[0],&base))return 1;if(base.t!=VAL_STR){VERR("Error: string method '%s' on %s\n",n->v,vtn(&base));vfree(&base);return 1;}
const char*m=n->v;int rc=0;char*res=NULL;
if(!strcmp(m,"U")||!strcmp(m,"L")||!strcmp(m,"S")||!strcmp(m,"T")||!strcmp(m,"C")){
if(n->n!=1){VERR("Error: .%s takes no arguments\n",m);rc=1;}else{res=sm(base.s,m[0]);*out=vs(res);free(res);}
}else if(!strcmp(m,"R")){
if(n->n!=3){rc=1;}else{V a=vn(),b=vn();if(ee(v,n->c[1],&a)||ee(v,n->c[2],&b)){vfree(&a);vfree(&b);rc=1;}else if(a.t!=VAL_STR||b.t!=VAL_STR){rc=1;}else{size_t al=strlen(a.s),bl=strlen(b.s),cap=strlen(base.s)+1,len=0;res=malloc(cap);for(const char*p=base.s;*p;){if(al&&strncmp(p,a.s,al)==0){while(len+bl+1>cap){cap*=2;res=realloc(res,cap);}memcpy(res+len,b.s,bl);len+=bl;p+=al;}else{if(len+2>cap){cap*=2;res=realloc(res,cap);}res[len++]=*p++;}}res[len]=0;*out=vs(res);free(res);}vfree(&a);vfree(&b);}
}else if(!strcmp(m,"SP")){
if(n->n!=2){rc=1;}else{V d;if(ee(v,n->c[1],&d)){rc=1;d=vn();}else if(d.t!=VAL_STR){rc=1;}else{const char*del=d.s;size_t dl=strlen(del);LST*l=lstnew(4);const char*p=base.s,*q;if(!dl){for(;*p;p++){char c[2]={*p,0};lstpush(l,vs(c));}}else{while((q=strstr(p,del))){char*c=strndup(p,(size_t)(q-p));lstpush(l,vs(c));free(c);p=q+dl;}lstpush(l,vs(p));}*out=(V){.t=VAL_LIST,.o=l};}vfree(&d);}
}else if(!strcmp(m,"J")){
if(n->n!=2){rc=1;}else{V list;if(ee(v,n->c[1],&list)){rc=1;list=vn();}else if(list.t!=VAL_LIST){rc=1;}else{LST*l=list.o;size_t total=1;char**parts=calloc((size_t)(l->len?l->len:1),sizeof(char*));for(int i=0;i<l->len;i++){parts[i]=vr2(&l->a[i]);total+=strlen(parts[i])+(i?strlen(base.s):0);}res=malloc(total);res[0]=0;for(int i=0;i<l->len;i++){if(i)strcat(res,base.s);strcat(res,parts[i]);free(parts[i]);}free(parts);*out=vs(res);free(res);}vfree(&list);}
}else if(!strcmp(m,"F")){
if(n->n!=2){rc=1;}else{V sub;if(ee(v,n->c[1],&sub)){rc=1;sub=vn();}else if(sub.t!=VAL_STR){rc=1;}else{const char*q=strstr(base.s,sub.s);*out=vi(q?(int64_t)(q-base.s):-1);}vfree(&sub);}
}else{VERR("Error: unknown string method '.%s'\n",m);rc=1;}
vfree(&base);if(rc)*out=vn();return rc;
}
static int ee(VM*v,Node*n,V*out){
if(!n){*out=vn();return 0;}
switch(n->t){
case ND_NUM:{char*end=NULL;double d=strtod(n->v,&end);if(!end||*end){VERR("Error: invalid numeric literal '%s'\n",n->v);*out=vn();return 1;}if(strchr(n->v,'.')||strchr(n->v,'e')||strchr(n->v,'E'))*out=vf2(d);else*out=vi((int64_t)d);return 0;}
case ND_STR:*out=vs(n->v);return 0;
case ND_FMT:{char*r=fmtstr(v,n->v);if(!r){*out=vn();return 1;}*out=vs(r);free(r);return 0;}
case ND_IDENT:{
if(!strcmp(n->v,"true")){*out=vb(true);return 0;}
if(!strcmp(n->v,"false")){*out=vb(false);return 0;}
const char*name=n->v;if(name[0]=='$')name++;
V*x=vget2((VM2*)v->v,name);if(x){*out=vc(*x);return 0;}
const char*dot=strchr(name,'.');
if(dot&&dot!=name){char*mod=strndup(name,(size_t)(dot-name));const char*m=dot+1;int rc=bc(v,mod,m,NULL,0,out);free(mod);return rc;}
*out=vn();return 0;
}
case ND_BINOP:{
const char*op=n->v;
if(n->n==1){
V r;if(ee(v,n->c[0],&r))return 1;
if(op[0]=='+'){*out=r;return 0;}
if(op[0]=='-'){if(r.t==VAL_INT){*out=vi(-r.i);vfree(&r);return 0;}if(r.t==VAL_FLOAT){*out=vf2(-r.f);vfree(&r);return 0;}}
if(op[0]=='!'){*out=vb(!vt(&r));vfree(&r);return 0;}
VERR("Error: unsupported unary operator '%s'\n",op);vfree(&r);return 1;
}
if(n->n<2){*out=vn();return 0;}
V l;if(ee(v,n->c[0],&l))return 1;
if(!strcmp(op,"&&")||!strcmp(op,"&")){
if(!vt(&l)){*out=vb(false);vfree(&l);return 0;}
V r;if(ee(v,n->c[1],&r)){vfree(&l);return 1;}
if(r.t!=VAL_BOOL||l.t!=VAL_BOOL){VERR("Error: logical operator '%s' requires Bool operands\n",op);vfree(&l);vfree(&r);return 1;}
*out=vb(l.b&&r.b);vfree(&l);vfree(&r);return 0;
}
if(!strcmp(op,"||")||!strcmp(op,"|")){
if(vt(&l)){*out=vb(true);vfree(&l);return 0;}
V r;if(ee(v,n->c[1],&r)){vfree(&l);return 1;}
if(r.t!=VAL_BOOL||l.t!=VAL_BOOL){VERR("Error: logical operator '%s' requires Bool operands\n",op);vfree(&l);vfree(&r);return 1;}
*out=vb(l.b||r.b);vfree(&l);vfree(&r);return 0;
}
V r;if(ee(v,n->c[1],&r)){vfree(&l);return 1;}int rc=eb(&l,op,&r,out);vfree(&l);vfree(&r);return rc;
}
case ND_TERNARY:{
if(n->n!=3){*out=vn();return 1;}V c;if(ee(v,n->c[0],&c))return 1;int yes=vt(&c);vfree(&c);return ee(v,yes?n->c[1]:n->c[2],out);
}
case ND_TRYEXPR:{
if(n->n!=2){*out=vn();return 1;}V r;quiet_errors++;int rc=ee(v,n->c[0],&r);quiet_errors--;if(!rc){*out=r;return 0;}return ee(v,n->c[1],out);
}
case ND_CALL:return cf(v,n,out);
case ND_LIST:{LST*l=lstnew(n->n);for(int i=0;i<n->n;i++){V el;if(ee(v,n->c[i],&el)){for(int j=0;j<l->len;j++)vfree(&l->a[j]);free(l->a);free(l);return 1;}lstpush(l,el);}*out=(V){.t=VAL_LIST,.s=NULL,.o=l,.i=0};return 0;}
case ND_LISTCOMP:{
if(n->n<3)return 1;V src;if(ee(v,n->c[2],&src))return 1;if(src.t!=VAL_LIST){VERR("Error: list comprehension requires a List\n");vfree(&src);return 1;}
LST*in=src.o,*r=lstnew(in->len);const char*name=n->c[1]->v;V*old=vget2((VM2*)v->v,name);V saved=old?vc(*old):vn();int had=old!=NULL;
for(int i=0;i<in->len;i++){vput2((VM2*)v->v,name,vc(in->a[i]));V z;if(ee(v,n->c[0],&z)){if(had)vput2((VM2*)v->v,name,saved);else vdel2((VM2*)v->v,name);vfree(&src);vfree(&saved);for(int j=0;j<r->len;j++)vfree(&r->a[j]);free(r->a);free(r);return 1;}lstpush(r,z);}
if(had)vput2((VM2*)v->v,name,saved);else vdel2((VM2*)v->v,name);if(!had){}*out=(V){.t=VAL_LIST,.o=r};vfree(&src);return 0;
}
case ND_DICT:{DCT*d=dctnew();for(int i=0;i+1<n->n;i+=2){V key,val;if(ee(v,n->c[i],&key)){vmfree2(d->m);free(d);return 1;}if(ee(v,n->c[i+1],&val)){vfree(&key);vmfree2(d->m);free(d);return 1;}char*ks=vr2(&key);vput2(d->m,ks,val);free(ks);vfree(&key);}*out=(V){.t=VAL_DICT,.s=NULL,.o=d,.i=0};return 0;}
case ND_DICTCOMP:{
if(n->n<4)return 1;V src;if(ee(v,n->c[3],&src))return 1;if(src.t!=VAL_LIST){VERR("Error: dict comprehension requires a List\n");vfree(&src);return 1;}
DCT*d=dctnew();const char*name=n->c[2]->v;V*old=vget2((VM2*)v->v,name);V saved=old?vc(*old):vn();int had=old!=NULL;LST*in=src.o;
for(int i=0;i<in->len;i++){vput2((VM2*)v->v,name,vc(in->a[i]));V k=vn(),val=vn();if(ee(v,n->c[0],&k)||ee(v,n->c[1],&val)){if(had)vput2((VM2*)v->v,name,saved);else vdel2((VM2*)v->v,name);vfree(&src);vfree(&saved);if(k.t!=VAL_NONE)vfree(&k);if(val.t!=VAL_NONE)vfree(&val);vmfree2(d->m);free(d);return 1;}char*ks=vr2(&k);vput2(d->m,ks,val);free(ks);vfree(&k);}
if(had)vput2((VM2*)v->v,name,saved);else vdel2((VM2*)v->v,name);*out=(V){.t=VAL_DICT,.o=d};vfree(&src);return 0;
}
case ND_INDEX:{V cont;if(ee(v,n->c[0],&cont))return 1;V idx;if(ee(v,n->c[1],&idx)){vfree(&cont);return 1;}int rc=0;if(cont.t==VAL_LIST){LST*l=cont.o;if(idx.t!=VAL_INT){VERR("ERR: list index must be Int\n");rc=1;}else{int64_t i=idx.i;if(i<0)i+=l->len;if(i<0||i>=l->len){VERR("ERR: list index out of range\n");rc=1;}else*out=vc(l->a[i]);}}else if(cont.t==VAL_DICT||cont.t==VAL_SET){DCT*d=cont.o;char*key=vr2(&idx);V*val=vget2(d->m,key);free(key);if(!val){VERR("Error: key not found\n");rc=1;}else*out=vc(*val);}else if(cont.t==VAL_STR){if(idx.t!=VAL_INT){VERR("Error: string index must be Int\n");rc=1;}else{int64_t i=idx.i;int sl=(int)strlen(cont.s);if(i<0)i+=sl;if(i<0||i>=sl){VERR("Error: string index out of range\n");rc=1;}else{char cb[2]={cont.s[i],0};*out=vs(cb);}}}else{VERR("Error: cannot index %s\n",vtn(&cont));rc=1;}vfree(&cont);vfree(&idx);if(rc)*out=vn();return rc;}
case ND_SLICE:return evalSlice(v,n,out);
case ND_STRMETH:return strMethod(v,n,out);
default:VERR("Error: unexpected node type in expression\n");*out=vn();return 1;
}
}
static int bc(VM*v,const char*mod,const char*m,V*args,int argc,V*out){
(void)v;
if(!strcmp(mod,"math")){
if(!strcmp(m,"sqrt")){if(argc!=1)return 1;double x=args[0].t==VAL_INT?(double)args[0].i:args[0].t==VAL_FLOAT?args[0].f:0;*out=vf2(sqrt(x));return 0;}
if(!strcmp(m,"pi")){*out=vf2(M_PI);return 0;}if(!strcmp(m,"e")){*out=vf2(M_E);return 0;}
if(!strcmp(m,"floor")){if(argc!=1)return 1;if(args[0].t==VAL_INT){*out=vc(args[0]);return 0;}if(args[0].t==VAL_FLOAT){*out=vi((int64_t)floor(args[0].f));return 0;}return 1;}
if(!strcmp(m,"ceil")){if(argc!=1)return 1;if(args[0].t==VAL_INT){*out=vc(args[0]);return 0;}if(args[0].t==VAL_FLOAT){*out=vi((int64_t)ceil(args[0].f));return 0;}return 1;}
if(!strcmp(m,"abs")){if(argc!=1)return 1;if(args[0].t==VAL_INT){*out=vi(llabs(args[0].i));return 0;}if(args[0].t==VAL_FLOAT){*out=vf2(fabs(args[0].f));return 0;}return 1;}
VERR("math.%s: unknown method\n",m);return 1;
}
if(!strcmp(mod,"random")){
if(!strcmp(m,"rt")||!strcmp(m,"randint")){if(argc!=2||args[0].t!=VAL_INT||args[1].t!=VAL_INT){VERR("random.randint: expected 2 int args\n");return 1;}int64_t a=args[0].i,b=args[1].i;if(a>b){int64_t t=a;a=b;b=t;}uint64_t range=(uint64_t)(b-a)+1;*out=vi(a+(int64_t)(rand()%range));return 0;}
VERR("random.%s: unknown method\n",m);return 1;
}
VERR("N/A module '%s'\n",mod);return 1;
}
static int cuf(VM*t,const char*fn,V*args,int argc,V*out){
FI*fi=fget2((FM*)t->fu,fn);
if(!fi){VERR("Error: function '%s' is not defined\n",fn);*out=vn();return 1;}
if(argc<fi->required||argc>fi->pc){VERR("Error: %s() expects %d required argument%s and at most %d total; got %d\n",fn,fi->required==1?1:fi->required,fi->required==1?"":"s",fi->pc,argc);*out=vn();return 1;}
VM2*lv=vmk2();vext(lv,(VM2*)t->v);
for(int i=0;i<fi->pc;i++){
const char*pn=fi->p?fi->p[i]:NULL;
if(!pn)continue;
int optional=pn[0]=='!';const char*name=optional?pn+1:pn;
if(i<argc)vput2(lv,name,vc(args[i]));
else if(optional)vput2(lv,name,vn());
}void*ov=t->v;int sip=t->ip;t->v=lv;t->ip=fi->s+1;vfree(&t->rv);t->rv=vn();int rc=0;while(t->ip<t->nc&&t->ip<=fi->e){if(t->st[t->ip]!=-1){t->ip=t->st[t->ip];continue;}Node*nd=t->p->c[t->ip];t->ip++;Node*prev_error_node=active_error_node;active_error_node=nd;rc=es(t,nd);active_error_node=prev_error_node;if(rc==2){rc=0;break;}if(rc==1){if(t->tl>0){int tr=t->ts[t->tl-1];int c=t->mel[tr];if(c!=-1){t->tl--;Node*cn=t->p->c[c];if(cn->v[0])vput2((VM2*)t->v,cn->v,vs("error"));t->ip=c+1;rc=0;continue;}}break;}}t->v=ov;t->ip=sip;if(rc==1){vfree(&t->rv);t->rv=vn();vmfree2(lv);*out=vn();return 1;}V ret=vc(t->rv);vfree(&t->rv);t->rv=vn();vmfree2(lv);*out=ret;return 0;}
static int cf(VM*v,Node*call,V*out){
if(!call||!call->v||call->v[0]!='$')return cf_named(v,call,out);
const char*name=call->v+1;V*x=vget2((VM2*)v->v,name);
if(!x||x->t!=VAL_STR){VERR("ERR: dereference call '$%s' requires a string function name\n",name);*out=vn();return 1;}
Node tmp=*call;tmp.v=x->s;return cf_named(v,&tmp,out);
}
static int cf_named(VM*v,Node*call,V*out){
const char*fn=call->v;int argc=call->n;
if(!strcmp(fn,"o")){
if(argc!=1){VERR("o: expected 1 string argument\n");*out=vn();return 1;}
V a;if(ee(v,call->c[0],&a))return 1;if(a.t!=VAL_STR){VERR("o: expected string argument\n");vfree(&a);*out=vn();return 1;}
int code=shellStatus(system(a.s));vfree(&a);*out=vi(code);return 0;
}
if(!strcmp(fn,"map")||!strcmp(fn,"filter")){
if(argc!=2){VERR("%s: expected function and List\n",fn);*out=vn();return 1;}
const char*name=NULL;if(call->c[0]->t==ND_IDENT||call->c[0]->t==ND_STR)name=call->c[0]->v;
if(!name){VERR("%s: first argument must be a function name\n",fn);*out=vn();return 1;}
V src;if(ee(v,call->c[1],&src))return 1;int rc=mapValue(v,name,&src,out,!strcmp(fn,"filter"));vfree(&src);if(rc)*out=vn();return rc;
}
V*args=NULL;if(argc>0){args=malloc((size_t)argc*sizeof(V));for(int i=0;i<argc;i++){if(ee(v,call->c[i],&args[i])){for(int j=0;j<i;j++)vfree(&args[j]);free(args);return 1;}}}
if(!strcmp(fn,"len")){int rc=0;if(argc!=1){rc=1;}else{V*a=&args[0];if(a->t==VAL_LIST)*out=vi(((LST*)a->o)->len);else if(a->t==VAL_DICT||a->t==VAL_SET)*out=vi(((DCT*)a->o)->m->len);else if(a->t==VAL_STR)*out=vi((int64_t)strlen(a->s));else rc=1;}if(rc)*out=vn();goto done;}
if(!strcmp(fn,"push")){int rc=0;if(argc!=2||args[0].t!=VAL_LIST)rc=1;else{lstpush((LST*)args[0].o,vc(args[1]));*out=vn();}if(rc)*out=vn();goto done;}
if(!strcmp(fn,"pop")){int rc=0;if(argc!=1||args[0].t!=VAL_LIST)rc=1;else{LST*l=args[0].o;if(!l->len)rc=1;else*out=l->a[--l->len];}if(rc)*out=vn();goto done;}
if(!strcmp(fn,"keys")||!strcmp(fn,"values")){int rc=0;if(argc!=1||args[0].t!=VAL_DICT)rc=1;else{bool wantKeys=!strcmp(fn,"keys");DCT*d=args[0].o;LST*l=lstnew(d->m->len?d->m->len:1);for(int i=0;i<d->m->cap;i++)if(d->m->e[i].st==2)lstpush(l,wantKeys?vs(d->m->e[i].k):vc(d->m->e[i].v));*out=(V){.t=VAL_LIST,.o=l};}if(rc)*out=vn();goto done;}
if(!strcmp(fn,"sq")||!strcmp(fn,"abs")){int rc=0;if(argc!=1||(args[0].t!=VAL_INT&&args[0].t!=VAL_FLOAT))rc=1;else if(!strcmp(fn,"sq"))*out=vf2(sqrt(args[0].t==VAL_INT?(double)args[0].i:args[0].f));else if(args[0].t==VAL_INT)*out=vi(llabs(args[0].i));else*out=vf2(fabs(args[0].f));if(rc)*out=vn();goto done;}
if(!strcmp(fn,"pow")){int rc=0;if(argc!=2||(args[0].t!=VAL_INT&&args[0].t!=VAL_FLOAT)||(args[1].t!=VAL_INT&&args[1].t!=VAL_FLOAT))rc=1;else{double a=args[0].t==VAL_INT?(double)args[0].i:args[0].f,b=args[1].t==VAL_INT?(double)args[1].i:args[1].f;*out=vf2(pow(a,b));}if(rc)*out=vn();goto done;}
if(!strcmp(fn,"fmt")){int rc=0;if(argc!=1||args[0].t!=VAL_STR)rc=1;else{char*r=fmtstr(v,args[0].s);if(!r)rc=1;else{*out=vs(r);free(r);}}if(rc)*out=vn();goto done;}
if(!strcmp(fn,"read")){int rc=0;if(argc!=1||args[0].t!=VAL_STR)rc=1;else rc=readFile(args[0].s,out);if(rc)*out=vn();goto done;}
if(!strcmp(fn,"write")){int rc=0;if(argc!=2||args[0].t!=VAL_STR||args[1].t!=VAL_STR)rc=1;else rc=writeFile(args[0].s,args[1].s);*out=vb(rc==0);goto done;}
if(!strcmp(fn,"enumerate")){int rc=argc==1?enumValue(&args[0],out):1;if(rc)*out=vn();goto done;}
if(!strcmp(fn,"zip")){int rc=argc==2?zipValue(&args[0],&args[1],out):1;if(rc)*out=vn();goto done;}
if(strchr(fn,'.')){char*mod=strdup(fn);char*method=strchr(mod,'.');*method++=0;int rc=bc(v,mod,method,args,argc,out);free(mod);for(int i=0;i<argc;i++)vfree(&args[i]);free(args);return rc;}
{int rc=cuf(v,fn,args,argc,out);for(int i=0;i<argc;i++)vfree(&args[i]);free(args);return rc;}
done:
for(int i=0;i<argc;i++)vfree(&args[i]);free(args);return 0;
}
static int es(VM*v,Node*n){
if(!n)return 0;
switch(n->t){
case ND_PRINTNL:{if(n->n){V x;if(ee(v,n->c[0],&x))return 1;char*s=vr2(&x);printf("%s\n",s);free(s);vfree(&x);}else printf("\n");fflush(stdout);return 0;}
case ND_PRINT:{if(n->n){V x;if(ee(v,n->c[0],&x))return 1;char*s=vr2(&x);printf("%s",s);free(s);vfree(&x);fflush(stdout);}return 0;}
case ND_QUIT:exit(0);
case ND_ERROR:{if(n->n){V x;if(ee(v,n->c[0],&x))return 1;char*s=vr2(&x);VERR("Error: %s\n",s);free(s);vfree(&x);}else VERR("Error: %s\n",n->v[0]?n->v:"unspecified");return 1;}
case ND_ASSIGN:{if(n->n<1)return 0;V x=n->n>1?vn():vn();if(n->n>1&&ee(v,n->c[1],&x))return 1;vput2((VM2*)v->v,n->c[0]->v,x);return 0;}
case ND_ARITH:{if(n->n<2)return 0;const char*name=n->c[0]->v;V*cur=vget2((VM2*)v->v,name);V l=cur?vc(*cur):vi(0),r,res;if(ee(v,n->c[1],&r)){vfree(&l);return 1;}if(eb(&l,n->v,&r,&res)){vfree(&l);vfree(&r);return 1;}vput2((VM2*)v->v,name,res);vfree(&l);vfree(&r);return 0;}
case ND_DELAY:{V x=n->n?vn():vi(0);if(n->n&&ee(v,n->c[0],&x))return 1;int64_t ms=x.t==VAL_INT?x.i*1000:x.t==VAL_FLOAT?(int64_t)(x.f*1000):0;vfree(&x);
#ifdef _WIN32
Sleep((DWORD)ms);
#else
struct timespec ts;ts.tv_sec=(time_t)(ms/1000);ts.tv_nsec=(long)((ms%1000)*1000000);while(nanosleep(&ts,&ts)==-1&&errno==EINTR){}
#endif
return 0;}
case ND_INPUT:{Node*var=n->n>0?n->c[0]:NULL;Node*prompt=n->n>1?n->c[1]:NULL;Node*type=n->n>2?n->c[2]:NULL;const char*msg="? ";char typ='S';if(var&&var->t==ND_STR){msg=var->v;if(prompt&&prompt->t==ND_STR)typ=prompt->v[0];}else{if(prompt&&prompt->t==ND_STR)msg=prompt->v;if(type&&type->t==ND_STR&&type->v[0])typ=type->v[0];}printf("%s",msg);fflush(stdout);char*line=NULL;size_t len=0;if(getline(&line,&len,stdin)>0){size_t z=strlen(line);while(z&&(line[z-1]=='\n'||line[z-1]=='\r'))line[--z]=0;}V val;switch(typ){case'I':case'i':val=vi(atoll(line?line:"0"));break;case'F':case'f':val=vf2(atof(line?line:"0"));break;default:val=vs(line?line:"");break;}free(line);if(var&&var->t==ND_IDENT)vput2((VM2*)v->v,var->v,val);else vfree(&val);return 0;}
case ND_STRREP:{if(n->n<3)return 0;Node*var=n->c[0];V*cur=vget2((VM2*)v->v,var->v);if(!cur||cur->t!=VAL_STR)return 0;const char*fs=n->c[1]->v,*ts=n->c[2]->v;size_t fl=strlen(fs),tl=strlen(ts),cap=strlen(cur->s)+1,len=0;char*r=malloc(cap);for(const char*p=cur->s;*p;){if(fl&&strncmp(p,fs,fl)==0){while(len+tl+1>cap){cap*=2;r=realloc(r,cap);}memcpy(r+len,ts,tl);len+=tl;p+=fl;}else{if(len+2>cap){cap*=2;r=realloc(r,cap);}r[len++]=*p++;}}r[len]=0;vput2((VM2*)v->v,var->v,(V){.t=VAL_STR,.s=r});return 0;}
case ND_DEL:{vdel2((VM2*)v->v,n->v);return 0;}
case ND_IDXASSIGN:{if(n->n<3)return 1;V cont=vn(),idx=vn(),val=vn();if(ee(v,n->c[0],&cont)||ee(v,n->c[1],&idx)||ee(v,n->c[2],&val)){vfree(&cont);vfree(&idx);vfree(&val);return 1;}int rc=0;if(cont.t==VAL_LIST&&idx.t==VAL_INT){LST*l=cont.o;int64_t i=idx.i;if(i<0)i+=l->len;if(i<0||i>=l->len)rc=1;else{vfree(&l->a[i]);l->a[i]=vc(val);}}else if(cont.t==VAL_DICT){char*k=vr2(&idx);vput2(((DCT*)cont.o)->m,k,vc(val));free(k);}else rc=1;vfree(&cont);vfree(&idx);vfree(&val);return rc;}
case ND_IF:{V c;if(n->n&&ee(v,n->c[0],&c))return 1;bool yes=n->n?vt(&c):false;if(n->n)vfree(&c);int me=v->me[v->ip-1];int nx=v->mel[v->ip-1];if(yes){if(v->il>=v->ic)v->is=dg(v->is,&v->ic,sizeof(int));v->is[v->il++]=v->ip-1;}else v->ip=(nx!=-1?nx:me);return 0;}
case ND_ELIF:{V c;if(n->n&&ee(v,n->c[0],&c))return 1;bool yes=n->n&&vt(&c);if(n->n)vfree(&c);int me=v->me[v->ip-1];int nx=v->mel[v->ip-1];if(yes){if(v->il>=v->ic)v->is=dg(v->is,&v->ic,sizeof(int));v->is[v->il++]=v->ip-1;}else v->ip=(nx!=-1?nx:me);return 0;}
case ND_ELSE:{if(v->il>0){int start=v->is[--v->il];v->ip=v->me[start];}return 0;}
case ND_ENDIF:{if(v->il>0&&v->me[v->is[v->il-1]]==v->ip-1)v->il--;return 0;}
case ND_WHILE:{V c;if(n->n<1||ee(v,n->c[0],&c))return 1;if(vt(&c)){if(v->ll==v->lc)v->ls=dg(v->ls,&v->lc,sizeof(int));if(v->ll==0||v->ls[v->ll-1]!=v->ip-1)v->ls[v->ll++]=v->ip-1;}else{if(v->ll>0&&v->ls[v->ll-1]==v->ip-1)v->ll--;v->ip=v->me[v->ip-1]+1;}vfree(&c);return 0;}
case ND_WEND:{if(v->ll==0)return 0;int start=v->ls[v->ll-1];Node*sn=v->p->c[start];if(sn->t==ND_FOR){if(v->lml){LM*lm=&v->lm[v->lml-1];V*cur=vget2((VM2*)v->v,lm->v);V current=cur?vc(*cur):vi(0),next;if(!eb(&current,"+",&lm->s,&next)){vput2((VM2*)v->v,lm->v,next);V*upd=vget2((VM2*)v->v,lm->v);if(ir(upd,&lm->e,&lm->s)){vfree(&current);v->ip=start+1;}else{v->ll--;free(lm->v);vfree(&lm->e);vfree(&lm->s);v->lml--;v->ip=v->me[start]+1;}}else return 1;vfree(&current);}else{v->ll--;v->ip=v->me[start]+1;}}else if(sn->t==ND_ENUMERATE){if(v->eml){EM*em=&v->em[v->eml-1];LST*lst=em->src.o;em->pos++;if(em->pos<lst->len){vput2((VM2*)v->v,em->iv,vi(em->pos));vput2((VM2*)v->v,em->vv,vc(lst->a[em->pos]));v->ip=start+1;}else{v->ll--;free(em->iv);free(em->vv);vfree(&em->src);v->eml--;v->ip=v->me[start]+1;}}else{v->ll--;v->ip=v->me[start]+1;}}else v->ip=start;return 0;}
case ND_FOR:{if(n->n<3)return 1;V a=vn(),b=vn(),st=vn();if(ee(v,n->c[1],&a)||ee(v,n->c[2],&b)){vfree(&a);vfree(&b);return 1;}if(n->n>3&&ee(v,n->c[3],&st)){vfree(&a);vfree(&b);return 1;}if(n->n<=3)st=vi(1);if((a.t!=VAL_INT&&a.t!=VAL_FLOAT)||(b.t!=VAL_INT&&b.t!=VAL_FLOAT)||(st.t!=VAL_INT&&st.t!=VAL_FLOAT)){vfree(&a);vfree(&b);vfree(&st);return 1;}vput2((VM2*)v->v,n->c[0]->v,vc(a));if(viz(&st)){vfree(&a);vfree(&b);vfree(&st);v->ip=v->me[v->ip-1]+1;return 0;}if(ir(&a,&b,&st)){if(v->ll==v->lc)v->ls=dg(v->ls,&v->lc,sizeof(int));v->ls[v->ll++]=v->ip-1;if(v->lml==v->lmc)v->lm=dg(v->lm,&v->lmc,sizeof(*v->lm));v->lm[v->lml].v=strdup(n->c[0]->v);v->lm[v->lml].e=vc(b);v->lm[v->lml].s=vc(st);v->lml++;}else v->ip=v->me[v->ip-1]+1;vfree(&a);vfree(&b);vfree(&st);return 0;}
case ND_ENUMERATE:{if(n->n<3)return 1;V src;if(ee(v,n->c[2],&src))return 1;if(src.t!=VAL_LIST){VERR("E: enumerate requires List\n");vfree(&src);return 1;}LST*l=src.o;if(!l->len){vfree(&src);v->ip=v->me[v->ip-1]+1;return 0;}if(v->eml==v->emc)v->em=dg(v->em,&v->emc,sizeof(*v->em));v->em[v->eml].iv=strdup(n->c[0]->v);v->em[v->eml].vv=strdup(n->c[1]->v);v->em[v->eml].src=vc(src);v->em[v->eml].pos=0;v->eml++;vput2((VM2*)v->v,n->c[0]->v,vi(0));vput2((VM2*)v->v,n->c[1]->v,vc(l->a[0]));if(v->ll==v->lc)v->ls=dg(v->ls,&v->lc,sizeof(int));v->ls[v->ll++]=v->ip-1;vfree(&src);return 0;}
case ND_MAP:case ND_FILTER:{if(n->n<1)return 1;V src;if(ee(v,n->c[0],&src))return 1;V r;int rc=mapValue(v,n->v,&src,&r,n->t==ND_FILTER);vfree(&src);if(rc)return 1;vput2((VM2*)v->v,"_",r);return 0;}
case ND_ZIP:{if(n->n!=2)return 1;V a,b,r;if(ee(v,n->c[0],&a)||ee(v,n->c[1],&b)){vfree(&a);vfree(&b);return 1;}int rc=zipValue(&a,&b,&r);vfree(&a);vfree(&b);if(rc)return 1;vput2((VM2*)v->v,"_",r);return 0;}
case ND_READ:{if(n->n<1)return 1;V path;if(ee(v,n->c[0],&path))return 1;if(path.t!=VAL_STR){vfree(&path);return 1;}V r;int rc=readFile(path.s,&r);vfree(&path);if(rc)return 1;if(n->v[0])vput2((VM2*)v->v,n->v,r);else{char*s=vr2(&r);printf("%s",s);free(s);vfree(&r);}return 0;}
case ND_WRITE:{if(n->n<2)return 1;V path=vn(),data=vn();if(ee(v,n->c[0],&path)||ee(v,n->c[1],&data)){vfree(&path);vfree(&data);return 1;}int rc=(path.t==VAL_STR&&data.t==VAL_STR)?writeFile(path.s,data.s):1;vfree(&path);vfree(&data);return rc;}
case ND_INSPECT:{if(!n->n)return 0;V x;if(ee(v,n->c[0],&x))return 1;char*s=vr2(&x);printf("%s: %s\n",vtn(&x),s);free(s);vfree(&x);return 0;}
case ND_SWITCH:{V x;if(n->n&&ee(v,n->c[0],&x))return 1;if(v->sl==v->sc)v->ss=dg(v->ss,&v->sc,sizeof(*v->ss));v->ss[v->sl].v=n->n?x:vn();v->ss[v->sl].m=false;v->sl++;return 0;}
case ND_CASE:{if(!v->sl||!n->n)return 1;SF*sf=&v->ss[v->sl-1];if(sf->m){int i=v->ip,depth=0;while(i<v->nc){NT t=v->p->c[i]->t;if(t==ND_SWITCH)depth++;else if(t==ND_ENDSW){if(!depth){v->ip=i;return 0;}depth--;}i++;}v->ip=v->nc;return 0;}V x;if(ee(v,n->c[0],&x))return 1;if(veq(&sf->v,&x))sf->m=true;vfree(&x);if(!sf->m){int i=v->ip,depth=0;while(i<v->nc){NT t=v->p->c[i]->t;if(t==ND_SWITCH)depth++;else if(t==ND_ENDSW){if(!depth){v->ip=i;return 0;}depth--;}else if(!depth&&(t==ND_CASE||t==ND_DEF)){v->ip=i;return 0;}i++;}v->ip=v->nc;}return 0;}
case ND_DEF:{if(!v->sl)return 0;SF*sf=&v->ss[v->sl-1];if(sf->m){int i=v->ip,depth=0;while(i<v->nc){NT t=v->p->c[i]->t;if(t==ND_SWITCH)depth++;else if(t==ND_ENDSW){if(!depth){v->ip=i;return 0;}depth--;}i++;}v->ip=v->nc;return 0;}sf->m=true;return 0;}
case ND_ENDSW:if(v->sl){vfree(&v->ss[v->sl-1].v);v->sl--;}return 0;
case ND_TRY:if(v->tl==v->tc)v->ts=dg(v->ts,&v->tc,sizeof(int));v->ts[v->tl++]=v->ip-1;return 0;
case ND_CATCH:if(v->tl){int tr=v->ts[--v->tl];v->ip=v->me[tr]+1;}return 0;
case ND_ENDTRY:return 0;
case ND_FUNC:return 0;
case ND_RETURN:if(n->n){if(ee(v,n->c[0],&v->rv))return 1;}else{vfree(&v->rv);v->rv=vn();}return 2;
case ND_ENDFN:return 2;
case ND_EXEC:case ND_CEXEC:{V cmd;if(n->n&&ee(v,n->c[0],&cmd))return 1;else if(!n->n)cmd=vs(n->v);if(cmd.t!=VAL_STR){vfree(&cmd);return 1;}int code=shellStatus(system(cmd.s));vfree(&cmd);return code?1:0;}
case ND_IMPORT:{const char*target=n->v;if(!strcmp(target,"math")||!strcmp(target,"random")){vput2((VM2*)v->v,target,vs(target));return 0;}char*file=strstr(target,".vul")?strdup(target):malloc(strlen(target)+5);if(!strstr(target,".vul"))sprintf(file,"%s.vul",target);char*modn=mnpath(file);MM*mods=(MM*)v->m;if(mget2(mods,modn)){free(file);free(modn);return 0;}char*src=rwf(file);if(!src){free(file);free(modn);return 1;}Node*sub=parse(src);free(src);VM*modv=nv(sub);vp(modv);mput2(mods,modn,sub,modv);int rc=vr(modv);free(file);free(modn);return rc;}
case ND_CLASS:if(n->v[0])vput2((VM2*)v->v,n->v,vs("class"));return 0;
default:{V x;int rc=ee(v,n,&x);if(!rc)vfree(&x);return rc;}
}
}
void vp(VM*v){
int n=v->nc;Node*p=v->p;free(v->me);v->me=malloc((size_t)n*sizeof(int));free(v->mel);v->mel=malloc((size_t)n*sizeof(int));for(int i=0;i<n;i++){v->me[i]=n;v->mel[i]=-1;}
typedef struct{int idx;int type;}SE;SE*st=NULL;int sc=0,cap=0;
#define PUSH(i,t) do{if(sc>=cap){cap=cap?cap*2:16;st=realloc(st,(size_t)cap*sizeof(*st));}st[sc].idx=(i);st[sc].type=(t);sc++;}while(0)
for(int i=0;i<n;i++){
Node*nd=p->c[i];
switch(nd->t){
case ND_IF:PUSH(i,ND_IF);break;
case ND_ELIF:case ND_ELSE:{int fi=-1;for(int j=sc-1;j>=0;j--)if(st[j].type==ND_IF){fi=st[j].idx;break;}if(fi>=0){int b=fi;while(v->mel[b]!=-1)b=v->mel[b];v->mel[b]=i;}break;}
case ND_ENDIF:{for(int j=sc-1;j>=0;j--)if(st[j].type==ND_IF){int sidx=st[j].idx;memmove(&st[j],&st[j+1],(size_t)(sc-j-1)*sizeof(*st));sc--;v->me[sidx]=i;int b=v->mel[sidx];while(b!=-1){v->me[b]=i;b=v->mel[b];}break;}break;}
case ND_WHILE:case ND_FOR:case ND_ENUMERATE:case ND_SWITCH:case ND_TRY:case ND_FUNC:PUSH(i,nd->t);break;
case ND_WEND:{int match=ND_WHILE;for(int j=sc-1;j>=0;j--){if(st[j].type==ND_FOR||st[j].type==ND_ENUMERATE){match=st[j].type;break;}if(st[j].type==ND_WHILE)break;}for(int j=sc-1;j>=0;j--)if(st[j].type==match){int sidx=st[j].idx;memmove(&st[j],&st[j+1],(size_t)(sc-j-1)*sizeof(*st));sc--;v->me[sidx]=i;v->me[i]=sidx;break;}break;}
case ND_ENDSW:case ND_ENDTRY:case ND_ENDFN:{int match=nd->t==ND_ENDSW?ND_SWITCH:nd->t==ND_ENDTRY?ND_TRY:ND_FUNC;for(int j=sc-1;j>=0;j--)if(st[j].type==match){int sidx=st[j].idx;memmove(&st[j],&st[j+1],(size_t)(sc-j-1)*sizeof(*st));sc--;v->me[sidx]=i;v->me[i]=sidx;break;}break;}
case ND_CATCH:for(int j=sc-1;j>=0;j--)if(st[j].type==ND_TRY&&v->mel[st[j].idx]==-1){v->mel[st[j].idx]=i;break;break;}
case ND_LABEL:if(nd->v[0])lput2((LM2*)v->l,nd->v,i);break;
default:break;
}
}
free(st);free(v->st);v->st=malloc((size_t)n*sizeof(int));for(int i=0;i<n;i++)v->st[i]=-1;
for(int i=0;i<n;i++)if(p->c[i]->t==ND_FUNC&&v->me[i]<n){
v->st[i]=v->me[i]+1;
FI fi={.s=i,.e=v->me[i],.pc=p->c[i]->n,.required=0,.p=NULL};
fi.p=malloc((size_t)fi.pc*sizeof(char*));
for(int j=0;j<fi.pc;j++){
fi.p[j]=strdup(p->c[i]->c[j]->v);
if(fi.p[j][0]!='!')fi.required++;
}
if(p->c[i]->v[0])fput2((FM*)v->fu,p->c[i]->v,fi);
}
#undef PUSH
}
int vr(VM*v){if(v&&v->parse_failed)return 1;Node*p=v->p;VM*prev_error_vm=active_error_vm;active_error_vm=v;while(v->ip<v->nc){if(v->st[v->ip]!=-1){v->ip=v->st[v->ip];continue;}Node*nd=p->c[v->ip];v->ip++;Node*prev_error_node=active_error_node;active_error_node=nd;int rs=es(v,nd);active_error_node=prev_error_node;if(rs==2){VERR("Error: return/endfn outside function\n");active_error_vm=prev_error_vm;return 1;}if(rs==1){if(v->tl>0){int tr=v->ts[v->tl-1];int c=v->mel[tr];if(c!=-1){v->tl--;Node*cn=p->c[c];if(cn->v[0])vput2((VM2*)v->v,cn->v,vs("error"));v->ip=c+1;continue;}}active_error_vm=prev_error_vm;return 1;}}active_error_vm=prev_error_vm;return 0;}
