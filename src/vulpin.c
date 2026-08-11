#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>
#include"lib/vm.h"

static Node*L(const char*p){
    FILE*f=fopen(p,"r");

    if(!f){
e:
        fprintf(stderr,"Error: cannot open '%s'\n",p);
        return 0;
    }

    fseek(f,0,2);
    long s=ftell(f);
    rewind(f);

    char*b=malloc(s+1);

    if(!b){
        fclose(f);
        goto e;
    }

    size_t n=fread(b,1,s,f);
    b[n]=0;
    fclose(f);

    Node*g=parse(b);
    free(b);

    return g;
}

int main(int c,char**v){
    if(c>1&&!strcmp(v[1],"version")){
        puts("Vulpin 0.9");
        return 0;
    }

    if(c<2){
        fputs("Usage: vulpin <file.vul>\n",stderr);
        return 1;
    }

    srand(time(0));

    Node*p=L(v[1]);

    if(!p)return 1;

    if(c>2&&!strcmp(v[2],"--debug"))
        printTree(p,0);

    VM*m=vm_new(p);
    vm_precompute(m);

    int r=vm_run(m);

    vm_free(m);
    freeTree(p);

    if(r){
        fputs("Program exited with error.\n",stderr);
        return 1;
    }

    return 0;
}
