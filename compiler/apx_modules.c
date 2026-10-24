#define _POSIX_C_SOURCE 200809L
#include "apx_modules.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define MAX_MODULES 64u
#define MAX_SOURCE 65536u
#define MAX_SYMBOLS 1024u
#define MAX_IMPORTS 64u
typedef struct {size_t start,end;char *text;} Token;
typedef struct {char *name;int type,exported;} Symbol;
typedef struct {char *alias,*key;unsigned target;} Import;
typedef struct {const char *source,*key;char *name;Symbol symbols[MAX_SYMBOLS];size_t symbols_n;Import imports[MAX_IMPORTS];size_t imports_n;char *exports[MAX_SYMBOLS];size_t exports_n;int visit,reserved;unsigned id;} Module;
typedef struct {Module *modules;size_t count;char *out;size_t length;const char *error;int changed;} Link;
static int ident(const char *s){if(!s||!((*s>='A'&&*s<='Z')||(*s>='a'&&*s<='z')||*s=='_')||!strcmp(s,"_"))return 0;for(++s;*s;s++)if(!((*s>='A'&&*s<='Z')||(*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='_'))return 0;return 1;}
static char *copy(const char *s,size_t n){char *p=malloc(n+1);if(p){memcpy(p,s,n);p[n]=0;}return p;}
static int emit(Link *l,const char *s,size_t n){if(n>MAX_SOURCE||l->length>MAX_SOURCE-n){l->error="module_source_limit";return 0;}memcpy(l->out+l->length,s,n);l->length+=n;l->out[l->length]=0;return 1;}
static void token_free(Token *t,size_t n){for(size_t i=0;i<n;i++)free(t[i].text);free(t);}
static Token *tokenize(const char *s,size_t n,size_t *count){Token *t=calloc(n+1,sizeof *t);size_t i=0,k=0;if(!t)return NULL;
 while(i<n){if(isspace((unsigned char)s[i])){i++;continue;}if(s[i]==';')break;size_t start=i;
  if(s[i]=='"'){i++;while(i<n){if(s[i]=='\\'){i+=i+1<n?2:1;continue;}if(s[i++]=='"')break;}}
  else if(i+1<n&&((s[i]==':'&&s[i+1]==':')||(s[i]=='-'&&s[i+1]=='>')))i+=2;
  else if(isalpha((unsigned char)s[i])||s[i]=='_'){i++;while(i<n&&(isalnum((unsigned char)s[i])||s[i]=='_'))i++;}
  else if(strchr("()[]:=",s[i]))i++;else{ i++;while(i<n&&!isspace((unsigned char)s[i])&&!strchr("()[]:=;\"",s[i]))i++;}
  t[k].start=start;t[k].end=i;t[k].text=copy(s+start,i-start);if(!t[k].text){token_free(t,k);return NULL;}k++;
 }*count=k;return t;}
static Symbol *symbol(Module *m,const char *name){for(size_t i=0;i<m->symbols_n;i++)if(!strcmp(m->symbols[i].name,name))return &m->symbols[i];return NULL;}
static int scan(Link *l,Module *m){size_t pos=0;int first=1;
 if(!m->source||strlen(m->source)>MAX_SOURCE){l->error="module_source_limit";return 0;}
 while(m->source[pos]){size_t start=pos;while(m->source[pos]&&m->source[pos]!='\n')pos++;size_t n=pos-start;if(m->source[pos])pos++;size_t count=0;Token *t=tokenize(m->source+start,n,&count);if(!t){l->error="module_allocation_failed";return 0;}if(!count){token_free(t,count);continue;}
  int top=t[0].start==0;const char *head=t[0].text;
  for(size_t i=0;i<count;i++)if(!strncmp(t[i].text,"__apx_",6))m->reserved=1;
  if(first){first=0;if(!top||count!=2||strcmp(head,"M")||!ident(t[1].text)){l->error="invalid_module_header";token_free(t,count);return 0;}m->name=copy(t[1].text,strlen(t[1].text));if(!m->name){l->error="module_allocation_failed";token_free(t,count);return 0;}if(m->key&&strcmp(m->key,m->name)){l->error="module_name_mismatch";token_free(t,count);return 0;}token_free(t,count);continue;}
  if(top&&!strcmp(head,"I")){l->changed=1;if(count!=3||!ident(t[1].text)||m->imports_n==MAX_IMPORTS){l->error="invalid_import";token_free(t,count);return 0;}
   const char *key=t[2].text;size_t len=strlen(key);if(len<3||key[0]!='"'||key[len-1]!='"'){l->error="invalid_module_key";token_free(t,count);return 0;}char *plain=copy(key+1,len-2);if(!plain||!ident(plain)){free(plain);l->error="invalid_module_key";token_free(t,count);return 0;}
   for(size_t i=0;i<m->imports_n;i++)if(!strcmp(m->imports[i].alias,t[1].text)){free(plain);l->error="duplicate_import_alias";token_free(t,count);return 0;}
   Import *im=&m->imports[m->imports_n++];im->alias=copy(t[1].text,strlen(t[1].text));im->key=plain;if(!im->alias){l->error="module_allocation_failed";token_free(t,count);return 0;}
  }else if(top&&!strcmp(head,"V")){l->changed=1;if(count!=2||!ident(t[1].text)||m->exports_n==MAX_SYMBOLS){l->error="invalid_export";token_free(t,count);return 0;}for(size_t i=0;i<m->exports_n;i++)if(!strcmp(m->exports[i],t[1].text)){l->error="invalid_export";token_free(t,count);return 0;}m->exports[m->exports_n++]=copy(t[1].text,strlen(t[1].text));
  }else if(top&&(!strcmp(head,"T")||!strcmp(head,"F")||!strcmp(head,"X"))){if(count<2||!ident(t[1].text)||m->symbols_n==MAX_SYMBOLS){l->error="invalid_module_declaration";token_free(t,count);return 0;}if(m->key&&!strncmp(t[1].text,"__apx_",6)){l->error="reserved_module_symbol";token_free(t,count);return 0;}if(symbol(m,t[1].text)){l->error="duplicate_module_symbol";token_free(t,count);return 0;}Symbol *sym=&m->symbols[m->symbols_n++];sym->name=copy(t[1].text,strlen(t[1].text));sym->type=!strcmp(head,"T")?1:!strcmp(head,"F")?0:2;
  }token_free(t,count);
 }if(first){l->error="invalid_module_header";return 0;}if(m->reserved&&(m->key||m->imports_n||m->exports_n)){l->error="reserved_module_symbol";return 0;}if(m->imports_n)for(size_t i=0;i<m->symbols_n;i++)if(!strncmp(m->symbols[i].name,"__apx_",6)){l->error="reserved_module_symbol";return 0;}for(size_t i=0;i<m->exports_n;i++){Symbol *s=symbol(m,m->exports[i]);if(!s||s->type==2){l->error="invalid_export";return 0;}s->exported=1;}return 1;}
static void release(Link *l){for(size_t j=0;j<l->count;j++){Module *m=&l->modules[j];free(m->name);for(size_t i=0;i<m->symbols_n;i++)free(m->symbols[i].name);for(size_t i=0;i<m->imports_n;i++){free(m->imports[i].alias);free(m->imports[i].key);}for(size_t i=0;i<m->exports_n;i++)free(m->exports[i]);}free(l->modules);}
static int visit(Link *l,unsigned index,unsigned depth){Module *m=&l->modules[index];if(depth>MAX_MODULES){l->error="module_depth_limit";return 0;}if(m->visit==1){l->error="module_cycle";return 0;}if(m->visit==2)return 1;m->visit=1;
 if(!m->name&&!scan(l,m))return 0;
 for(size_t i=0;i<m->imports_n;i++){Import *im=&m->imports[i];size_t j;for(j=1;j<l->count;j++)if(!strcmp(l->modules[j].key,im->key))break;if(j==l->count){l->error="missing_module";return 0;}im->target=(unsigned)j;if(!visit(l,(unsigned)j,depth+1))return 0;}m->visit=2;return 1;}
static char *qualified(unsigned id,const char *name){if(!id)return copy(name,strlen(name));size_t n=strlen(name)+32;char *s=malloc(n);if(s)snprintf(s,n,"__apx_m%u_%s",id,name);return s;}
static int rename_ref(Link *l,Module *m,Token *t,size_t count,size_t at,char **replacement,int need_type){if(at>=count)return 1;
 Module *owner=m;const char *name=t[at].text;size_t end=at;
 if(at+2<count&&!strcmp(t[at+1].text,"::")){Import *im=NULL;for(size_t j=0;j<m->imports_n;j++)if(!strcmp(m->imports[j].alias,name))im=&m->imports[j];if(!im){l->error="unknown_import_alias";return 0;}owner=&l->modules[im->target];name=t[at+2].text;end=at+2;Symbol *s=symbol(owner,name);if(!s){l->error="unknown_module_symbol";return 0;}if(!s->exported){l->error="private_module_symbol";return 0;}if(need_type&&s->type!=1){l->error="invalid_module_type";return 0;}}
 Symbol *s=symbol(owner,name);if(s&&s->type!=2&&(!need_type||s->type==1)){
  if(!replacement[at])replacement[at]=qualified(owner->id,name);
  if(!replacement[at]){l->error="module_allocation_failed";return 0;}for(size_t j=at+1;j<=end;j++){if(!replacement[j])replacement[j]=copy("",0);if(!replacement[j]){l->error="module_allocation_failed";return 0;}}
 }return 1;}
static size_t type_ref(Link *l,Module *m,Token *t,size_t n,size_t at,char **repl,unsigned depth){if(at>=n||depth>64){l->error="invalid_module_type";return n;}
 if(!strcmp(t[at].text,"[")){size_t end=type_ref(l,m,t,n,at+1,repl,depth+1);return end<n&&!strcmp(t[end].text,"]")?end+1:end;}
 if(!strcmp(t[at].text,"result")){size_t end=type_ref(l,m,t,n,at+1,repl,depth+1);return type_ref(l,m,t,n,end,repl,depth+1);}
 if(!strcmp(t[at].text,"option")||!strcmp(t[at].text,"builder")||!strcmp(t[at].text,"map"))return type_ref(l,m,t,n,at+1,repl,depth+1);
 if(!rename_ref(l,m,t,n,at,repl,1))return n;
 return at+2<n&&!strcmp(t[at+1].text,"::")?at+3:at+1;}
static int assemble_module(Link *l,Module *m,int root_module){size_t pos=0;int in_type=0;
 while(m->source[pos]){size_t start=pos;while(m->source[pos]&&m->source[pos]!='\n')pos++;size_t end=pos;if(m->source[pos])pos++;size_t n=end-start,count=0;Token *t=tokenize(m->source+start,n,&count);if(!t){l->error="module_allocation_failed";return 0;}
  if(!count){token_free(t,count);if(!emit(l,m->source+start,pos-start))return 0;continue;}int top=t[0].start==0;const char *head=t[0].text;
  if(top&&(!strcmp(head,"I")||!strcmp(head,"V")||!strcmp(head,"M"))){if(!strcmp(head,"M")&&root_module){if(!emit(l,m->source+start,pos-start)){token_free(t,count);return 0;}}token_free(t,count);continue;}
  if(top)in_type=!strcmp(head,"T");
  char **repl=calloc(count,sizeof *repl);if(!repl){token_free(t,count);l->error="module_allocation_failed";return 0;}
  if(top&&count>1&&(!strcmp(head,"T")||!strcmp(head,"F")||!strcmp(head,"X")))repl[1]=qualified(m->id,t[1].text);
  if(top&&!strcmp(head,"F")){for(size_t i=2;i<count;i++)if(!strcmp(t[i].text,":")||!strcmp(t[i].text,"->")){size_t next=type_ref(l,m,t,count,i+1,repl,0);i=next?next-1:i;}}
  else if(in_type&&!top){for(size_t i=1;i<count;i++)if(!strcmp(t[i].text,":")){size_t next=type_ref(l,m,t,count,i+1,repl,0);i=next?next-1:i;}}
  else if(!top&&!strcmp(head,"L")&&count>3&&!strcmp(t[2].text,":"))type_ref(l,m,t,count,3,repl,0);
  for(size_t i=0;i+1<count&&!l->error;i++)if(!strcmp(t[i].text,"(")){if(!rename_ref(l,m,t,count,i+1,repl,0))break;if(i+2<count&&(!strcmp(t[i+1].text,"buf")||!strcmp(t[i+1].text,"map")))type_ref(l,m,t,count,i+2,repl,0);}
  /* A remaining namespace token is not a legal field/binding expression. */
  for(size_t i=0;i<count&&!l->error;i++)if(!strcmp(t[i].text,"::")&&!repl[i])l->error="invalid_qualified_reference";
  size_t cursor=0;for(size_t i=0;i<count&&!l->error;i++){if(!emit(l,m->source+start+cursor,t[i].start-cursor))break;if(!emit(l,repl[i]?repl[i]:t[i].text,repl[i]?strlen(repl[i]):strlen(t[i].text)))break;cursor=t[i].end;}
  if(!l->error)emit(l,m->source+start+cursor,n-cursor);
  if(!l->error)emit(l,"\n",1);
  for(size_t i=0;i<count;i++)free(repl[i]);
  free(repl);token_free(t,count);if(l->error)return 0;
 }return 1;}
static char *diagnostic(const char *kind){char s[512];snprintf(s,sizeof s,"{\"schema\":\"apodexis.diagnostic.v1\",\"severity\":\"error\",\"code\":\"N105\",\"phase\":\"resolve\",\"kind\":\"%s\",\"message\":\"module resolution failed: %s\"}",kind,kind);return copy(s,strlen(s));}
int apx_modules_resolve(const char *entry,const ApxModuleSource *sources,size_t count,char **out,char **diag){Link l={0};*out=NULL;*diag=NULL;if(count>=MAX_MODULES||(!sources&&count)){*diag=diagnostic("module_count_limit");return 1;}
 l.count=count+1;l.modules=calloc(l.count,sizeof *l.modules);l.out=malloc(MAX_SOURCE+1);if(!l.modules||!l.out){free(l.modules);free(l.out);*diag=diagnostic("module_allocation_failed");return 1;}l.out[0]=0;l.modules[0].source=entry;
 for(size_t i=0;i<count;i++){if(!ident(sources[i].name)){l.error="invalid_module_key";break;}for(size_t j=0;j<i;j++)if(!strcmp(sources[i].name,sources[j].name))l.error="duplicate_module_key";l.modules[i+1].key=sources[i].name;l.modules[i+1].source=sources[i].source;}
 if(!l.error)visit(&l,0,0);
 if(!l.error){unsigned id=1;for(size_t i=1;i<l.count;i++)if(l.modules[i].visit==2){unsigned rank=0;for(size_t j=1;j<l.count;j++)if(l.modules[j].visit==2&&strcmp(l.modules[j].key,l.modules[i].key)<0)rank++;l.modules[i].id=rank+1;id++;}(void)id;
  if(!l.changed){*out=copy(entry,strlen(entry));if(!*out)l.error="module_allocation_failed";}
  else {assemble_module(&l,&l.modules[0],1);for(unsigned next=1;next<l.count&&!l.error;next++)for(size_t i=1;i<l.count;i++)if(l.modules[i].visit==2&&l.modules[i].id==next){emit(&l,"\n",1);assemble_module(&l,&l.modules[i],0);}if(!l.error){*out=l.out;l.out=NULL;}}
 }if(l.error){free(*out);*out=NULL;*diag=diagnostic(l.error);}release(&l);free(l.out);return *out?0:1;}
static char *read_at(int fd,const char *name){int file=openat(fd,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(file<0)return NULL;struct stat st;if(fstat(file,&st)||!S_ISREG(st.st_mode)||st.st_size<0||st.st_size>MAX_SOURCE){close(file);return NULL;}char *out=malloc((size_t)st.st_size+1);if(!out){close(file);return NULL;}size_t n=0;while(n<(size_t)st.st_size){ssize_t got=read(file,out+n,(size_t)st.st_size-n);if(got<0&&errno==EINTR)continue;if(got<=0){free(out);close(file);return NULL;}n+=(size_t)got;}close(file);out[n]=0;if(memchr(out,0,n)){free(out);return NULL;}return out;}
int apx_modules_load_file(const char *entry_path,char **out,char **diag){*out=NULL;*diag=NULL;char *path=copy(entry_path,strlen(entry_path));if(!path){*diag=diagnostic("module_allocation_failed");return 1;}char *slash=strrchr(path,'/');const char *dir=".",*entry=path;if(slash){*slash=0;dir=*path?path:"/";entry=slash+1;}int fd=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0){free(path);*diag=diagnostic("module_directory_unreadable");return 1;}
 char *source=read_at(fd,entry);ApxModuleSource sources[MAX_MODULES]={0};size_t count=0;const char *error=source?NULL:"module_source_unreadable";
 /* Discover the graph using bounded scans, without executing compiler code. */
 for(size_t current=0;!error&&current<=count;current++){Link scan_link={0};Module mod={0};mod.source=current?sources[current-1].source:source;if(!scan(&scan_link,&mod)){error=scan_link.error;}
  for(size_t i=0;i<mod.imports_n&&!error;i++){const char *key=mod.imports[i].key;size_t j;for(j=0;j<count;j++)if(!strcmp(sources[j].name,key))break;if(j<count)continue;if(count+1>=MAX_MODULES){error="module_count_limit";break;}char name[256];if(snprintf(name,sizeof name,"%s.apx",key)>=(int)sizeof name){error="invalid_module_key";break;}char *body=read_at(fd,name);if(!body){error="module_source_unreadable";break;}sources[count].name=copy(key,strlen(key));sources[count].source=body;count++;}
  free(mod.name);for(size_t i=0;i<mod.symbols_n;i++)free(mod.symbols[i].name);for(size_t i=0;i<mod.imports_n;i++){free(mod.imports[i].alias);free(mod.imports[i].key);}for(size_t i=0;i<mod.exports_n;i++)free(mod.exports[i]);
 }close(fd);free(path);int status;if(error){*diag=diagnostic(error);status=1;}else status=apx_modules_resolve(source,sources,count,out,diag);free(source);for(size_t i=0;i<count;i++){free((char*)sources[i].name);free((char*)sources[i].source);}return status;}
