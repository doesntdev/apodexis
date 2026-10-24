/* Native QBE 1.3 lowering. No interpreter, bytecode or host-call fallback.
 * User identifiers only occur in integer byte data, never IL symbol names. */
#include "apx_qbe.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

typedef struct {
    ApxContext *ctx; ApxProgram *program; ApxBuffer data, code; unsigned data_count;
} Emitter;
typedef struct {
    Emitter *owner; ApxFunction *function; ApxBuffer body, allocations;
    unsigned temps, labels; int terminated;
} FunctionEmitter;
static const ApxSpan no_span = {0, 0};
static char *symbol(ApxContext *ctx, const char *prefix, unsigned id) {
    char text[64]; snprintf(text, sizeof text, "%s%u", prefix, id); return apx_copy(ctx, text, strlen(text));
}
static char *data_bytes(Emitter *e, const char *bytes, size_t n, const char *name, int exported) {
    size_t i; char *label;
    label = name ? apx_copy(e->ctx, name, strlen(name)) : symbol(e->ctx, "$d", e->data_count++);
    apx_buf_printf(&e->data, "%sdata %s = align 8 { ", exported ? "export " : "", label);
    for (i = 0; i < n; ++i) apx_buf_printf(&e->data, "b %u, ", (unsigned char)bytes[i]);
    apx_buf_put(&e->data, "b 0 }\n"); return label;
}
static char *data_string(Emitter *e, const char *bytes) { return data_bytes(e, bytes, strlen(bytes), NULL, 0); }
static char *temp(FunctionEmitter *f) { return symbol(f->owner->ctx, "%v", ++f->temps); }
static char *label(FunctionEmitter *f) { return symbol(f->owner->ctx, "@b", ++f->labels); }
static char *number(FunctionEmitter *f, int64_t value) {
    char text[64]; snprintf(text, sizeof text, "%" PRId64, value); return apx_copy(f->owner->ctx, text, strlen(text));
}
static char *call(FunctionEmitter *f, const char *name, char result, const char *arguments) {
    char *value = NULL;
    if (result) { value = temp(f); apx_buf_printf(&f->body, "    %s =%c call %s(%s)\n", value, result, name, arguments); }
    else apx_buf_printf(&f->body, "    call %s(%s)\n", name, arguments);
    return value;
}
static char *call1(FunctionEmitter *f, const char *name, char result, char argument_type, const char *argument) {
    ApxBuffer args; apx_buf_init(&args, f->owner->ctx); apx_buf_printf(&args, "%c %s", argument_type, argument); return call(f, name, result, args.data);
}
static char *call2(FunctionEmitter *f, const char *name, char result, const char *a, const char *b) {
    ApxBuffer args; apx_buf_init(&args, f->owner->ctx); apx_buf_printf(&args, "l %s, l %s", a, b); return call(f, name, result, args.data);
}
static void start(FunctionEmitter *f, const char *block) { apx_buf_printf(&f->body, "%s\n", block); f->terminated = 0; }
static void jump(FunctionEmitter *f, const char *block) {
    if (!f->terminated) { apx_buf_printf(&f->body, "    jmp %s\n", block); f->terminated = 1; }
}
static void branch(FunctionEmitter *f, const char *condition, const char *yes, const char *no) {
    apx_buf_printf(&f->body, "    jnz %s, %s, %s\n", condition, yes, no); f->terminated = 1;
}
static void return_value(FunctionEmitter *f, const char *value) {
    call(f, "$apx_leave", 0, ""); apx_buf_printf(&f->body, "    ret %s\n", value); f->terminated = 1;
}
static char *slot(FunctionEmitter *f) {
    char *value = temp(f); apx_buf_printf(&f->allocations, "    %s =l alloc8 8\n", value); return value;
}
static char *binding_slot(FunctionEmitter *f, unsigned id) { return symbol(f->owner->ctx, "%s", id); }
static void store(FunctionEmitter *f, const char *address, const char *value) { apx_buf_printf(&f->body, "    storel %s, %s\n", value, address); }
static char *load(FunctionEmitter *f, const char *address) {
    char *value = temp(f); apx_buf_printf(&f->body, "    %s =l loadl %s\n", value, address); return value;
}
static char *expression(FunctionEmitter *f, ApxExpr *e) {
    Emitter *owner = f->owner; ApxArg *a; size_t i; char **values, *result;
    switch (e->kind) {
    case APX_INT_EXPR: return call1(f, "$apx_int", 'l', 'l', number(f, e->integer));
    case APX_FLOAT_EXPR: {
        char text[64]; snprintf(text, sizeof text, "d_%.17g", e->real);
        return call1(f, "$apx_float", 'l', 'd', text);
    }
    case APX_BOOL_EXPR: return call1(f, "$apx_bool", 'l', 'w', e->integer ? "1" : "0");
    case APX_STRING_EXPR: return call2(f, "$apx_string", 'l', data_bytes(owner, e->text, e->length, NULL, 0), number(f, (int64_t)e->length));
    case APX_VAR_EXPR: return load(f, binding_slot(f, e->binding));
    case APX_ARRAY_EXPR:
        values = apx_alloc(owner->ctx, (e->count ? e->count : 1) * sizeof *values);
        for (a = e->args, i = 0; a; a = a->next, ++i) values[i] = expression(f, a->expr);
        result = call1(f, "$apx_array_new", 'l', 'l', number(f, (int64_t)e->count));
        for (i = 0; i < e->count; ++i) { ApxBuffer args; apx_buf_init(&args, owner->ctx);
            apx_buf_printf(&args, "l %s, l %zu, l %s", result, i, values[i]); call(f, "$apx_array_put", 0, args.data);
        }
        return result;
    case APX_CALL_EXPR: break;
    default: apx_fail(owner->ctx, "E101", "codegen", "unsupported_native_construct", e->span, "non-executable expression reached native emitter"); return NULL;
    }
    if (e->opcode == 18) {
        char *value = expression(f, e->args->expr), *name = data_string(owner, e->args->next->expr->text);
        return call2(f, "$apx_field", 'l', value, name);
    }
    if (e->opcode == 19) {
        char *capacity = expression(f, e->args->next->expr); ApxBuffer args; apx_buf_init(&args, owner->ctx);
        apx_buf_printf(&args, "w 19, l %s, l 0", capacity); return call(f, "$apx_builtin", 'l', args.data);
    }
    if (e->opcode == 43) return call(f,"$apx_builtin",'l',"w 43, l 0, l 0");
    /* Evaluate positional function arguments and named constructor fields in
       textual order. Only constructors reorder the resulting registers. */
    values = apx_alloc(owner->ctx, (e->count ? e->count : 1) * sizeof *values);
    for (a = e->args, i = 0; a; a = a->next, ++i) values[i] = expression(f, a->expr);
    if(e->opcode==54){
        ApxField *field; size_t index;
        result=call1(f,"$apx_record_copy",'l','l',values[0]);
        for(a=e->args->next,i=1;a;a=a->next,++i){
            ApxBuffer args; index=0;
            for(field=e->record->fields;field;field=field->next,++index)if(!strcmp(field->name,a->name))break;
            apx_buf_init(&args,owner->ctx);
            apx_buf_printf(&args,"l %s, l %zu, l %s, l %s",result,index,data_string(owner,a->name),values[i]);
            call(f,"$apx_record_put",0,args.data);
        }
        return result;
    }
    if (e->opcode) {
        ApxBuffer args; apx_buf_init(&args, owner->ctx);
        apx_buf_printf(&args, "w %d, l %s, l %s", e->opcode, e->count ? values[0] : "0", e->count > 1 ? values[1] : "0");
        if(e->count>2){apx_buf_printf(&args,", l %s",values[2]);return call(f,"$apx_builtin3",'l',args.data);}
        return call(f, "$apx_builtin", 'l', args.data);
    }
    if (e->function) {
        ApxBuffer args; ApxField *p; size_t param_index = 0;
        apx_buf_init(&args, owner->ctx);
        for (p = e->function->params; p; p = p->next, ++param_index) {
            if (param_index) apx_buf_put(&args, ", ");
            apx_buf_printf(&args, "l %s", values[param_index]);
        }
        apx_buf_put(&args, ""); return call(f, symbol(owner->ctx, "$f", e->function->index), 'l', args.data);
    }
    if (e->record) {
        ApxField *field; size_t count = 0, field_index = 0;
        for (field = e->record->fields; field; field = field->next) ++count;
        result = call2(f, "$apx_record_new", 'l', data_string(owner, e->record->name), number(f, (int64_t)count));
        for (field = e->record->fields; field; field = field->next, ++field_index) {
            ApxBuffer args; size_t index = 0; char *name;
            for (a = e->args; a; a = a->next, ++index) if (!strcmp(a->name, field->name)) break;
            name = data_string(owner, field->name); apx_buf_init(&args, owner->ctx);
            apx_buf_printf(&args, "l %s, l %zu, l %s, l %s", result, field_index, name, values[index]);
            call(f, "$apx_record_put", 0, args.data);
        }
        return result;
    }
    apx_fail(owner->ctx, "E101", "codegen", "unsupported_native_construct", e->span, "call has no native implementation"); return NULL;
}
static void block(FunctionEmitter *f, ApxStmt *body);
static void statement(FunctionEmitter *f, ApxStmt *s) {
    Emitter *owner = f->owner;
    call1(f, "$apx_charge", 0, 'l', "1");
    switch (s->kind) {
    case 'L': case 'S': { char *value = expression(f, s->expr); store(f, binding_slot(f, s->binding), value); break; }
    case 'R': return_value(f, expression(f, s->expr)); break;
    case 'A': call1(f, "$apx_assert", 0, 'l', expression(f, s->expr)); break;
    case 'Z': call1(f, "$apx_trap_value", 0, 'l', expression(f,s->expr)); apx_buf_put(&f->body, "    ret 0\n"); f->terminated = 1; break;
    case '?': {
        char *yes = label(f), *done = label(f); char *value = expression(f, s->expr);
        char *condition = call1(f, "$apx_truth", 'w', 'l', value);
        branch(f, condition, yes, done); start(f, yes); block(f, s->body); jump(f, done); start(f, done); break;
    }
    case 'W': {
        char *condition_label = label(f), *yes = label(f), *done = label(f), *value, *condition;
        jump(f, condition_label); start(f, condition_label); value = expression(f, s->expr);
        condition = call1(f, "$apx_truth", 'w', 'l', value); branch(f, condition, yes, done); start(f, yes);
        call1(f, "$apx_charge", 0, 'l', "2"); block(f, s->body); jump(f, condition_label); start(f, done); break;
    }
    case 'E': {
        char *array = expression(f, s->expr), *count = call1(f, "$apx_length_raw", 'l', 'l', array), *index = slot(f);
        char *condition_label = label(f), *yes = label(f), *done = label(f), *i, *condition, *value;
        /* Immutable iterable snapshot, independent of later binding assignments. */
        store(f, index, "0"); jump(f, condition_label); start(f, condition_label); i = load(f, index); condition = temp(f);
        apx_buf_printf(&f->body, "    %s =w csltl %s, %s\n", condition, i, count);
        branch(f, condition, yes, done); start(f, yes); call1(f, "$apx_charge", 0, 'l', "2");
        value = call2(f, "$apx_at_raw", 'l', array, i); store(f, binding_slot(f, s->binding), value); block(f, s->body);
        if (!f->terminated) { char *increment = temp(f); apx_buf_printf(&f->body, "    %s =l add %s, 1\n", increment, i); store(f, index, increment); jump(f, condition_label); }
        start(f, done); break;
    }
    case 'C': {
        ApxArm *a; char *value, *done;
        if (s->expr->type->kind == APX_NAMED) apx_fail(owner->ctx, "E101", "codegen", "unsupported_native_construct", s->span, "custom enum execution is not implemented by the language runtime");
        value = expression(f, s->expr); done = label(f);
        for (a = s->arms; a; a = a->next) {
            char *yes = label(f), *next = label(f), *condition = call2(f, "$apx_tag_is", 'w', value, data_string(owner, a->name));
            branch(f, condition, yes, next); start(f, yes);
            if (a->bindings) { char *payload = call1(f, "$apx_payload", 'l', 'l', value); store(f, binding_slot(f, a->bindings->binding), payload); }
            block(f, a->body); jump(f, done); start(f, next);
        }
        call(f, "$apx_missing_return", 0, ""); apx_buf_put(&f->body, "    ret 0\n"); f->terminated = 1; start(f, done); break;
    }
    default: apx_fail(owner->ctx, "E101", "codegen", "unsupported_native_construct", s->span, "unknown checked statement");
    }
}
static void block(FunctionEmitter *f, ApxStmt *body) {
    ApxStmt *s; for (s = body; s && !f->terminated; s = s->next) statement(f, s);
}
static void emit_function(Emitter *owner, ApxFunction *function) {
    FunctionEmitter f; ApxField *p; unsigned i = 0; char *name;
    memset(&f, 0, sizeof f); f.owner = owner; f.function = function;
    apx_buf_init(&f.body, owner->ctx); apx_buf_init(&f.allocations, owner->ctx);
    for (i = 0; i < function->bindings; ++i) apx_buf_printf(&f.allocations, "    %%s%u =l alloc8 8\n", i);
    call(&f, "$apx_enter", 0, "");
    for (p = function->params, i = 0; p; p = p->next, ++i) store(&f, binding_slot(&f, p->binding), symbol(owner->ctx, "%p", i));
    block(&f, function->body);
    if (!f.terminated) {
        if (function->is_test) return_value(&f, "0");
        else { call(&f, "$apx_missing_return", 0, ""); apx_buf_put(&f.body, "    ret 0\n"); }
    }
    name = symbol(owner->ctx, function->is_test ? "$t" : "$f", function->index);
    apx_buf_printf(&owner->code, "function l %s(", name);
    for (p = function->params, i = 0; p; p = p->next, ++i) { if (i) apx_buf_put(&owner->code, ", "); apx_buf_printf(&owner->code, "l %%p%u", i); }
    apx_buf_put(&owner->code, ") {\n@start\n");
    if (f.allocations.data) apx_buf_bytes(&owner->code, f.allocations.data, f.allocations.length);
    if (f.body.data) apx_buf_bytes(&owner->code, f.body.data, f.body.length);
    apx_buf_put(&owner->code, "}\n\n");
}
static void schema_type(Emitter *e, ApxBuffer *b, ApxType *type, unsigned depth, ApxSpan span, int map_payload) {
    if (depth > APX_MAX_DEPTH) apx_fail(e->ctx, "T106", "type", "type_nesting_limit_exceeded", span, "input schema nesting exceeds maximum depth 64");
    if (type->kind <= APX_STR) {
        static const char *const names[] = {"i64", "f64", "bool", "str"}; apx_buf_printf(b, "{\"kind\":\"%s\"}", names[type->kind]);
    } else if (type->kind == APX_ARRAY) {
        apx_buf_put(b, "{\"kind\":\"array\",\"items\":"); schema_type(e, b, type->a, depth + 1, span, map_payload); apx_buf_put(b, "}");
    } else if (type->kind == APX_MAP) {
        apx_buf_put(b,"{\"kind\":\"map\",\"values\":"); schema_type(e,b,type->a,depth+1,span,1); apx_buf_put(b,"}");
    } else if(map_payload && (type->kind==APX_OPTION || type->kind==APX_RESULT)) {
        apx_buf_put(b,type->kind==APX_OPTION?"{\"kind\":\"option\",\"item\":" : "{\"kind\":\"result\",\"ok\":");
        schema_type(e,b,type->a,depth+1,span,1);
        if(type->kind==APX_RESULT){apx_buf_put(b,",\"error\":");schema_type(e,b,type->b,depth+1,span,1);}
        apx_buf_put(b,"}");
    } else if (type->kind == APX_NAMED) {
        ApxDecl *d = apx_find_decl(e->program, type->name); ApxField *f;
        if (!d || d->is_enum) apx_fail(e->ctx, "A104", "abi", "unsupported_input_type", span, "JSON input must use primitive, array or struct types");
        apx_buf_put(b, "{\"kind\":\"struct\",\"name\":"); apx_json_quote(b, d->name, strlen(d->name)); apx_buf_put(b, ",\"fields\":[");
        for (f = d->fields; f; f = f->next) {
            if (f != d->fields) apx_buf_put(b, ",");
            apx_buf_put(b, "{\"name\":"); apx_json_quote(b, f->name, strlen(f->name));
            apx_buf_put(b, ",\"type\":"); schema_type(e, b, f->type, depth + 1, span, map_payload); apx_buf_put(b, "}");
        }
        apx_buf_put(b, "]}");
    } else apx_fail(e->ctx, "A104", "abi", "unsupported_input_type", span, "JSON input must use primitive, array or struct types");
}
static char *emit_program(ApxContext *ctx, ApxProgram *program,int tests_only) {
    Emitter e; ApxBuffer schema, result; ApxFunction *main = apx_find_function(program, "main"), *f; size_t test_count = 0, i; char **names;
    if (!tests_only&&!main) apx_fail(ctx, "N101", "resolve", "unknown_symbol", no_span, "missing main entrypoint");
    if (!tests_only&&main->params && main->params->next) apx_fail(ctx, "A104", "abi", "invalid_entrypoint", main->span, "main must have zero or one parameter");
    memset(&e, 0, sizeof e); e.ctx = ctx; e.program = program; apx_buf_init(&e.data, ctx); apx_buf_init(&e.code, ctx); apx_buf_init(&schema, ctx);
    if (!tests_only&&main->params) schema_type(&e, &schema, main->params->type, 0, main->span, 0); else apx_buf_put(&schema, "null");
    data_bytes(&e, schema.data, schema.length, "$apx_schema_json", 1);
    data_bytes(&e, program->source_hash, strlen(program->source_hash), "$apx_source_hash", 1);
    data_bytes(&e, APX_LANGUAGE_VERSION, strlen(APX_LANGUAGE_VERSION), "$apx_language_version", 1);
    for (f = program->functions; f; f = f->next) emit_function(&e, f);
    for (f = program->tests; f; f = f->next) ++test_count;
    names = apx_alloc(ctx, (test_count ? test_count : 1) * sizeof *names);
    for (f = program->tests, i = 0; f; f = f->next, ++i) { names[i] = data_string(&e, f->name); emit_function(&e, f); }
    if(tests_only){
        char *message=data_string(&e,"test artifact has no host entrypoint");
        apx_buf_printf(&e.code,"export function l $apx_entry(l %%input) {\n@start\n    call $apx_trap(l %s)\n    ret 0\n}\n\n",message);
    }else apx_buf_printf(&e.code, "export function l $apx_entry(l %%input) {\n@start\n    %%validated =l call $apx_validate(l $apx_schema_json, l %%input)\n    %%answer =l call $f%u(%s)\n    ret %%answer\n}\n\n", main->index, main->params ? "l %validated" : "");
    apx_buf_printf(&e.code, "export function l $apx_test_count() {\n@start\n    ret %zu\n}\n\n", test_count);
    apx_buf_put(&e.code, "export function l $apx_test_name(l %index) {\n@start\n");
    for (i = 0; i < test_count; ++i) {
        apx_buf_printf(&e.code, "    %%match%zu =w ceql %%index, %zu\n    jnz %%match%zu, @yes%zu, @next%zu\n@yes%zu\n    ret %s\n@next%zu\n", i, i, i, i, i, i, names[i], i);
    }
    apx_buf_put(&e.code, "    ret 0\n}\n\nexport function l $apx_test_run(l %index) {\n@start\n");
    for (i = 0; i < test_count; ++i) {
        apx_buf_printf(&e.code, "    %%match%zu =w ceql %%index, %zu\n    jnz %%match%zu, @yes%zu, @next%zu\n@yes%zu\n    %%result%zu =l call $t%zu()\n    ret %%result%zu\n@next%zu\n", i, i, i, i, i, i, i, i, i, i);
    }
    apx_buf_put(&e.code, "    ret 0\n}\n"); apx_buf_init(&result, ctx);
    apx_buf_bytes(&result, e.data.data, e.data.length); apx_buf_put(&result, "\n"); apx_buf_bytes(&result, e.code.data, e.code.length); return result.data;
}
char *apx_qbe_emit(ApxContext *ctx,ApxProgram *program){return emit_program(ctx,program,0);}
char *apx_qbe_emit_tests(ApxContext *ctx,ApxProgram *program){return emit_program(ctx,program,1);}
