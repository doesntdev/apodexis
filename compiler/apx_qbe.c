/* Native QBE 1.3 lowering. No interpreter, bytecode or host-call fallback.
 * User identifiers only occur in integer byte data, never IL symbol names. */
#include "apx_qbe.h"
#include "../runtime/apx_runtime.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

typedef struct {
    ApxContext *ctx; ApxProgram *program; ApxBuffer data, code; unsigned data_count;
} Emitter;
typedef struct {
    Emitter *owner; ApxFunction *function; ApxBuffer body, allocations;
    unsigned temps, labels; int terminated;
    unsigned char *private_builders;
    char *used_address, *depth_address, *fuel_failure, *depth_failure;
    ApxFunction *inline_function;
    char **inline_values;
    unsigned inline_nodes, inline_sites;
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
/* Load the one live runtime state at every semantic point. Only invariant
 * addresses are hoisted; never cache counters across user/runtime/host calls. */
static const char *state_address(FunctionEmitter *f, int depth) {
    char **address=depth?&f->depth_address:&f->used_address;
    if (!*address) {
        *address=temp(f);
        apx_buf_printf(&f->allocations,"    %s =l add $apx_execution_state, %d\n",
            *address,depth?APX_EXEC_DEPTH_OFFSET:APX_EXEC_USED_OFFSET);
    }
    return *address;
}
static void charge(FunctionEmitter *f, unsigned amount) {
    const char *address=state_address(f,0);
    char *used=temp(f), *limit=temp(f), *next=temp(f), *wrap=temp(f), *over=temp(f), *bad=temp(f), *done=label(f);
    if (!f->fuel_failure) f->fuel_failure=label(f);
    apx_buf_printf(&f->body,
        "    %s =l loadl %s\n    %s =l loadl $apx_execution_state\n"
        "    %s =l add %s, %u\n    %s =w cultl %s, %u\n"
        "    %s =w cugtl %s, %s\n    %s =w or %s, %s\n",
        used,address,limit,next,used,amount,wrap,next,amount,over,next,limit,bad,wrap,over);
    branch(f,bad,f->fuel_failure,done); start(f,done);
    apx_buf_printf(&f->body,"    storel %s, %s\n",next,address);
}
static void enter(FunctionEmitter *f) {
    const char *address=state_address(f,1);
    char *depth=temp(f), *next=temp(f), *bad=temp(f), *done=label(f);
    if (!f->depth_failure) f->depth_failure=label(f);
    apx_buf_printf(&f->body,"    %s =w loadw %s\n    %s =w add %s, 1\n"
        "    storew %s, %s\n    %s =w csgtw %s, %d\n",
        depth,address,next,depth,next,address,bad,next,APX_DEPTH);
    branch(f,bad,f->depth_failure,done); start(f,done);
}
static void leave(FunctionEmitter *f) {
    const char *address=state_address(f,1);
    char *depth=temp(f), *next=temp(f), *decrement=label(f), *done=label(f);
    apx_buf_printf(&f->body,"    %s =w loadw %s\n",depth,address);
    branch(f,depth,decrement,done); start(f,decrement);
    apx_buf_printf(&f->body,"    %s =w sub %s, 1\n    storew %s, %s\n",next,depth,next,address);
    jump(f,done); start(f,done);
}
static void return_value(FunctionEmitter *f, const char *value) {
    leave(f); apx_buf_printf(&f->body, "    ret %s\n", value); f->terminated = 1;
}
static char *slot(FunctionEmitter *f) {
    char *value = temp(f); apx_buf_printf(&f->allocations, "    %s =l alloc8 8\n", value); return value;
}
static char *binding_slot(FunctionEmitter *f, unsigned id) { return symbol(f->owner->ctx, "%s", id); }
/* Checked source types select the internal ABI. All other values, including
 * f64, retain the boxed pointer representation. Never infer types from IL text. */
static char native_type(const ApxType *type) { return type && type->kind == APX_BOOL ? 'w' : 'l'; }
static char *box(FunctionEmitter *f, const ApxType *type, const char *value) {
    if (type && type->kind == APX_I64) return call1(f, "$apx_int", 'l', 'l', value);
    if (type && type->kind == APX_BOOL) return call1(f, "$apx_bool", 'l', 'w', value);
    return (char *)value;
}
static char *unbox(FunctionEmitter *f, const ApxType *type, const char *value) {
    if (type && type->kind == APX_I64) return call1(f, "$apx_i64_raw", 'l', 'l', value);
    if (type && type->kind == APX_BOOL) return call1(f, "$apx_truth", 'w', 'l', value);
    return (char *)value;
}
static void store_typed(FunctionEmitter *f, const char *address, const char *value, const ApxType *type) {
    apx_buf_printf(&f->body, "    store%c %s, %s\n", native_type(type), value, address);
}
static char *load_typed(FunctionEmitter *f, const char *address, const ApxType *type) {
    char *value = temp(f); char width = native_type(type);
    apx_buf_printf(&f->body, "    %s =%c load%c %s\n", value, width, width, address); return value;
}
static void store(FunctionEmitter *f, const char *address, const char *value) { store_typed(f, address, value, NULL); }
static char *load(FunctionEmitter *f, const char *address) { return load_typed(f, address, NULL); }
/* A private candidate must originate at a local i64 buf declaration. Inspect
 * every checked binding-ID use in the entire function (including branches and
 * unreachable suffixes); only count/cap/finish and self-assigned push may read
 * it. No alias, fork, argument, return, container or unknown operation qualifies.
 * This is deliberately not an ownership type system or a name-based pattern. */
static int i64_builder_new(ApxExpr *e) {
    return e && e->kind == APX_CALL_EXPR && e->opcode == 19 &&
        e->type->kind == APX_BUILDER && e->type->a->kind == APX_I64;
}
static void private_candidates(FunctionEmitter *f, ApxStmt *body) {
    for (ApxStmt *s=body;s;s=s->next) {
        if (s->kind=='L' && i64_builder_new(s->expr)) f->private_builders[s->binding]=1;
        private_candidates(f,s->body);
        for (ApxArm *a=s->arms;a;a=a->next) private_candidates(f,a->body);
    }
}
static void private_uses(FunctionEmitter *f, ApxExpr *e, int self_push, unsigned binding) {
    if (!e) return;
    if (e->kind == APX_VAR_EXPR) {
        if (f->private_builders[e->binding]) f->private_builders[e->binding]=2;
        return;
    }
    ApxArg *a=e->args;
    if (e->kind==APX_CALL_EXPR && a && a->expr->kind==APX_VAR_EXPR &&
        ((e->opcode>=21 && e->opcode<=23) ||
         (e->opcode==20 && self_push && a->expr->binding==binding))) {
        /* Only the first direct operand is a permitted builder observation. */
        a=a->next;
    }
    for (;a;a=a->next) private_uses(f,a->expr,0,0);
}
static void private_check(FunctionEmitter *f, ApxStmt *body) {
    for (ApxStmt *s=body;s;s=s->next) {
        if ((s->kind=='L'||s->kind=='S') && f->private_builders[s->binding] &&
            !((s->kind=='L' && i64_builder_new(s->expr)) ||
              (s->kind=='S' && s->expr->kind==APX_CALL_EXPR && s->expr->opcode==20 &&
               s->expr->args->expr->kind==APX_VAR_EXPR && s->expr->args->expr->binding==s->binding)))
            f->private_builders[s->binding]=2;
        private_uses(f,s->expr,s->kind=='S',s->binding);
        private_check(f,s->body);
        for (ApxArm *a=s->arms;a;a=a->next) private_check(f,a->body);
    }
}
/* Finite, intentionally narrow leaf rule. No user calls (hence no cycles),
 * containers, effects, control flow, or locals; only scalar parameters/literals
 * and existing checked arithmetic/comparisons/eager boolean operations. */
enum { INLINE_EXPRESSION_NODES=32, INLINE_PARAMETERS=16, INLINE_TOTAL_NODES=256, INLINE_SITES=32 };
static int scalar_type(const ApxType *t) { return t && (t->kind==APX_I64 || t->kind==APX_BOOL); }
static int leaf_expression(ApxFunction *callee, ApxExpr *e, unsigned *nodes) {
    if (!e || !scalar_type(e->type) || ++*nodes>INLINE_EXPRESSION_NODES) return 0;
    switch (e->kind) {
    case APX_INT_EXPR: case APX_BOOL_EXPR: return 1;
    case APX_VAR_EXPR:
        for (ApxField *p=callee->params;p;p=p->next) if (p->binding==e->binding) return 1;
        return 0;
    case APX_CALL_EXPR:
        if (e->function || e->record || e->opcode<1 || e->opcode>14) return 0;
        for (ApxArg *a=e->args;a;a=a->next) if (!leaf_expression(callee,a->expr,nodes)) return 0;
        return 1;
    default: return 0;
    }
}
static unsigned inline_cost(ApxFunction *callee) {
    unsigned nodes=0, params=0;
    if (callee->is_test || !scalar_type(callee->returns) || !callee->body ||
        callee->body->kind!='R' || callee->body->next) return 0;
    for (ApxField *p=callee->params;p;p=p->next)
        if (!scalar_type(p->type) || ++params>INLINE_PARAMETERS) return 0;
    return leaf_expression(callee,callee->body->expr,&nodes)?nodes:0;
}
static char *expression(FunctionEmitter *f, ApxExpr *e) {
    Emitter *owner = f->owner; ApxArg *a; size_t i; char **values, *result;
    switch (e->kind) {
    case APX_INT_EXPR: return number(f, e->integer);
    case APX_FLOAT_EXPR: {
        char text[64]; snprintf(text, sizeof text, "d_%.17g", e->real);
        return call1(f, "$apx_float", 'l', 'd', text);
    }
    case APX_BOOL_EXPR: return e->integer ? "1" : "0";
    case APX_STRING_EXPR: return call2(f, "$apx_string", 'l', data_bytes(owner, e->text, e->length, NULL, 0), number(f, (int64_t)e->length));
    case APX_VAR_EXPR:
        if (f->inline_function) {
            size_t index=0;
            for (ApxField *p=f->inline_function->params;p;p=p->next,++index)
                if (p->binding==e->binding) return f->inline_values[index];
            apx_fail(owner->ctx,"E101","codegen","unsupported_native_construct",e->span,"invalid inline parameter binding");
        }
        return load_typed(f, binding_slot(f, e->binding), e->type);
    case APX_ARRAY_EXPR: {
        int packed=e->type->a->kind==APX_I64;
        values = apx_alloc(owner->ctx, (e->count ? e->count : 1) * sizeof *values);
        for (a = e->args, i = 0; a; a = a->next, ++i) {
            char *v=expression(f,a->expr); values[i]=packed?v:box(f,a->expr->type,v);
        }
        result = call1(f, packed?"$apx_array_i64_new":"$apx_array_new", 'l', 'l', number(f, (int64_t)e->count));
        for (i = 0; i < e->count; ++i) { ApxBuffer args; apx_buf_init(&args, owner->ctx);
            apx_buf_printf(&args, "l %s, l %zu, l %s", result, i, values[i]); call(f, packed?"$apx_array_i64_put":"$apx_array_put", 0, args.data);
        }
        return result;
    }
    case APX_CALL_EXPR: break;
    default: apx_fail(owner->ctx, "E101", "codegen", "unsupported_native_construct", e->span, "non-executable expression reached native emitter"); return NULL;
    }
    if (e->opcode == 18) {
        ApxType *receiver = e->args->expr->type;
        ApxDecl *record = receiver->kind == APX_NAMED ? apx_find_decl(owner->program, receiver->name) : NULL;
        ApxField *field = record ? record->fields : NULL; size_t index = 0;
        for (; field; field = field->next, ++index)
            if (!strcmp(field->name, e->args->next->expr->text)) break;
        if (!field) apx_fail(owner->ctx, "E101", "codegen", "unsupported_native_construct", e->span, "field read has no checked record position");
        char *value = expression(f, e->args->expr);
        return unbox(f, e->type, call2(f, "$apx_field_index", 'l', value, number(f, (int64_t)index)));
    }
    if (e->opcode == 19) {
        char *capacity = box(f, e->args->next->expr->type, expression(f, e->args->next->expr)); ApxBuffer args; apx_buf_init(&args, owner->ctx);
        apx_buf_printf(&args, "w 19, l %s, l 0", capacity); return call(f, "$apx_builtin", 'l', args.data);
    }
    if (e->opcode == 43) return call(f,"$apx_builtin",'l',"w 43, l 0, l 0");
    /* Evaluate positional function arguments and named constructor fields in
       textual order. Only constructors reorder the resulting registers. */
    values = apx_alloc(owner->ctx, (e->count ? e->count : 1) * sizeof *values);
    for (a = e->args, i = 0; a; a = a->next, ++i) values[i] = expression(f, a->expr);
    /* All operands have already executed, even for & and |. Scalar operations
     * only consume registers; checked arithmetic traps at the original point. */
    if (e->opcode >= 1 && e->opcode <= 5 && e->type->kind == APX_I64) {
        ApxBuffer args; apx_buf_init(&args, owner->ctx);
        apx_buf_printf(&args, "w %d, l %s, l %s", e->opcode, values[0], values[1]);
        return call(f, "$apx_i64_checked", 'l', args.data);
    }
    if (e->opcode >= 6 && e->opcode <= 11 &&
        (e->args->expr->type->kind == APX_I64 || e->args->expr->type->kind == APX_BOOL)) {
        static const char *const compare[] = {"ceq", "cne", "cslt", "csle", "csgt", "csge"};
        result = temp(f);
        apx_buf_printf(&f->body, "    %s =w %s%c %s, %s\n", result, compare[e->opcode - 6], native_type(e->args->expr->type), values[0], values[1]);
        return result;
    }
    if (e->opcode >= 12 && e->opcode <= 14) {
        result = temp(f);
        apx_buf_printf(&f->body, "    %s =w %s %s, %s\n", result,
            e->opcode == 12 ? "xor" : e->opcode == 13 ? "and" : "or", values[0], e->opcode == 12 ? "1" : values[1]);
        return result;
    }
    if (e->opcode==17 && e->type->kind==APX_I64)
        return call2(f,"$apx_array_i64_at",'l',values[0],values[1]);
    if (e->opcode==17)
        return unbox(f,e->type,call2(f,"$apx_at_raw",'l',values[0],values[1]));
    if (e->opcode==20 && e->args->expr->kind==APX_VAR_EXPR &&
        f->private_builders[e->args->expr->binding]==1)
        return call2(f,"$apx_builder_i64_private_push",'l',values[0],values[1]);
    /* User functions use their checked native ABI. Everything else below is a
     * boxed runtime/container boundary, including casts and host operations. */
    if (!e->function)
        for (a = e->args, i = 0; a; a = a->next, ++i) values[i] = box(f, a->expr->type, values[i]);
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
        if(e->count>2){apx_buf_printf(&args,", l %s",values[2]);return unbox(f, e->type, call(f,"$apx_builtin3",'l',args.data));}
        return unbox(f, e->type, call(f, "$apx_builtin", 'l', args.data));
    }
    if (e->function) {
        unsigned cost=inline_cost(e->function);
        if (cost && f->inline_sites<INLINE_SITES && cost<=INLINE_TOTAL_NODES-f->inline_nodes) {
            /* Arguments above already ran once in caller context, before enter.
             * Substitute registers by checked callee parameter ID, never %sN
             * slots, since those IDs are local to each source function. */
            ApxFunction *saved_function=f->inline_function; char **saved_values=f->inline_values;
            ++f->inline_sites; f->inline_nodes+=cost;
            enter(f); charge(f,1);
            f->inline_function=e->function; f->inline_values=values;
            result=expression(f,e->function->body->expr);
            f->inline_function=saved_function; f->inline_values=saved_values;
            leave(f); return result;
        }
        ApxBuffer args; ApxField *p; size_t param_index = 0;
        apx_buf_init(&args, owner->ctx);
        for (p = e->function->params; p; p = p->next, ++param_index) {
            if (param_index) apx_buf_put(&args, ", ");
            apx_buf_printf(&args, "%c %s", native_type(p->type), values[param_index]);
        }
        apx_buf_put(&args, ""); return call(f, symbol(owner->ctx, "$f", e->function->index), native_type(e->function->returns), args.data);
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
    charge(f,1);
    switch (s->kind) {
    case 'L': case 'S': {
        char *value;
        if (s->kind=='L' && f->private_builders[s->binding]==1 && i64_builder_new(s->expr))
            value=call1(f,"$apx_builder_i64_private_new",'l','l',expression(f,s->expr->args->next->expr));
        else value=expression(f,s->expr);
        store_typed(f, binding_slot(f, s->binding), value, s->expr->type); break;
    }
    case 'R': return_value(f, expression(f, s->expr)); break;
    case 'A': call1(f, "$apx_assert_raw", 0, 'w', expression(f, s->expr)); break;
    case 'Z': call1(f, "$apx_trap_value", 0, 'l', expression(f,s->expr)); apx_buf_put(&f->body, "    ret 0\n"); f->terminated = 1; break;
    case '?': {
        char *yes = label(f), *done = label(f); char *value = expression(f, s->expr);
        char *condition = value;
        branch(f, condition, yes, done); start(f, yes); block(f, s->body); jump(f, done); start(f, done); break;
    }
    case 'W': {
        char *condition_label = label(f), *yes = label(f), *done = label(f), *value, *condition;
        jump(f, condition_label); start(f, condition_label); value = expression(f, s->expr);
        condition = value; branch(f, condition, yes, done); start(f, yes);
        charge(f,2); block(f, s->body); jump(f, condition_label); start(f, done); break;
    }
    case 'E': {
        char *array = expression(f, s->expr), *count = call1(f, "$apx_length_raw", 'l', 'l', array), *index = slot(f);
        char *condition_label = label(f), *yes = label(f), *done = label(f), *i, *condition, *value;
        /* Immutable iterable snapshot, independent of later binding assignments. */
        store(f, index, "0"); jump(f, condition_label); start(f, condition_label); i = load(f, index); condition = temp(f);
        apx_buf_printf(&f->body, "    %s =w csltl %s, %s\n", condition, i, count);
        branch(f, condition, yes, done); start(f, yes); charge(f,2);
        value = s->expr->type->a->kind==APX_I64 ? call2(f,"$apx_array_i64_at",'l',array,i) :
            unbox(f, s->expr->type->a, call2(f, "$apx_at_raw", 'l', array, i));
        store_typed(f, binding_slot(f, s->binding), value, s->expr->type->a); block(f, s->body);
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
            if (a->bindings) { char *payload = unbox(f, a->bindings->type, call1(f, "$apx_payload", 'l', 'l', value)); store_typed(f, binding_slot(f, a->bindings->binding), payload, a->bindings->type); }
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
    f.private_builders=apx_alloc(owner->ctx,function->bindings?function->bindings:1);
    private_candidates(&f,function->body); private_check(&f,function->body);
    for (i = 0; i < function->bindings; ++i) apx_buf_printf(&f.allocations, "    %%s%u =l alloc8 8\n", i);
    enter(&f);
    for (p = function->params, i = 0; p; p = p->next, ++i) store_typed(&f, binding_slot(&f, p->binding), symbol(owner->ctx, "%p", i), p->type);
    block(&f, function->body);
    if (!f.terminated) {
        if (function->is_test) return_value(&f, "0");
        else { call(&f, "$apx_missing_return", 0, ""); apx_buf_put(&f.body, "    ret 0\n"); }
    }
    if (f.fuel_failure) { start(&f,f.fuel_failure); call(&f,"$apx_fuel_exhausted",0,""); apx_buf_put(&f.body,"    hlt\n"); }
    if (f.depth_failure) { start(&f,f.depth_failure); call(&f,"$apx_depth_exhausted",0,""); apx_buf_put(&f.body,"    hlt\n"); }
    name = symbol(owner->ctx, function->is_test ? "$t" : "$f", function->index);
    apx_buf_printf(&owner->code, "function %c %s(", function->is_test ? 'l' : native_type(function->returns), name);
    for (p = function->params, i = 0; p; p = p->next, ++i) { if (i) apx_buf_put(&owner->code, ", "); apx_buf_printf(&owner->code, "%c %%p%u", native_type(p->type), i); }
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
/* schema_type above remains the authority for ABI eligibility/depth/diagnostics.
 * Emit descriptors only AFTER it succeeds, never as a substitute for that walk.
 * Every descriptor is six 8-byte slots; fields are pointer pairs. */
static char *schema_descriptor(Emitter *e, ApxType *type) {
    const char *name="0", *item="0", *error="0", *fields="0";
    uint64_t kind=APX_SCHEMA_NULL; size_t count=0;
    if (type) switch(type->kind) {
    case APX_I64: kind=APX_SCHEMA_I64; break;
    case APX_F64: kind=APX_SCHEMA_F64; break;
    case APX_BOOL: kind=APX_SCHEMA_BOOL; break;
    case APX_STR: kind=APX_SCHEMA_STR; break;
    case APX_ARRAY: case APX_MAP: case APX_OPTION: case APX_RESULT:
        kind=type->kind==APX_ARRAY?APX_SCHEMA_ARRAY:type->kind==APX_MAP?APX_SCHEMA_MAP:
            type->kind==APX_OPTION?APX_SCHEMA_OPTION:APX_SCHEMA_RESULT;
        item=schema_descriptor(e,type->a);
        if (type->kind==APX_RESULT) error=schema_descriptor(e,type->b);
        break;
    case APX_NAMED: {
        ApxDecl *d=apx_find_decl(e->program,type->name); ApxField *f; ApxBuffer pairs;
        kind=APX_SCHEMA_STRUCT; name=data_string(e,d->name); apx_buf_init(&pairs,e->ctx);
        for(f=d->fields;f;f=f->next) {
            char *field_name=data_string(e,f->name), *child=schema_descriptor(e,f->type);
            apx_buf_printf(&pairs,"l %s, l %s, ",field_name,child); ++count;
        }
        if(count) {
            fields=symbol(e->ctx,"$d",e->data_count++);
            apx_buf_printf(&e->data,"section \".rodata\" data %s = align 8 { %s }\n",fields,pairs.data);
        }
        break;
    }
    default: apx_fail(e->ctx,"E101","codegen","unsupported_native_construct",no_span,"invalid checked input schema");
    }
    char *descriptor=symbol(e->ctx,"$d",e->data_count++);
    apx_buf_printf(&e->data,"section \".rodata\" data %s = align 8 { l %"PRIu64", l %s, l %zu, l %s, l %s, l %s }\n",
        descriptor,kind,name,count,item,error,fields);
    return descriptor;
}
static char *emit_program(ApxContext *ctx, ApxProgram *program,int tests_only) {
    Emitter e; ApxBuffer schema, result; ApxFunction *main = apx_find_function(program, "main"), *f; size_t test_count = 0, i; char **names;
    if (!tests_only&&!main) apx_fail(ctx, "N101", "resolve", "unknown_symbol", no_span, "missing main entrypoint");
    if (!tests_only&&main->params && main->params->next) apx_fail(ctx, "A104", "abi", "invalid_entrypoint", main->span, "main must have zero or one parameter");
    memset(&e, 0, sizeof e); e.ctx = ctx; e.program = program; apx_buf_init(&e.data, ctx); apx_buf_init(&e.code, ctx); apx_buf_init(&schema, ctx);
    if (!tests_only&&main->params) schema_type(&e, &schema, main->params->type, 0, main->span, 0); else apx_buf_put(&schema, "null");
    data_bytes(&e, schema.data, schema.length, "$apx_schema_json", 1);
    char *descriptor=tests_only?NULL:schema_descriptor(&e,main->params?main->params->type:NULL);
    data_bytes(&e, program->source_hash, strlen(program->source_hash), "$apx_source_hash", 1);
    data_bytes(&e, APX_LANGUAGE_VERSION, strlen(APX_LANGUAGE_VERSION), "$apx_language_version", 1);
    for (f = program->functions; f; f = f->next) emit_function(&e, f);
    for (f = program->tests; f; f = f->next) ++test_count;
    names = apx_alloc(ctx, (test_count ? test_count : 1) * sizeof *names);
    for (f = program->tests, i = 0; f; f = f->next, ++i) { names[i] = data_string(&e, f->name); emit_function(&e, f); }
    if(tests_only){
        char *message=data_string(&e,"test artifact has no host entrypoint");
        apx_buf_printf(&e.code,"export function l $apx_entry(l %%input) {\n@start\n    call $apx_trap(l %s)\n    ret 0\n}\n\n",message);
    } else {
        FunctionEmitter wrapper; ApxBuffer args; char *validated, *answer;
        memset(&wrapper, 0, sizeof wrapper); wrapper.owner = &e;
        apx_buf_init(&wrapper.body, ctx); apx_buf_init(&args, ctx);
        validated = call2(&wrapper, "$apx_validate_static", 'l', descriptor, "%input");
        if (main->params) {
            char *input = unbox(&wrapper, main->params->type, validated);
            apx_buf_printf(&args, "%c %s", native_type(main->params->type), input);
        } else apx_buf_put(&args, "");
        answer = call(&wrapper, symbol(ctx, "$f", main->index), native_type(main->returns), args.data);
        answer = box(&wrapper, main->returns, answer);
        apx_buf_put(&e.code, "export function l $apx_entry(l %input) {\n@start\n");
        apx_buf_bytes(&e.code, wrapper.body.data, wrapper.body.length);
        apx_buf_printf(&e.code, "    ret %s\n}\n\n", answer);
    }
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
