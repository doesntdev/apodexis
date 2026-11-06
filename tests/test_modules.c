#include "../compiler/apx_modules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void require(int ok,const char *name){checks++;if(!ok){fprintf(stderr,"modules FAIL: %s\n",name);exit(1);}}
static void rejected(const char *source,ApxModuleSource *modules,size_t count,const char *kind){char *out=NULL,*diag=NULL;require(apx_modules_resolve(source,modules,count,&out,&diag)!=0&&!out&&diag&&strstr(diag,kind),kind);free(out);free(diag);}
int main(void){
 const char *entry="M app\nI u \"util\"\nF main input:u::Point->i64\n  R (u::double input.x)\nX own\n  A (= (u::double 2) 4)\n";
 ApxModuleSource modules[]={{"util","M util\nV double\nV Point\nT Point struct\n  x:i64\nF private value:i64->i64\n  R (* value 2)\nF double value:i64->i64\n  R (private value)\nX local\n  A (= (double 1) 2)\n"}};
 char *out=NULL,*diag=NULL;require(!apx_modules_resolve(entry,modules,1,&out,&diag)&&out&&!diag,"bundle resolves");require(strstr(out,"F __apx_m1_double")&&strstr(out,"( __apx")==NULL&&strstr(out,"(__apx_m1_double input.x)")&&strstr(out,"input:__apx_m1_Point"),"qualified types and calls linked");require(strstr(out,"(__apx_m1_private value)")&&strstr(out,"X __apx_m1_local"),"private internal call and imported tests linked");free(out);
 const char *legacy="M old\nF main->str\n  R \"(double) u::Point ; keep\"\n";require(!apx_modules_resolve(legacy,NULL,0,&out,&diag)&&!strcmp(out,legacy),"single source exact passthrough");free(out);
 rejected("M x\nI u \"util\"\nF main->i64\n  R (u::private 2)\n",modules,1,"private_module_symbol");
 rejected("M x\nI u \"missing\"\nF main->i64\n  R 0\n",modules,1,"missing_module");
 rejected("M x\nI u \"../util\"\nF main->i64\n  R 0\n",modules,1,"invalid_module_key");
 rejected("M x\nI u \"util\"\nI u \"util\"\nF main->i64\n  R 0\n",modules,1,"duplicate_import_alias");
 rejected("M x\nI u \"util\"\nF main->i64\n  R (z::double 2)\n",modules,1,"unknown_import_alias");
 ApxModuleSource cyclic[]={{"a","M a\nI b \"b\"\nV f\nF f->i64\n  R 0\n"},{"b","M b\nI a \"a\"\nV f\nF f->i64\n  R 0\n"}};rejected("M x\nI a \"a\"\nF main->i64\n  R 0\n",cyclic,2,"module_cycle");
 ApxModuleSource wrong[]={{"util","M incorrect\nF f->i64\n  R 0\n"}};rejected(entry,wrong,1,"module_name_mismatch");
 ApxModuleSource dup[]={{"util",modules[0].source},{"util",modules[0].source}};rejected(entry,dup,2,"duplicate_module_key");
 ApxModuleSource badexport[]={{"util","M util\nV nonexistent\nF f->i64\n  R 0\n"}};rejected(entry,badexport,1,"invalid_export");
 rejected("M x\nI u \"util\"\nF __apx_m1_double->i64\n  R 0\n",modules,1,"reserved_module_symbol");
 rejected("M x\nI u \"util\"\nF main->i64\n  R (__apx_m1_private 2)\n",modules,1,"reserved_module_symbol");
 ApxModuleSource reordered[]={{"z","M z\nV f\nF f->i64\n  R 2\n"},{"a","M a\nV f\nF f->i64\n  R 1\n"}};
 const char *ordered="M app\nI z \"z\"\nI a \"a\"\nF main->i64\n  R (+ (z::f) (a::f))\n";
 char *left=NULL,*right=NULL;require(!apx_modules_resolve(ordered,reordered,2,&left,&diag),"two dependency keys resolve");
 ApxModuleSource reversed[]={reordered[1],reordered[0]};require(!apx_modules_resolve(ordered,reversed,2,&right,&diag)&&!strcmp(left,right),"inline module object order cannot change bundled identity");free(left);free(right);
 const char *comments="M app\nI u \"util\"\nF main->str\n  R \"u::private __apx_m1_private\" ; u::private __apx_m1_private\n";
 require(!apx_modules_resolve(comments,modules,1,&out,&diag)&&strstr(out,"u::private __apx_m1_private"),"strings/comments are not symbol references");free(out);
 /* Type-marker consumers must participate in module qualification. */
 ApxModuleSource decode_modules[]={{"shared","M shared\nV Row\nV decode\nV payload\nT Row struct\n  id:i64\nT Secret struct\n  hidden:i64\nF decode text:str->result Row str\n  R (json_decode Row text)\nF payload->str\n  R \"{\\\"id\\\":7}\"\n"}};
 const char *decode_entry="M entry\nI shared \"shared\"\nF main doc:str->result shared::Row str\n  R (json_decode shared::Row doc)\n";
 require(!apx_modules_resolve(decode_entry,decode_modules,1,&out,&diag)&&out&&!diag,"qualified json_decode target resolves");
 require(strstr(out,"(json_decode __apx_m1_Row doc)")&&strstr(out,"(json_decode __apx_m1_Row text)")&&strstr(out,"->result __apx_m1_Row str"),"entry and imported decoder type markers both renamed");free(out);
 const char *imported_decode="M entry\nI shared \"shared\"\nF main doc:str->result shared::Row str\n  R (shared::decode doc)\n";
 require(!apx_modules_resolve(imported_decode,decode_modules,1,&out,&diag)&&out&&!diag,"imported json_decode function resolves");
 require(strstr(out,"(__apx_m1_decode doc)")&&strstr(out,"(json_decode __apx_m1_Row text)"),"imported decoder keeps callable and type roles distinct");free(out);
 const char *nested_decode="M entry\nI shared \"shared\"\nF main doc:str->result [shared::Row] str\n  L a (json_decode [shared::Row] doc)\n  L b (json_decode map shared::Row doc)\n  R a\n";
 require(!apx_modules_resolve(nested_decode,decode_modules,1,&out,&diag)&&out&&!diag,"nested json_decode target resolves");
 require(strstr(out,"(json_decode [__apx_m1_Row] doc)")&&strstr(out,"(json_decode map __apx_m1_Row doc)"),"array and map decoder markers recursively qualified");free(out);
 const char *decode_value="M entry\nI shared \"shared\"\nF main->result shared::Row str\n  R (json_decode shared::Row (shared::payload))\n";
 require(!apx_modules_resolve(decode_value,decode_modules,1,&out,&diag)&&out&&!diag,"decoder text remains a value expression");
 require(strstr(out,"(json_decode __apx_m1_Row (__apx_m1_payload))")!=NULL,"only the marker uses type-reference rules");free(out);
 rejected("M entry\nI shared \"shared\"\nF main doc:str->str\n  R (json_decode shared::Secret doc)\n",decode_modules,1,"private_module_symbol");
 rejected("M entry\nI shared \"shared\"\nF main doc:str->str\n  R (json_decode shared::decode doc)\n",decode_modules,1,"invalid_module_type");
 const char *marker_text="M entry\nI shared \"shared\"\nF main->str\n  R \"(json_decode shared::Secret text)\" ; (json_decode shared::Secret text)\n";
 require(!apx_modules_resolve(marker_text,decode_modules,1,&out,&diag)&&out&&!diag&&strstr(out,"\"(json_decode shared::Secret text)\" ; (json_decode shared::Secret text)"),"decoder-looking strings and comments stay literal");free(out);
 const char *marker_controls="M entry\nI shared \"shared\"\nF main->i64\n  L a:builder shared::Row (buf shared::Row 2)\n  L b:map shared::Row (map shared::Row)\n  R 0\n";
 require(!apx_modules_resolve(marker_controls,decode_modules,1,&out,&diag)&&out&&!diag&&strstr(out,"(buf __apx_m1_Row 2)")&&strstr(out,"(map __apx_m1_Row)"),"existing buf and map marker handling retained");free(out);
 printf("native module resolver: %u checks PASS\n",checks);return 0;
}
