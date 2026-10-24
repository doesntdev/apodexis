/* Standalone Apodexis syntax + static semantics. C99, libc only.
 * The arena is compile-time storage, never a runtime execution environment. */
#include "apx_frontend.h"
#include "apx_qbe.h"
#include "apx_hash.h"
#include "apx_unicode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <inttypes.h>

struct ApxAllocation { struct ApxAllocation *next; };
struct ApxRecovery { jmp_buf jump; struct ApxRecovery *previous; };
struct ApxDiagnostic {
    const char *code, *phase, *kind, *message;
    ApxSpan span; ApxType *expected; ApxBinding *scope;
    struct ApxDiagnostic *next;
};
static const ApxSpan no_span = {0, 0};
static const char *const builtin_names[] = {
    "+", "-", "*", "/", "%", "=", "!=", "<", "<=", ">", ">=",
    "!", "&", "|", "cat", "#", "@", ".", "buf", "push", "finish",
    "count", "cap", "i64", "f64", "ok", "err", "some", "none",
    "str_chars", "str_char", "str_slice", "str_find", "str_split", "str_trim",
    "str_replace", "str_lower_ascii", "str_upper_ascii", "parse_i64", "parse_f64",
    "str_i64", "str_f64", "map", "map_get", "map_put", "map_remove", "map_has",
    "map_keys", "map_values", "array_get", "array_set", "array_slice", "array_cat", "with"
};
int apx_builtin_opcode(const char *name) {
    static const char *const host_names[]={"fs_read","fs_write","env_get","clock_ms","random_hex","http_get"};
    unsigned i;
    for(i=0;i<sizeof host_names/sizeof host_names[0];i++)if(!strcmp(name,host_names[i]))return 1001+(int)i;
    for (i = 0; i < sizeof builtin_names / sizeof builtin_names[0]; ++i)
        if (!strcmp(name, builtin_names[i])) return (int)i + 1;
    return 0;
}
void apx_fail(ApxContext *ctx, const char *code, const char *phase,
              const char *kind, ApxSpan span, const char *message) {
    ctx->failed = 1; ctx->error_code = code; ctx->error_phase = phase;
    ctx->error_kind = kind; ctx->error_span = span; ctx->error_message = message;
    if (ctx->collect_diagnostics && ctx->recovery &&
        (!strcmp(phase, "resolve") || !strcmp(phase, "type")))
        longjmp(ctx->recovery->jump, 1);
    longjmp(ctx->escape, 1);
}
void *apx_alloc(ApxContext *ctx, size_t size) {
    ApxAllocation *item;
    if (size > 64u * 1024u * 1024u || ctx->allocated > 64u * 1024u * 1024u - size)
        apx_fail(ctx, "P006", "compile", "compiler_resource_limit", no_span, "compiler storage limit exceeded");
    item = calloc(1, sizeof *item + (size ? size : 1));
    if (!item) apx_fail(ctx, "P006", "compile", "out_of_memory", no_span, "compiler allocation failed");
    item->next = ctx->allocations; ctx->allocations = item; ctx->allocated += size;
    return item + 1;
}
char *apx_copy(ApxContext *ctx, const char *source, size_t size) {
    char *p = apx_alloc(ctx, size + 1); memcpy(p, source, size); return p;
}
static void context_free(ApxContext *ctx) {
    ApxAllocation *p = ctx->allocations;
    while (p) { ApxAllocation *next = p->next; free(p); p = next; }
}
void apx_buf_init(ApxBuffer *b, ApxContext *ctx) { memset(b, 0, sizeof *b); b->ctx = ctx; }
void apx_buf_bytes(ApxBuffer *b, const char *text, size_t n) {
    if (n > 64u * 1024u * 1024u || b->length > 64u * 1024u * 1024u - n - 1)
        apx_fail(b->ctx, "P006", "compile", "compiler_resource_limit", no_span, "compiler output limit exceeded");
    if (b->length + n + 1 > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 256;
        char *p;
        while (capacity < b->length + n + 1) capacity *= 2;
        p = apx_alloc(b->ctx, capacity);
        if (b->length) memcpy(p, b->data, b->length);
        b->data = p; b->capacity = capacity;
    }
    if (n) memcpy(b->data + b->length, text, n);
    b->length += n; b->data[b->length] = 0;
}
void apx_buf_put(ApxBuffer *b, const char *text) { apx_buf_bytes(b, text, strlen(text)); }
void apx_buf_printf(ApxBuffer *b, const char *format, ...) {
    va_list args, copy; int count; char small[256], *text;
    va_start(args, format); va_copy(copy, args);
    count = vsnprintf(small, sizeof small, format, args); va_end(args);
    if (count < 0) apx_fail(b->ctx, "P006", "compile", "format_error", no_span, "compiler formatting failed");
    if ((size_t)count < sizeof small) text = small;
    else { text = apx_alloc(b->ctx, (size_t)count + 1); vsnprintf(text, (size_t)count + 1, format, copy); }
    va_end(copy); apx_buf_bytes(b, text, (size_t)count);
}
void apx_json_quote(ApxBuffer *b, const char *text, size_t n) {
    size_t i; apx_buf_put(b, "\"");
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') apx_buf_printf(b, "\\%c", c);
        else if (c < 32) apx_buf_printf(b, "\\u%04x", c);
        else apx_buf_bytes(b, text + i, 1);
    }
    apx_buf_put(b, "\"");
}
void apx_type_text(ApxBuffer *b, const ApxType *t) {
    static const char *const names[] = {"i64", "f64", "bool", "str", "array", "option", "result", "builder", "named", "map", "<error>"};
    if (t->kind == APX_NAMED) apx_buf_put(b, t->name);
    else if (t->kind == APX_ARRAY) { apx_buf_put(b, "["); apx_type_text(b, t->a); apx_buf_put(b, "]"); }
    else { apx_buf_put(b, names[t->kind]);
        if (t->a) { apx_buf_put(b, " "); apx_type_text(b, t->a); }
        if (t->b) { apx_buf_put(b, " "); apx_type_text(b, t->b); }
    }
}
static ApxType *type_new(ApxContext *ctx, ApxTypeKind kind, ApxType *a, ApxType *b) {
    ApxType *t = apx_alloc(ctx, sizeof *t); t->kind = kind; t->a = a; t->b = b; return t;
}
ApxDecl *apx_find_decl(ApxProgram *p, const char *name) {
    ApxDecl *d; for (d = p->types; d; d = d->next) if (!strcmp(d->name, name)) return d; return NULL;
}
ApxFunction *apx_find_function(ApxProgram *p, const char *name) {
    ApxFunction *f; for (f = p->functions; f; f = f->next) if (!strcmp(f->name, name)) return f; return NULL;
}
static int ident_start(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
static int ident_continue(unsigned char c) { return ident_start(c) || (c >= '0' && c <= '9'); }
static int identifier(const char *s) {
    if (!ident_start((unsigned char)*s) || !strcmp(s, "_")) return 0;
    while (*++s) if (!ident_continue((unsigned char)*s)) return 0;
    return 1;
}
static void require_ident(ApxContext *ctx, const char *s, ApxSpan span) {
    if (!identifier(s)) apx_fail(ctx, "P001", "parse", "invalid_identifier", span, "invalid identifier");
}
static unsigned utf8_scalar(const unsigned char *s, size_t n, size_t *used) {
    unsigned v, min; size_t need, i;
    if (!n) return 0xffffffffu;
    if (s[0] < 0x80) { *used = 1; return s[0]; }
    if (s[0] >= 0xc2 && s[0] <= 0xdf) { need = 2; v = s[0] & 31u; min = 0x80; }
    else if (s[0] >= 0xe0 && s[0] <= 0xef) { need = 3; v = s[0] & 15u; min = 0x800; }
    else if (s[0] >= 0xf0 && s[0] <= 0xf4) { need = 4; v = s[0] & 7u; min = 0x10000; }
    else return 0xffffffffu;
    if (n < need) return 0xffffffffu;
    for (i = 1; i < need; ++i) { if ((s[i] & 0xc0) != 0x80) return 0xffffffffu; v = (v << 6) | (s[i] & 63u); }
    if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return 0xffffffffu;
    *used = need; return v;
}
static void utf8_put(ApxBuffer *b, unsigned v) {
    char s[4]; size_t n;
    if (v < 0x80) { s[0] = (char)v; n = 1; }
    else if (v < 0x800) { s[0] = (char)(0xc0 | (v >> 6)); s[1] = (char)(0x80 | (v & 63)); n = 2; }
    else if (v < 0x10000) { s[0] = (char)(0xe0 | (v >> 12)); s[1] = (char)(0x80 | ((v >> 6) & 63)); s[2] = (char)(0x80 | (v & 63)); n = 3; }
    else { s[0] = (char)(0xf0 | (v >> 18)); s[1] = (char)(0x80 | ((v >> 12) & 63)); s[2] = (char)(0x80 | ((v >> 6) & 63)); s[3] = (char)(0x80 | (v & 63)); n = 4; }
    apx_buf_bytes(b, s, n);
}

typedef struct Token { char *text; ApxSpan span; struct Token *next; } Token;
typedef struct Line { char *text; unsigned indent; ApxSpan span; struct Line *next; } Line;
typedef struct { ApxContext *ctx; Line *line; } Parser;
static void syntax_depth(ApxContext *ctx, unsigned depth, ApxSpan span) {
    if (depth > APX_MAX_DEPTH) apx_fail(ctx, "P001", "parse", "nesting_limit_exceeded", span, "syntax nesting exceeds maximum depth 64");
}
static Token *lex(ApxContext *ctx, const char *text, size_t base) {
    Token *head = NULL, **tail = &head; size_t i = 0, n = strlen(text);
    while (i < n) {
        size_t start; Token *t;
        if (isspace((unsigned char)text[i])) { ++i; continue; }
        start = i;
        if (text[i] == '"') {
            int closed = 0; ++i;
            while (i < n) {
                if (text[i] == '\\') { i += 2; if (i > n) i = n; }
                else if (text[i++] == '"') { closed = 1; break; }
            }
            if (!closed) { ApxSpan s = {base + start, base + i}; apx_fail(ctx, "P002", "parse", "unterminated_string", s, "unterminated string"); }
        } else if (strchr("!<>", text[i]) && i + 1 < n && text[i + 1] == '=') i += 2;
        else if (strchr("()[]=:", text[i]) || (text[i]=='.' && !(i+1<n && isdigit((unsigned char)text[i+1])))) ++i;
        else if(ident_start((unsigned char)text[i])){
            ++i;while(i<n&&ident_continue((unsigned char)text[i]))++i;
        }else{
            int numeric=isdigit((unsigned char)text[i]) ||
                (text[i]=='-'&&i+1<n&&(isdigit((unsigned char)text[i+1])||
                    (text[i+1]=='.'&&i+2<n&&isdigit((unsigned char)text[i+2])))) ||
                (text[i]=='.'&&i+1<n&&isdigit((unsigned char)text[i+1]));
            ++i;while(i<n&&!isspace((unsigned char)text[i])&&!strchr(numeric?"()[]=:":"()[]=:.",text[i]))++i;
        }
        t = apx_alloc(ctx, sizeof *t); t->text = apx_copy(ctx, text + start, i - start);
        t->span.start = base + start; t->span.end = base + i;
        *tail = t; tail = &t->next;
    }
    return head;
}
static Token *take(ApxContext *ctx, Token **tokens) {
    Token *t = *tokens;
    if (!t) apx_fail(ctx, "P001", "parse", "unexpected_eof", no_span, "required syntax is missing");
    *tokens = t->next; return t;
}
static void expect(ApxContext *ctx, Token **tokens, const char *text) {
    Token *t = take(ctx, tokens);
    if (strcmp(t->text, text)) apx_fail(ctx, "P001", "parse", "unexpected_token", t->span, "unexpected token");
}
static void end_tokens(ApxContext *ctx, Token *tokens) {
    if (tokens) apx_fail(ctx, "P001", "parse", "unexpected_token", tokens->span, "unexpected trailing token");
}
static ApxType *type_at(ApxContext *ctx,ApxTypeKind kind,ApxType *a,ApxType *b,ApxSpan span){ApxType *type=type_new(ctx,kind,a,b);type->span=span;return type;}
static ApxType *parse_type(ApxContext *ctx, Token **tokens, unsigned depth) {
    Token *t; ApxType *type; syntax_depth(ctx, depth, *tokens ? (*tokens)->span : no_span); t = take(ctx, tokens);
    if (!strcmp(t->text, "[")) {
        type = type_at(ctx, APX_ARRAY, parse_type(ctx, tokens, depth + 1), NULL,t->span); expect(ctx, tokens, "]"); return type;
    }
    if (!strcmp(t->text, "option") || !strcmp(t->text, "builder") || !strcmp(t->text, "result") || !strcmp(t->text, "map")) {
        ApxTypeKind k = !strcmp(t->text, "option") ? APX_OPTION : !strcmp(t->text, "builder") ? APX_BUILDER : !strcmp(t->text, "map") ? APX_MAP : APX_RESULT;
        type = type_at(ctx, k, parse_type(ctx, tokens, depth + 1), NULL,t->span);
        if (k == APX_RESULT) type->b = parse_type(ctx, tokens, depth + 1);
        return type;
    }
    if (!strcmp(t->text, "i64")) return type_at(ctx, APX_I64, NULL, NULL,t->span);
    if (!strcmp(t->text, "f64")) return type_at(ctx, APX_F64, NULL, NULL,t->span);
    if (!strcmp(t->text, "bool")) return type_at(ctx, APX_BOOL, NULL, NULL,t->span);
    if (!strcmp(t->text, "str")) return type_at(ctx, APX_STR, NULL, NULL,t->span);
    require_ident(ctx, t->text, t->span); type = type_at(ctx, APX_NAMED, NULL, NULL,t->span); type->name = t->text; return type;
}
static char *decode_string(ApxContext *ctx, Token *t, size_t *length) {
    size_t i = 1, n = strlen(t->text); ApxBuffer b; apx_buf_init(&b, ctx);
    while (i + 1 < n) {
        char c = t->text[i++];
        if (c != '\\') { apx_buf_bytes(&b, &c, 1); continue; }
        c = t->text[i++];
        if (c == 'n') c = '\n'; else if (c == 'r') c = '\r'; else if (c == 't') c = '\t'; else if (c == '0') c = '\0';
        else if (c == 'u') {
            unsigned value = 0, digits = 0;
            if (i >= n - 1 || t->text[i++] != '{') goto bad_escape;
            while (i < n - 1 && t->text[i] != '}') {
                unsigned d; unsigned char h = (unsigned char)t->text[i++];
                if (h >= '0' && h <= '9') d = h - '0';
                else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
                else goto bad_escape;
                if (++digits > 6) goto bad_escape;
                value = value * 16 + d;
            }
            if (!digits || i >= n - 1 || t->text[i++] != '}' || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) goto bad_escape;
            utf8_put(&b, value); continue;
        } else if (c != '"' && c != '\\') goto bad_escape;
        apx_buf_bytes(&b, &c, 1);
    }
    apx_buf_put(&b, ""); *length = b.length; return b.data;
bad_escape:
    apx_fail(ctx, "P005", "parse", "invalid_literal", t->span, "invalid string escape or Unicode scalar"); return NULL;
}
static int integer_token(const char *s) {
    if (*s == '-') ++s;
    if (!*s) return 0;
    while (*s) if (*s < '0' || *s++ > '9') return 0;
    return 1;
}
static int float_token(const char *s) {
    unsigned digits = 0; int fractional = 0;
    if (*s == '-') ++s;
    while (*s >= '0' && *s <= '9') { ++s; ++digits; }
    if (*s == '.') { fractional = 1; ++s; while (*s >= '0' && *s <= '9') { ++s; ++digits; } }
    if (!digits) return 0;
    if (*s == 'e' || *s == 'E') {
        unsigned exponent_digits = 0; fractional = 1; ++s;
        if (*s == '+' || *s == '-') ++s;
        while (*s >= '0' && *s <= '9') { ++s; ++exponent_digits; }
        if (!exponent_digits) return 0;
    }
    return fractional && !*s;
}
static unsigned parsed_type_height(ApxType *type){
    unsigned a=type->a?parsed_type_height(type->a)+1:0;
    unsigned b=type->b?parsed_type_height(type->b)+1:0;return a>b?a:b;
}
static ApxExpr *parse_expr(ApxContext *ctx,Token **tokens,unsigned depth);
static void expression_height(ApxContext *ctx,ApxExpr *expr,unsigned depth){
    unsigned height=0;for(ApxArg *a=expr->args;a;a=a->next){
        unsigned next=a->expr->syntax_height+1;if(next>height)height=next;
    }
    expr->syntax_height=height;syntax_depth(ctx,depth+height,expr->span);
}
static ApxExpr *parse_atom(ApxContext *ctx, Token **tokens, unsigned depth) {
    Token *t; ApxExpr *e; syntax_depth(ctx, depth, *tokens ? (*tokens)->span : no_span); t = take(ctx, tokens);
    e = apx_alloc(ctx, sizeof *e); e->span = t->span;
    if (!strcmp(t->text, "(") || !strcmp(t->text, "[")) {
        int call = t->text[0] == '('; const char *close = call ? ")" : "]"; ApxArg **tail = &e->args;
        e->kind = call ? APX_CALL_EXPR : APX_ARRAY_EXPR;
        if (call) {
            Token *head = take(ctx, tokens);
            if (strchr("()[]=:\"", head->text[0]) && strcmp(head->text, "="))
                apx_fail(ctx, "P001", "parse", "unexpected_token", head->span, "call head is missing or malformed");
            e->text = head->text;
            /* Dotted callable names remain opaque, unsupported names. This
             * does not introduce methods, enum constructors or member calls. */
            while(identifier(head->text)&&*tokens&&!strcmp((*tokens)->text,".")&&
                  (*tokens)->span.start==head->span.end&&(*tokens)->next&&
                  (*tokens)->next->span.start==(*tokens)->span.end&&identifier((*tokens)->next->text)){
                Token *member=(*tokens)->next;ApxBuffer name;apx_buf_init(&name,ctx);
                apx_buf_printf(&name,"%s.%s",e->text,member->text);e->text=name.data;
                head->span.end=member->span.end;*tokens=member->next;
            }
            if ((!strcmp(head->text, "buf") || !strcmp(head->text, "map")) && *tokens && strcmp((*tokens)->text, ")")) {
                ApxArg *a = apx_alloc(ctx, sizeof *a); ApxExpr *marker = apx_alloc(ctx, sizeof *marker);
                marker->kind = APX_TYPE_EXPR; marker->span = (*tokens)->span; marker->marker = parse_type(ctx, tokens, depth + 1);
                marker->syntax_height=parsed_type_height(marker->marker);
                a->expr = marker; *tail = a; tail = &a->next; ++e->count;
            }
        }
        while (*tokens && strcmp((*tokens)->text, close)) {
            ApxArg *a = apx_alloc(ctx, sizeof *a);
            if (call && identifier((*tokens)->text) && (*tokens)->next && !strcmp((*tokens)->next->text, "=")) {
                a->name = (*tokens)->text; *tokens = (*tokens)->next->next;
            }
            a->expr = parse_expr(ctx, tokens, depth + 1); *tail = a; tail = &a->next; ++e->count;
        }
        if (!*tokens) apx_fail(ctx, "P001", "parse", "unclosed_delimiter", t->span, "missing closing delimiter");
        e->span.end = (*tokens)->span.end; *tokens = (*tokens)->next;expression_height(ctx,e,depth); return e;
    }
    if (!strcmp(t->text, "_")) e->kind = APX_HOLE_EXPR;
    else if (!strcmp(t->text, "true") || !strcmp(t->text, "false")) { e->kind = APX_BOOL_EXPR; e->integer = !strcmp(t->text, "true"); }
    else if (t->text[0] == '"') { e->kind = APX_STRING_EXPR; e->text = decode_string(ctx, t, &e->length); }
    else if (integer_token(t->text)) {
        char *end; intmax_t value; errno = 0; value = strtoimax(t->text, &end, 10);
        if (errno == ERANGE || *end || value < INT64_MIN || value > INT64_MAX)
            apx_fail(ctx, "A103", "parse", "numeric_out_of_range", t->span, "i64 literal is out of range");
        e->kind = APX_INT_EXPR; e->integer = (int64_t)value;
    } else if (float_token(t->text)) {
        char *end; e->real = strtod(t->text, &end);
        if (*end || !isfinite(e->real)) apx_fail(ctx, "P005", "parse", "invalid_literal", t->span, "float literal must be finite");
        e->kind = APX_FLOAT_EXPR;
    } else { require_ident(ctx, t->text, t->span); e->kind = APX_VAR_EXPR; e->text = t->text; }
    return e;
}
static ApxExpr *access_call(ApxContext *ctx,const char *head,ApxExpr *base,ApxExpr *part,ApxSpan span,unsigned depth){
    ApxExpr *call=apx_alloc(ctx,sizeof *call);ApxArg *left=apx_alloc(ctx,sizeof *left),*right=apx_alloc(ctx,sizeof *right);
    call->kind=APX_CALL_EXPR;call->text=apx_copy(ctx,head,strlen(head));call->span=span;call->count=2;
    left->expr=base;left->next=right;right->expr=part;call->args=left;
    expression_height(ctx,call,depth);return call;
}
static ApxExpr *parse_expr(ApxContext *ctx,Token **tokens,unsigned depth){
    ApxExpr *base=parse_atom(ctx,tokens,depth);
    while(*tokens&&(*tokens)->span.start==base->span.end&&
          (!strcmp((*tokens)->text,".")||!strcmp((*tokens)->text,"["))){
        Token *suffix=*tokens;*tokens=suffix->next;
        if(!strcmp(suffix->text,".")){
            Token *name=take(ctx,tokens);
            if(name->span.start!=suffix->span.end)apx_fail(ctx,"P001","parse","unexpected_token",name->span,"field suffix must attach directly to its field name");
            require_ident(ctx,name->text,name->span);
            ApxExpr *field=apx_alloc(ctx,sizeof *field);field->kind=APX_VAR_EXPR;field->text=name->text;field->span=name->span;
            /* Literal keywords are legal declared field names. Quote them in
             * the legacy prefix form so canonical source reparses identically. */
            if(!strcmp(name->text,"true")||!strcmp(name->text,"false")){
                field->kind=APX_STRING_EXPR;field->length=strlen(name->text);
            }
            base=access_call(ctx,".",base,field,(ApxSpan){base->span.start,name->span.end},depth);
        }else{
            ApxExpr *index=parse_expr(ctx,tokens,depth+1);Token *end=take(ctx,tokens);
            if(strcmp(end->text,"]"))apx_fail(ctx,"P001","parse","unexpected_token",end->span,"index requires exactly one expression followed by ]");
            base=access_call(ctx,"@",base,index,(ApxSpan){base->span.start,end->span.end},depth);
        }
    }
    return base;
}
static ApxField *parse_fields(ApxContext *ctx, Token **tokens) {
    ApxField *head = NULL, **tail = &head;
    while (*tokens) {
        Token *name = take(ctx, tokens); ApxField *f = apx_alloc(ctx, sizeof *f);
        require_ident(ctx, name->text, name->span); f->name = name->text; f->span = name->span;
        expect(ctx, tokens, ":"); f->type = parse_type(ctx, tokens, 0);
        *tail = f; tail = &f->next;
    }
    return head;
}
static Line *source_lines(ApxContext *ctx) {
    size_t pos = 0; unsigned line_number = 0; Line *head = NULL, **tail = &head;
    while (pos < ctx->source_length) {
        size_t start = pos, end, i, indent; int quoted = 0, escaped = 0; Line *line;
        ++line_number;
        while (pos < ctx->source_length && ctx->source[pos] != '\n') ++pos;
        end = pos; if (pos < ctx->source_length) ++pos;
        if (end > start && ctx->source[end - 1] == '\r') --end;
        for (i = start; i < end; ++i) {
            char c = ctx->source[i];
            if (quoted) { if (escaped) escaped = 0; else if (c == '\\') escaped = 1; else if (c == '"') quoted = 0; }
            else if (c == '"') quoted = 1;
            else if (c == ';') { end = i; break; }
        }
        while (end > start && isspace((unsigned char)ctx->source[end - 1])) --end;
        i = start; while (i < end && isspace((unsigned char)ctx->source[i])) ++i;
        if (i == end) continue;
        indent = i - start;
        if (indent % 2) { ApxSpan s = {start, end}; apx_fail(ctx, "P003", "parse", "bad_indentation", s, "use two spaces per indentation level"); }
        for (i = start; i < start + indent; ++i) if (ctx->source[i] != ' ') {
            ApxSpan s = {start, end}; apx_fail(ctx, "P003", "parse", "bad_indentation", s, "indentation must use spaces, not tabs");
        }
        { ApxSpan s = {start + indent, end}; syntax_depth(ctx, (unsigned)indent / 2, s); }
        line = apx_alloc(ctx, sizeof *line); line->indent = (unsigned)indent;
        line->span.start = start + indent; line->span.end = end;
        line->text = apx_copy(ctx, ctx->source + start + indent, end - start - indent);
        *tail = line; tail = &line->next;
    }
    (void)line_number; return head;
}
static ApxStmt *parse_block(Parser *p, unsigned indent) {
    ApxStmt *head = NULL, **tail = &head; syntax_depth(p->ctx, indent / 2, no_span);
    while (p->line && p->line->indent >= indent) {
        Line *l = p->line; ApxStmt *s; Token *tokens,*target=NULL; unsigned depth = indent / 2 - 1;
        if (l->indent != indent) apx_fail(p->ctx, "P003", "parse", "bad_indentation", l->span, "unexpected block indentation");
        if (!l->text[0] || !strchr("LSPR?WECAZ", l->text[0]) || l->text[1] != ' ')
            apx_fail(p->ctx, "P001", "parse", "unexpected_token", l->span, "unknown statement");
        p->line = l->next; s = apx_alloc(p->ctx, sizeof *s); s->kind = l->text[0]; s->span = l->span; s->span.start-=indent;
        tokens = lex(p->ctx, l->text + 2, l->span.start + 2);
        if (s->kind == 'L' || s->kind == 'S' || s->kind == 'P' || s->kind == 'E') {
            Token *name = take(p->ctx, &tokens); require_ident(p->ctx, name->text, name->span); s->name = name->text;target=name;
            if (s->kind == 'L' && tokens && !strcmp(tokens->text, ":")) {
                tokens = tokens->next; s->annotation = parse_type(p->ctx, &tokens, depth);
            } else if (tokens && tokens->span.start == name->span.end)
                apx_fail(p->ctx, "P001", "parse", "unexpected_token", name->span, "binding must be separated from expression");
        }
        s->expr = parse_expr(p->ctx, &tokens, depth+(s->kind=='P')); end_tokens(p->ctx, tokens);
        if(s->kind=='P'){
            ApxExpr *builder=apx_alloc(p->ctx,sizeof *builder);builder->kind=APX_VAR_EXPR;
            builder->text=target->text;builder->span=target->span;
            if(!strcmp(target->text,"true")||!strcmp(target->text,"false")){
                builder->kind=APX_BOOL_EXPR;builder->integer=!strcmp(target->text,"true");
            }
            s->expr=access_call(p->ctx,"push",builder,s->expr,(ApxSpan){target->span.start,s->expr->span.end},depth);
            s->kind='S'; /* One existing binding update; no new runtime opcode. */
        }
        if (s->kind == 'Z' && s->expr->kind != APX_STRING_EXPR)
            apx_fail(p->ctx, "P005", "parse", "invalid_literal", s->span, "trap message must be a string literal");
        if (s->kind == '?' || s->kind == 'W' || s->kind == 'E') s->body = parse_block(p, indent + 2);
        else if (s->kind == 'C') {
            ApxArm **arm_tail = &s->arms;
            while (p->line && p->line->indent > indent) {
                ApxArm *a; ApxField **binding_tail; Line *al = p->line; Token *at, *name;
                if (al->indent != indent + 2) apx_fail(p->ctx, "P003", "parse", "bad_indentation", al->span, "unexpected case arm indentation");
                p->line = al->next; a = apx_alloc(p->ctx, sizeof *a); a->span = al->span;
                at = lex(p->ctx, al->text, al->span.start); name = take(p->ctx, &at);
                require_ident(p->ctx, name->text, name->span); a->name = name->text; binding_tail = &a->bindings;
                while (at) { ApxField *f = apx_alloc(p->ctx, sizeof *f); name = take(p->ctx, &at);
                    require_ident(p->ctx, name->text, name->span); f->name = name->text; f->span = name->span;
                    *binding_tail = f; binding_tail = &f->next;
                }
                a->body = parse_block(p, indent + 4); *arm_tail = a; arm_tail = &a->next;
            }
        }
        *tail = s; tail = &s->next;
    }
    return head;
}
static ApxProgram *parse_program(ApxContext *ctx) {
    ApxProgram *program = apx_alloc(ctx, sizeof *program); Parser p;
    ApxDecl **dt = &program->types; ApxFunction **ft = &program->functions, **tt = &program->tests;
    Token *tokens, *name; unsigned function_index = 0, test_index = 0;
    p.ctx = ctx; p.line = source_lines(ctx);
    if (!p.line) apx_fail(ctx, "P001", "parse", "empty_source", no_span, "source is empty");
    if (p.line->indent) apx_fail(ctx, "P003", "parse", "bad_indentation", p.line->span, "module must start at column zero");
    tokens = lex(ctx, p.line->text, p.line->span.start); expect(ctx, &tokens, "M"); name = take(ctx, &tokens);
    require_ident(ctx, name->text, name->span); program->module = name->text; end_tokens(ctx, tokens); p.line = p.line->next;
    while (p.line) {
        Line *l = p.line; char k = l->text[0]; p.line = l->next;
        if (l->indent) apx_fail(ctx, "P003", "parse", "bad_indentation", l->span, "declaration must start at column zero");
        if (l->text[1] != ' ') apx_fail(ctx, "P001", "parse", "unexpected_token", l->span, "unknown top-level declaration");
        if (k == 'B') continue;
        if (k == 'T') {
            ApxDecl *d = apx_alloc(ctx, sizeof *d); ApxField **field_tail = &d->fields; ApxVariant **variant_tail = &d->variants; Token *kind;
            tokens = lex(ctx, l->text + 2, l->span.start + 2); name = take(ctx, &tokens); kind = take(ctx, &tokens);
            require_ident(ctx, name->text, name->span); d->name = name->text; d->span = l->span;d->name_span=name->span;
            if (strcmp(kind->text, "struct") && strcmp(kind->text, "enum")) apx_fail(ctx, "P001", "parse", "unexpected_token", l->span, "type must be struct or enum");
            d->is_enum = !strcmp(kind->text, "enum"); end_tokens(ctx, tokens);
            while (p.line && p.line->indent) {
                Line *member = p.line; p.line = member->next;
                if (member->indent != 2) apx_fail(ctx, "P003", "parse", "bad_indentation", member->span, "type members require two-space indentation");
                tokens = lex(ctx, member->text, member->span.start);
                if (!d->is_enum) { ApxField *f = parse_fields(ctx, &tokens);
                    if (!f || f->next) apx_fail(ctx, "P001", "parse", "unexpected_token", member->span, "one struct field per line is required");
                    *field_tail = f; field_tail = &f->next;
                } else { ApxVariant *v = apx_alloc(ctx, sizeof *v); name = take(ctx, &tokens);
                    require_ident(ctx, name->text, name->span); v->name = name->text; v->span = member->span;
                    v->fields = parse_fields(ctx, &tokens); *variant_tail = v; variant_tail = &v->next;
                }
            }
            *dt = d; dt = &d->next;
        } else if (k == 'F') {
            ApxFunction *f = apx_alloc(ctx, sizeof *f); char *arrow = strstr(l->text + 2, "->"); char *left;
            if (!arrow || strstr(arrow + 2, "->")) apx_fail(ctx, "P001", "parse", "unexpected_token", l->span, "function requires exactly one return type arrow");
            left = apx_copy(ctx, l->text + 2, (size_t)(arrow - l->text - 2)); tokens = lex(ctx, left, l->span.start + 2); name = take(ctx, &tokens);
            require_ident(ctx, name->text, name->span); f->name = name->text; f->span = l->span;f->name_span=name->span; f->index = function_index++;
            f->params = parse_fields(ctx, &tokens); tokens = lex(ctx, arrow + 2, l->span.start + (size_t)(arrow - l->text) + 2);
            f->returns = parse_type(ctx, &tokens, 0); end_tokens(ctx, tokens); f->body = parse_block(&p, 2); *ft = f; ft = &f->next;
        } else if (k == 'X') {
            ApxFunction *f = apx_alloc(ctx, sizeof *f); tokens = lex(ctx, l->text + 2, l->span.start + 2); name = take(ctx, &tokens);
            require_ident(ctx, name->text, name->span); f->name = name->text; f->span = l->span;f->name_span=name->span; f->index = test_index++; f->is_test = 1;
            end_tokens(ctx, tokens); f->body = parse_block(&p, 2); *tt = f; tt = &f->next;
        } else apx_fail(ctx, "P001", "parse", "unexpected_token", l->span, "unknown top-level declaration");
    }
    return program;
}
/* Canonical formatting is AST-based, removing comments, B declarations and CRLF. */
static void source_quote(ApxBuffer *b, const char *text, size_t n) {
    size_t i = 0; apx_buf_put(b, "\"");
    while (i < n) {
        size_t used = 1; unsigned v = utf8_scalar((const unsigned char *)text + i, n - i, &used);
        if (v == '"') apx_buf_put(b, "\\\""); else if (v == '\\') apx_buf_put(b, "\\\\");
        else if (v == '\n') apx_buf_put(b, "\\n"); else if (v == '\r') apx_buf_put(b, "\\r");
        else if (v == '\t') apx_buf_put(b, "\\t"); else if (!v) apx_buf_put(b, "\\0");
        else if (apx_unicode_escape(v)) apx_buf_printf(b, "\\u{%x}", v);
        else apx_buf_bytes(b, text + i, used);
        i += used;
    }
    apx_buf_put(b, "\"");
}
static void float_text(ApxBuffer *b, double value) {
    char s[64], digits[32]; int precision, exponent = 0, negative, before = 0, count = 0, point, i; char *p, *e;
    /* First correctly rounded significant-digit spelling that roundtrips.
       Expand exponents so canonical floats are always decimal with a point. */
    for (precision = 1; precision <= 17; ++precision) {
        double roundtrip; snprintf(s, sizeof s, "%.*g", precision, value); roundtrip = strtod(s, NULL);
        if (!memcmp(&roundtrip, &value, sizeof value)) break;
    }
    p = s; negative = *p == '-'; if (negative) ++p;
    e = strchr(p, 'e'); if (!e) e = strchr(p, 'E'); if (e) exponent = atoi(e + 1);
    for (; *p && p != e; ++p) {
        if (*p == '.') { before = count; continue; } digits[count++] = *p;
    }
    if (!strchr(s, '.')) before = count;
    point = before + exponent; if (negative) apx_buf_put(b, "-");
    if (point <= 0) { apx_buf_put(b, "0."); for (i = 0; i < -point; ++i) apx_buf_put(b, "0"); apx_buf_bytes(b, digits, (size_t)count); }
    else { for (i = 0; i < point || i < count; ++i) {
            if (i == point) apx_buf_put(b, ".");
            if (i < count) apx_buf_bytes(b, digits + i, 1); else apx_buf_put(b, "0");
        }
        if (point >= count) apx_buf_put(b, ".0");
    }
}
static void format_expr(ApxBuffer *b, ApxExpr *e) {
    ApxArg *a;
    switch (e->kind) {
    case APX_INT_EXPR: apx_buf_printf(b, "%" PRId64, e->integer); break;
    case APX_FLOAT_EXPR: float_text(b, e->real); break;
    case APX_BOOL_EXPR: apx_buf_put(b, e->integer ? "true" : "false"); break;
    case APX_STRING_EXPR: source_quote(b, e->text, e->length); break;
    case APX_VAR_EXPR: apx_buf_put(b, e->text); break;
    case APX_HOLE_EXPR: apx_buf_put(b, "_"); break;
    case APX_TYPE_EXPR: apx_type_text(b, e->marker); break;
    case APX_ARRAY_EXPR: case APX_CALL_EXPR:
        apx_buf_put(b, e->kind == APX_ARRAY_EXPR ? "[" : "(");
        if (e->kind == APX_CALL_EXPR) apx_buf_put(b, e->text);
        for (a = e->args; a; a = a->next) {
            if (e->kind == APX_CALL_EXPR || a != e->args) apx_buf_put(b, " ");
            if (a->name) { apx_buf_put(b, a->name); apx_buf_put(b, "="); }
            format_expr(b, a->expr);
        }
        apx_buf_put(b, e->kind == APX_ARRAY_EXPR ? "]" : ")"); break;
    }
}
static void pad(ApxBuffer *b, unsigned depth) { while (depth--) apx_buf_put(b, "  "); }
static void format_fields(ApxBuffer *b, ApxField *fields, int leading) {
    ApxField *f; for (f = fields; f; f = f->next) {
        if (leading || f != fields) apx_buf_put(b, " ");
        apx_buf_put(b, f->name); apx_buf_put(b, ":"); apx_type_text(b, f->type);
    }
}
static void format_block(ApxBuffer *b, ApxStmt *body, unsigned depth) {
    ApxStmt *s;
    for (s = body; s; s = s->next) {
        ApxArm *a; pad(b, depth); apx_buf_printf(b, "%c ", s->kind);
        if (s->name) { apx_buf_put(b, s->name);
            if (s->annotation) { apx_buf_put(b, ":"); apx_type_text(b, s->annotation); }
            apx_buf_put(b, " ");
        }
        format_expr(b, s->expr); apx_buf_put(b, "\n"); format_block(b, s->body, depth + 1);
        for (a = s->arms; a; a = a->next) {
            ApxField *f; pad(b, depth + 1); apx_buf_put(b, a->name);
            for (f = a->bindings; f; f = f->next) { apx_buf_put(b, " "); apx_buf_put(b, f->name); }
            apx_buf_put(b, "\n"); format_block(b, a->body, depth + 2);
        }
    }
}
static char *format_program(ApxContext *ctx, ApxProgram *program) {
    ApxBuffer b; ApxDecl *d; ApxFunction *f; apx_buf_init(&b, ctx); apx_buf_printf(&b, "M %s\n", program->module);
    for (d = program->types; d; d = d->next) {
        ApxField *field; ApxVariant *v; apx_buf_printf(&b, "\nT %s %s\n", d->name, d->is_enum ? "enum" : "struct");
        for (field = d->fields; field; field = field->next) { apx_buf_printf(&b, "  %s:", field->name); apx_type_text(&b, field->type); apx_buf_put(&b, "\n"); }
        for (v = d->variants; v; v = v->next) { apx_buf_printf(&b, "  %s", v->name); format_fields(&b, v->fields, 1); apx_buf_put(&b, "\n"); }
    }
    for (f = program->functions; f; f = f->next) {
        apx_buf_printf(&b, "\nF %s", f->name); format_fields(&b, f->params, 1); apx_buf_put(&b, "->"); apx_type_text(&b, f->returns); apx_buf_put(&b, "\n"); format_block(&b, f->body, 1);
    }
    for (f = program->tests; f; f = f->next) { apx_buf_printf(&b, "\nX %s\n", f->name); format_block(&b, f->body, 1); }
    return b.data;
}
/* Lexical scopes are resolved at compile time to unique native stack slots. */
typedef struct { ApxContext *ctx; ApxProgram *program; ApxFunction *function; ApxBinding *scope; unsigned depth, next_binding; ApxSpan expected_span; } Checker;
static ApxType poison_type = {APX_ERROR, NULL, NULL, NULL, {0, 0}};
static int poisoned(const ApxType *type) { return !type || type->kind == APX_ERROR; }
/* Scope lists are persistent arena allocations: saving their head preserves
 * the exact hole environment even after subsequent scopes/bindings change. */
static void collect_error(ApxContext *ctx) {
    ApxDiagnostic *d;
    if (ctx->diagnostic_count >= APX_MAX_DIAGNOSTICS)
        apx_fail(ctx, "P006", "compile", "compiler_resource_limit", no_span,
                 "diagnostic count exceeds maximum 4096; checking is incomplete");
    d = apx_alloc(ctx, sizeof *d);
    d->code = ctx->error_code; d->phase = ctx->error_phase;
    d->kind = ctx->error_kind; d->message = ctx->error_message;
    d->span = ctx->error_span; d->expected = ctx->error_expected;
    d->scope = ctx->error_scope;
    *ctx->diagnostics_tail = d; ctx->diagnostics_tail = &d->next;
    ++ctx->diagnostic_count;
    ctx->error_expected = NULL; ctx->error_scope = NULL;
}
static void mismatch(Checker *c, ApxSpan span, const char *message) { apx_fail(c->ctx, "T101", "type", "type_mismatch", span, message); }
static void unknown(Checker *c, ApxSpan span) { apx_fail(c->ctx, "N101", "resolve", "unknown_symbol", span, "unknown symbol, type, field or callable"); }
static void duplicate(Checker *c, ApxSpan span) { apx_fail(c->ctx, "N102", "resolve", "duplicate_symbol", span, "duplicate declaration or binding"); }
static void report_semantic(Checker *c, const char *code, const char *phase,
                            const char *kind, ApxSpan span, const char *message) {
    if (!c->ctx->collect_diagnostics) apx_fail(c->ctx, code, phase, kind, span, message);
    c->ctx->failed = 1; c->ctx->error_code = code; c->ctx->error_phase = phase;
    c->ctx->error_kind = kind; c->ctx->error_span = span; c->ctx->error_message = message;
    c->ctx->error_expected = NULL; c->ctx->error_scope = NULL; collect_error(c->ctx);
}
static void report_duplicate(Checker *c, ApxSpan span) {
    report_semantic(c, "N102", "resolve", "duplicate_symbol", span, "duplicate declaration or binding");
}
static int type_equal(const ApxType *a, const ApxType *b) {
    if (!a || !b) return a == b;
    return a->kind == b->kind && (a->kind != APX_NAMED || !strcmp(a->name, b->name)) && type_equal(a->a, b->a) && type_equal(a->b, b->b);
}
static int contains_builder(const ApxType *t) { return t && (t->kind == APX_BUILDER || contains_builder(t->a) || contains_builder(t->b)); }
static unsigned type_height(Checker *c, ApxType *t, ApxSpan span, unsigned depth) {
    unsigned a = 0, b = 0;
    if (depth > APX_MAX_DEPTH) apx_fail(c->ctx, "T106", "type", "type_nesting_limit_exceeded", span, "inferred type nesting exceeds maximum depth 64");
    if (t->a) a = 1 + type_height(c, t->a, span, depth + 1);
    if (t->b) b = 1 + type_height(c, t->b, span, depth + 1);
    return a > b ? a : b;
}
static void validate_type(Checker *c, ApxType *t, ApxSpan span, int boundary) {
    if ((boundary || t->kind == APX_MAP) && contains_builder(t)) apx_fail(c->ctx, "T101", "type", "invalid_boundary_type", span, "builder types are local-only");
    type_height(c, t, span, 0);
    if (t->kind == APX_NAMED && !apx_find_decl(c->program, t->name)) unknown(c, span);
    if (t->a) validate_type(c, t->a, span, boundary);
    if (t->b) validate_type(c, t->b, span, boundary);
}
static int validate_type_all(Checker *c, ApxType *t, ApxSpan span, int boundary) {
    int valid = 1;
    if (!c->ctx->collect_diagnostics) { validate_type(c, t, span, boundary); return 1; }
    if ((boundary || t->kind == APX_MAP) && contains_builder(t)) {
        report_semantic(c, "T101", "type", "invalid_boundary_type", span, "builder types are local-only"); valid = 0;
    }
    if (t->kind == APX_NAMED && !apx_find_decl(c->program, t->name)) {
        report_semantic(c, "N101", "resolve", "unknown_symbol", span, "unknown symbol, type, field or callable"); valid = 0;
    }
    if (t->a && !validate_type_all(c, t->a, span, 0)) valid = 0;
    if (t->b && !validate_type_all(c, t->b, span, 0)) valid = 0;
    return valid;
}
static int usable_type(Checker *c, const ApxType *t) {
    if (poisoned(t)) return 0;
    if (t->kind == APX_NAMED) {
        ApxDecl *d = apx_find_decl(c->program, t->name);
        if (!d || d->visit == 3) return 0;
    }
    return (!t->a || usable_type(c, t->a)) && (!t->b || usable_type(c, t->b));
}
static unsigned expanded_height(Checker *c, ApxType *t, ApxSpan span, unsigned depth) {
    unsigned result = 0, h; ApxDecl *d; ApxField *f; ApxVariant *v;
    if (depth > APX_MAX_DEPTH) apx_fail(c->ctx, "T106", "type", "type_nesting_limit_exceeded", span, "expanded type nesting exceeds maximum depth 64");
    if (t->kind == APX_NAMED) {
        d = apx_find_decl(c->program, t->name); if (!d) unknown(c, span);
        if (d->visit == 3) return 0;
        if (d->visit == 1) apx_fail(c->ctx, "T104", "type", "recursive_type", span, "recursive named type is not supported");
        if (d->visit == 2) result = d->height;
        else {
            d->visit = 1;
            for (f = d->fields; f; f = f->next) { h = 1 + expanded_height(c, f->type, span, depth + 1); if (h > result) result = h; }
            for (v = d->variants; v; v = v->next) for (f = v->fields; f; f = f->next) { h = 1 + expanded_height(c, f->type, span, depth + 1); if (h > result) result = h; }
            d->visit = 2; d->height = result;
        }
    } else {
        if (t->a) result = 1 + expanded_height(c, t->a, span, depth + 1);
        if (t->b) { h = 1 + expanded_height(c, t->b, span, depth + 1); if (h > result) result = h; }
    }
    if (depth + result > APX_MAX_DEPTH) apx_fail(c->ctx, "T106", "type", "type_nesting_limit_exceeded", span, "expanded type nesting exceeds maximum depth 64");
    return result;
}
static unsigned bind(Checker *c, char *name, ApxType *type, ApxSpan span) {
    ApxBinding *b;
    for (b = c->scope; b; b = b->next) if (!strcmp(b->name, name)) duplicate(c, span);
    b = apx_alloc(c->ctx, sizeof *b); b->name = name; b->type = type; b->depth = c->depth;
    b->id = c->next_binding++; b->next = c->scope; c->scope = b; return b->id;
}
static unsigned bind_recover(Checker *c, char *name, ApxType *type, ApxSpan span) {
    Checker saved = *c; ApxRecovery frame; volatile unsigned id = 0;
    if (!c->ctx->collect_diagnostics) return bind(c, name, type, span);
    frame.previous = c->ctx->recovery; c->ctx->recovery = &frame;
    if (!setjmp(frame.jump)) id = bind(c, name, type, span);
    else { collect_error(c->ctx); *c = saved; }
    c->ctx->recovery = frame.previous; return id;
}
static ApxBinding *lookup(Checker *c, const char *name, ApxSpan span) {
    ApxBinding *b; for (b = c->scope; b; b = b->next) if (!strcmp(b->name, name)) return b;
    unknown(c, span); return NULL;
}
static ApxType *infer(Checker *c, ApxExpr *e, ApxType *expected);
static void positional(Checker *c, ApxExpr *e, size_t count) {
    ApxArg *a;
    if (e->count != count) mismatch(c, e->span, "wrong number of call arguments");
    for (a = e->args; a; a = a->next) if (a->name) apx_fail(c->ctx, "N104", "resolve", "invalid_call_form", e->span, "builtin or positional call cannot use named arguments");
}
static ApxField *find_field(ApxField *fields, const char *name) {
    ApxField *f; for (f = fields; f; f = f->next) if (!strcmp(f->name, name)) return f; return NULL;
}
static void named_arguments(Checker *c, ApxExpr *e, ApxField *fields) {
    ApxArg *a, *b; ApxField *f;
    for (a = e->args; a; a = a->next) {
        if (!a->name) {
            report_semantic(c, "N104", "resolve", "invalid_call_form", e->span, "call requires all named arguments");
            infer(c, a->expr, NULL); continue;
        }
        for (b = e->args; b != a; b = b->next) if (b->name && !strcmp(a->name, b->name)) { report_duplicate(c, a->expr->span); break; }
        f = find_field(fields, a->name);
        if (!f) {
            report_semantic(c, "N101", "resolve", "unknown_symbol", a->expr->span, "unknown symbol, type, field or callable");
            infer(c, a->expr, NULL);
        } else infer(c, a->expr, f->type);
    }
    for (f = fields; f; f = f->next) {
        for (a = e->args; a; a = a->next) if (a->name && !strcmp(a->name, f->name)) break;
        if (!a) report_semantic(c, "A101", "resolve", "missing_field", e->span, "missing constructor field or function parameter");
    }
}
/* Additive pure library checking. Field updates are syntax, not named calls. */
static ApxType *infer_pure(Checker *c, ApxExpr *e, ApxType *expected) {
    int op=e->opcode, bad=0; ApxArg *a=e->args; ApxType *left,*right,*out;
    ApxType *str=type_new(c->ctx,APX_STR,NULL,NULL), *i64=type_new(c->ctx,APX_I64,NULL,NULL);
    if(op==54){
        ApxDecl *d=NULL; ApxArg *b;
        if(e->count<2 || a->name) mismatch(c,e->span,"with requires a record and at least one named field update");
        left=infer(c,a->expr,NULL);
        if(!poisoned(left)){
            if(left->kind!=APX_NAMED) mismatch(c,e->span,"with expects a struct");
            d=apx_find_decl(c->program,left->name);
            if(!d || d->is_enum) mismatch(c,e->span,"with expects a struct");
        }
        for(a=a->next;a;a=a->next){
            ApxField *field=NULL;
            if(!a->name) report_semantic(c,"N104","resolve","invalid_call_form",a->expr->span,"with updates require field=value");
            else {
                for(b=e->args->next;b!=a;b=b->next) if(b->name&&!strcmp(b->name,a->name)){report_duplicate(c,a->expr->span);break;}
                if(d){field=find_field(d->fields,a->name);if(!field)report_semantic(c,"N101","resolve","unknown_symbol",a->expr->span,"unknown update field");}
            }
            if(poisoned(infer(c,a->expr,field?field->type:NULL)))bad=1;
        }
        e->record=d; return bad||poisoned(left)?&poison_type:left;
    }
    if(op==43){
        positional(c,e,1);
        if(a->expr->kind!=APX_TYPE_EXPR) mismatch(c,e->span,"map requires a concrete value type marker");
        if(!validate_type_all(c,a->expr->marker,a->expr->span,1)) return &poison_type;
        return type_new(c->ctx,APX_MAP,a->expr->marker,NULL);
    }
    positional(c,e,(op==32||op==36||op==45||op==51||op==52)?3:
        (op==31||op==33||op==34||op==44||op==46||op==47||op==50||op==53)?2:1);
    if(op>=30&&op<=42){
        ApxType *input=op==41?i64:op==42?type_new(c->ctx,APX_F64,NULL,NULL):str;
        if(poisoned(infer(c,a->expr,input)))bad=1;
        if(a->next && poisoned(infer(c,a->next->expr,(op==31||op==32)?i64:str)))bad=1;
        if(a->next&&a->next->next && poisoned(infer(c,a->next->next->expr,op==32?i64:str)))bad=1;
        out=op==30?i64:op==31?type_new(c->ctx,APX_OPTION,str,NULL):
            op==33?type_new(c->ctx,APX_OPTION,i64,NULL):op==34?type_new(c->ctx,APX_ARRAY,str,NULL):
            (op==39||op==40)?type_new(c->ctx,APX_RESULT,op==39?i64:type_new(c->ctx,APX_F64,NULL,NULL),str):str;
        return bad?&poison_type:out;
    }
    /* Expected collection types keep empty array arguments unambiguous. */
    out=NULL;
    if(expected&&!poisoned(expected)){
        if((op==45||op==46)&&expected->kind==APX_MAP)out=expected;
        if((op==51||op==52||op==53)&&expected->kind==APX_ARRAY)out=expected;
        if(op==50&&expected->kind==APX_OPTION)out=type_new(c->ctx,APX_ARRAY,expected->a,NULL);
    }
    left=infer(c,a->expr,out);
    if(!poisoned(left)&&left->kind!=(op<=49?APX_MAP:APX_ARRAY))mismatch(c,e->span,"collection builtin expects matching map or array");
    if(a->next){right=infer(c,a->next->expr,op<=47?str:op==53?left:i64);if(poisoned(right))bad=1;}
    if(a->next&&a->next->next){
        right=infer(c,a->next->next->expr,op==52?i64:poisoned(left)?&poison_type:left->a);if(poisoned(right))bad=1;
    }
    if(bad||poisoned(left))return &poison_type;
    if(op==44||op==50)return type_new(c->ctx,APX_OPTION,left->a,NULL);
    if(op==47)return type_new(c->ctx,APX_BOOL,NULL,NULL);
    if(op==48||op==49)return type_new(c->ctx,APX_ARRAY,op==48?str:left->a,NULL);
    return left;
}
static ApxType *infer_call(Checker *c, ApxExpr *e, ApxType *expected) {
    int op = apx_builtin_opcode(e->text); ApxArg *a = e->args; ApxType *left, *primitive; size_t count = 2;
    e->opcode = op;
    if (op >= 30 && op <= 54) return infer_pure(c,e,expected);
    if(op>=1001&&op<=1006){
        int bad=0;positional(c,e,op==1004?0:op==1002?2:1);
        ApxType *str=type_new(c->ctx,APX_STR,NULL,NULL),*i64=type_new(c->ctx,APX_I64,NULL,NULL);
        for(ApxArg *arg=a;arg;arg=arg->next)if(poisoned(infer(c,arg->expr,op==1005?i64:str)))bad=1;
        return bad?&poison_type:type_new(c->ctx,APX_RESULT,(op==1002||op==1004)?i64:str,str);
    }
    if (!op) {
        ApxDecl *d = apx_find_decl(c->program, e->text); ApxFunction *f;
        if (d && !d->is_enum) {
            ApxType *t; ApxArg *arg; named_arguments(c, e, d->fields); e->record = d;
            for (arg = e->args; arg; arg = arg->next) if (poisoned(arg->expr->type)) return &poison_type;
            t = type_new(c->ctx, APX_NAMED, NULL, NULL); t->name = d->name; return t;
        }
        f = apx_find_function(c->program, e->text);
        if (f) {
            ApxField *p; ApxArg *arg; size_t n = 0; int bad = 0;
            e->function = f;
            for (arg=e->args;arg;arg=arg->next)if(arg->name)
                apx_fail(c->ctx,"N104","resolve","invalid_call_form",e->span,"named args are only constructors");
            for(p=f->params;p;p=p->next)++n;
            positional(c,e,n);
            for(arg=e->args,p=f->params;arg;arg=arg->next,p=p->next) if (poisoned(infer(c,arg->expr,p->type))) bad = 1;
            return bad ? &poison_type : f->returns;
        }
        unknown(c, e->span);
    }
    if (op == 12 || op == 16 || (op >= 21 && op <= 28)) count = 1;
    if (op == 29) count = 0;
    positional(c, e, count);
    if (op <= 11) {
        ApxType *hint = (op <= 5 && expected && (expected->kind == APX_I64 || expected->kind == APX_F64)) ? expected : NULL;
        ApxType *right;
        left = infer(c, a->expr, hint); right = infer(c, a->next->expr, poisoned(left) ? (hint ? hint : &poison_type) : left);
        if (poisoned(left) || poisoned(right)) {
            /* A failed sibling suppresses relational type comparisons, not
             * the independent numeric/comparability constraint of a known
             * operand. No type is fabricated for the failed operand. */
            ApxType *known = poisoned(left) ? right : left;
            if (!poisoned(known)) {
                if ((op == 6 || op == 7) && contains_builder(known)) mismatch(c, e->span, "builders are not comparable");
                if (op != 6 && op != 7 && ((known->kind != APX_I64 && known->kind != APX_F64) || (op == 5 && known->kind != APX_I64)))
                    mismatch(c, e->span, "numeric builtin expects matching numeric operands");
            }
            return &poison_type;
        }
        if (op == 6 || op == 7) { if (contains_builder(left)) mismatch(c, e->span, "builders are not comparable"); return type_new(c->ctx, APX_BOOL, NULL, NULL); }
        if ((left->kind != APX_I64 && left->kind != APX_F64) || (op == 5 && left->kind != APX_I64)) mismatch(c, e->span, "numeric builtin expects matching numeric operands");
        return op >= 8 ? type_new(c->ctx, APX_BOOL, NULL, NULL) : left;
    }
    if (op >= 12 && op <= 15) {
        ApxArg *arg; int bad = 0; primitive = type_new(c->ctx, op == 15 ? APX_STR : APX_BOOL, NULL, NULL);
        for (arg = a; arg; arg = arg->next) if (poisoned(infer(c, arg->expr, primitive))) bad = 1;
        return bad ? &poison_type : primitive;
    }
    if (op == 16) { left = infer(c, a->expr, NULL); if (poisoned(left)) return &poison_type; if (left->kind != APX_ARRAY && left->kind != APX_STR && left->kind != APX_MAP) mismatch(c, e->span, "length expects array, map or str"); return type_new(c->ctx, APX_I64, NULL, NULL); }
    if (op == 17) { ApxType *index; left = infer(c, a->expr, NULL); index = infer(c, a->next->expr, type_new(c->ctx, APX_I64, NULL, NULL)); if (!poisoned(left) && left->kind != APX_ARRAY) mismatch(c, e->span, "index expects array"); if (poisoned(left) || poisoned(index)) return &poison_type; return left->a; }
    if (op == 18) {
        ApxExpr *field = a->next->expr; ApxDecl *d; ApxField *f;
        left = infer(c, a->expr, NULL);
        if (field->kind != APX_VAR_EXPR && field->kind != APX_STRING_EXPR) mismatch(c, field->span, "field name must be an identifier or string");
        if (poisoned(left)) return &poison_type;
        if (left->kind != APX_NAMED) mismatch(c, e->span, "field access expects struct");
        d = apx_find_decl(c->program, left->name); if (!d) unknown(c, e->span);
        if (d->is_enum) mismatch(c, e->span, "field access expects struct");
        if (field->kind == APX_STRING_EXPR && strlen(field->text) != field->length) unknown(c, field->span);
        f = find_field(d->fields, field->text); if (!f) unknown(c, field->span); return f->type;
    }
    if (op == 19) {
        if (a->expr->kind != APX_TYPE_EXPR) mismatch(c, e->span, "buf first argument must be a type marker");
        if (!validate_type_all(c, a->expr->marker, a->expr->span, 0)) {
            infer(c, a->next->expr, type_new(c->ctx, APX_I64, NULL, NULL)); return &poison_type;
        }
        if (poisoned(infer(c, a->next->expr, type_new(c->ctx, APX_I64, NULL, NULL)))) return &poison_type;
        return type_new(c->ctx, APX_BUILDER, a->expr->marker, NULL);
    }
    if (op >= 20 && op <= 23) {
        left = infer(c, a->expr, NULL);
        if (poisoned(left)) { if (op == 20) infer(c, a->next->expr, &poison_type); return &poison_type; }
        if (left->kind != APX_BUILDER) mismatch(c, e->span, "builder builtin expects builder");
        if (op == 20) { if (poisoned(infer(c, a->next->expr, left->a))) return &poison_type; return left; }
        if (op == 21) return type_new(c->ctx, APX_ARRAY, left->a, NULL);
        return type_new(c->ctx, APX_I64, NULL, NULL);
    }
    if (op == 24 || op == 25) {
        left = infer(c, a->expr, NULL); if (poisoned(left)) return &poison_type; if (left->kind != APX_I64 && left->kind != APX_F64) mismatch(c, e->span, "numeric cast expects number");
        return type_new(c->ctx, op == 24 ? APX_I64 : APX_F64, NULL, NULL);
    }
    if (expected && poisoned(expected)) { if (op != 29) infer(c, a->expr, &poison_type); return &poison_type; }
    if (!expected || expected->kind != ((op == 26 || op == 27) ? APX_RESULT : APX_OPTION)) mismatch(c, e->span, "variant constructor requires an expected option or result type");
    if (op != 29 && poisoned(infer(c, a->expr, op == 27 ? expected->b : expected->a))) return &poison_type;
    return expected;
}
static ApxType *infer_value(Checker *c, ApxExpr *e, ApxType *expected) {
    ApxType *type = NULL;
    switch (e->kind) {
    case APX_INT_EXPR: type = type_new(c->ctx, APX_I64, NULL, NULL); break;
    case APX_FLOAT_EXPR: type = type_new(c->ctx, APX_F64, NULL, NULL); break;
    case APX_BOOL_EXPR: type = type_new(c->ctx, APX_BOOL, NULL, NULL); break;
    case APX_STRING_EXPR: type = type_new(c->ctx, APX_STR, NULL, NULL); break;
    case APX_VAR_EXPR: { ApxBinding *b = lookup(c, e->text, e->span); e->binding = b->id; type = b->type; break; }
    case APX_HOLE_EXPR:
        c->ctx->error_expected = poisoned(expected) ? NULL : expected; c->ctx->error_scope = c->scope;
        apx_fail(c->ctx, "H001", "type", "typed_hole", e->span, "typed hole is not executable"); break;
    case APX_ARRAY_EXPR: {
        ApxType *inner = NULL; ApxArg *a;
        int bad = 0;
        if (expected && !poisoned(expected)) { if (expected->kind != APX_ARRAY) mismatch(c, e->span, "expected non-array type, got array"); inner = expected->a; }
        else if (expected) inner = &poison_type;
        if (!inner && !e->args) mismatch(c, e->span, "empty array needs an expected array type");
        for (a = e->args; a; a = a->next) { ApxType *actual = infer(c, a->expr, inner); if (poisoned(actual)) bad = 1; else if (!inner || poisoned(inner)) inner = actual; }
        if (bad || poisoned(inner)) return &poison_type;
        type = type_new(c->ctx, APX_ARRAY, inner, NULL); break;
    }
    case APX_CALL_EXPR: type = infer_call(c, e, expected); break;
    default: mismatch(c, e->span, "type marker is not an executable value"); break;
    }
    if (poisoned(type) || (c->ctx->collect_diagnostics && !usable_type(c, type))) return &poison_type;
    type_height(c, type, e->span, 0);
    if (expected && !poisoned(expected) && !type_equal(type, expected)) mismatch(c, c->expected_span, "expression type does not match expected type");
    e->type = type; return type;
}
static void infer_unvisited_arguments(Checker *c, ApxExpr *e, ApxType *expected, const char *primary_code) {
    ApxArg *a; ApxField *param = NULL, *fields = NULL; int op = 0;
    if (e->kind == APX_CALL_EXPR) {
        ApxFunction *f = apx_find_function(c->program, e->text);
        ApxDecl *d = apx_find_decl(c->program, e->text);
        op = apx_builtin_opcode(e->text);
        if (f) param = f->params;
        if (d && !d->is_enum) fields = d->fields;
        /* Arity and positional-vs-named form are independent. The primary
         * error still has the fail-fast API's original priority. */
        if (op && op != 54 && !strcmp(primary_code, "T101")) {
            for (a = e->args; a; a = a->next) if (a->name) {
                report_semantic(c, "N104", "resolve", "invalid_call_form", e->span, "builtin or positional call cannot use named arguments"); break;
            }
        } else if (f && !fields && !strcmp(primary_code, "N104")) {
            ApxField *p; size_t n = 0;
            for (p = f->params; p; p = p->next) ++n;
            if (e->count != n) report_semantic(c, "T101", "type", "type_mismatch", e->span, "wrong number of call arguments");
        }
    }
    for (a = e->args; a; a = a->next) {
        ApxType *hint = NULL;
        if (param) { hint = param->type; param = param->next; }
        if (fields && a->name) { ApxField *field = find_field(fields, a->name); if (field) hint = field->type; }
        if (e->kind == APX_ARRAY_EXPR && expected && expected->kind == APX_ARRAY) hint = expected->a;
        if (op <= 5 && op && expected && (expected->kind == APX_I64 || expected->kind == APX_F64)) hint = expected;
        if (op >= 12 && op <= 15) hint = type_new(c->ctx, op == 15 ? APX_STR : APX_BOOL, NULL, NULL);
        if ((op == 17 || op == 19) && a != e->args) hint = type_new(c->ctx, APX_I64, NULL, NULL);
        if (a->expr->type || a->expr->kind == APX_TYPE_EXPR || (op == 18 && a != e->args)) continue;
        infer(c, a->expr, hint);
    }
}
static ApxType *infer(Checker *c, ApxExpr *e, ApxType *expected) {
    Checker saved = *c; ApxRecovery frame; size_t before = c->ctx->diagnostic_count;
    ApxType *volatile hint = expected;
    if (!c->ctx->collect_diagnostics) return infer_value(c, e, expected);
    if (hint && !usable_type(c, hint)) hint = &poison_type;
    frame.previous = c->ctx->recovery; c->ctx->recovery = &frame;
    if (!setjmp(frame.jump)) {
        ApxType *type = infer_value(c, e, hint);
        e->type = c->ctx->diagnostic_count != before || !usable_type(c, type) ? &poison_type : type;
    } else {
        const char *primary_code = c->ctx->error_code;
        collect_error(c->ctx); *c = saved; e->type = &poison_type;
        c->ctx->recovery = frame.previous;
        infer_unvisited_arguments(c, e, hint, primary_code);
    }
    c->ctx->recovery = frame.previous; return e->type;
}
static int block_exits(ApxStmt *body) {
    ApxStmt *s;
    for (s = body; s; s = s->next) {
        if (s->kind == 'R' || s->kind == 'Z') return 1;
        if (s->kind == 'C' && s->arms) { ApxArm *a; int all = 1; for (a = s->arms; a; a = a->next) if (!block_exits(a->body)) all = 0; if (all) return 1; }
    }
    return 0;
}
static void check_block(Checker *c, ApxStmt *body);
static void scoped_block(Checker *c, ApxStmt *body, ApxField *fields) {
    ApxBinding *old = c->scope; ApxField *f; ++c->depth;
    for (f = fields; f; f = f->next) f->binding = bind_recover(c, f->name, f->type, f->span);
    check_block(c, body); c->scope = old; --c->depth;
}
static void unchecked_arm_body(Checker *c, ApxArm *a) {
    ApxField *b;
    for (b = a->bindings; b; b = b->next) b->type = &poison_type;
    scoped_block(c, a->body, a->bindings);
}
static void case_arm_duplicate(Checker *c, ApxStmt *s, ApxArm *a) {
    ApxArm *previous;
    for (previous = s->arms; previous != a; previous = previous->next)
        if (!strcmp(a->name, previous->name)) { report_duplicate(c, a->span); break; }
}
static void check_case_arm(Checker *c, ApxStmt *s, ApxArm *a, ApxType *type, ApxDecl *decl) {
    ApxType *payload = NULL; ApxField *b, *field = NULL;
    ApxVariant *v = NULL; size_t n = 0, count = 0;
    case_arm_duplicate(c, s, a);
    if (decl) {
        for (v = decl->variants; v; v = v->next) if (!strcmp(v->name, a->name)) break;
        if (!v) unknown(c, a->span);
        field = v->fields; for (b = field; b; b = b->next) ++count;
    } else if (!strcmp(a->name, type->kind == APX_OPTION ? "some" : "ok")) { payload = type->a; count = 1; }
    else if (!strcmp(a->name, type->kind == APX_OPTION ? "none" : "err")) {
        if (type->kind == APX_RESULT) { payload = type->b; count = 1; }
    } else unknown(c, a->span);
    for (b = a->bindings; b; b = b->next) ++n;
    if (n > count) apx_fail(c->ctx, "N104", "resolve", "invalid_call_form", a->span, "too many case payload bindings");
    for (b = a->bindings; b; b = b->next) { b->type = decl ? field->type : payload; if (field) field = field->next; }
    scoped_block(c, a->body, a->bindings);
}
static void check_case_arm_recover(Checker *c, ApxStmt *s, ApxArm *a, ApxType *type, ApxDecl *decl) {
    Checker saved = *c; ApxRecovery frame;
    frame.previous = c->ctx->recovery; c->ctx->recovery = &frame;
    if (!setjmp(frame.jump)) check_case_arm(c, s, a, type, decl);
    else {
        collect_error(c->ctx); *c = saved; c->ctx->recovery = frame.previous;
        unchecked_arm_body(c, a);
    }
    c->ctx->recovery = frame.previous;
}
static void check_case(Checker *c, ApxStmt *s) {
    ApxType *type = infer(c, s->expr, NULL); ApxArm *a; ApxDecl *decl = NULL;
    const char *first = NULL, *second = NULL; int saw_first = 0, saw_second = 0;
    if (poisoned(type)) { for (a = s->arms; a; a = a->next) { case_arm_duplicate(c, s, a); unchecked_arm_body(c, a); } return; }
    if (type->kind == APX_OPTION) { first = "some"; second = "none"; }
    else if (type->kind == APX_RESULT) { first = "ok"; second = "err"; }
    else if (type->kind == APX_NAMED) { decl = apx_find_decl(c->program, type->name); if (!decl) unknown(c, s->span); if (!decl->is_enum) mismatch(c, s->span, "case expects option, result or enum"); }
    else mismatch(c, s->span, "case expects option, result or enum");
    for (a = s->arms; a; a = a->next) {
        if (first && !strcmp(a->name, first)) saw_first = 1;
        if (second && !strcmp(a->name, second)) saw_second = 1;
        if (c->ctx->collect_diagnostics) check_case_arm_recover(c, s, a, type, decl);
        else check_case_arm(c, s, a, type, decl);
    }
    if (decl) {
        ApxVariant *v; for (v = decl->variants; v; v = v->next) { for (a = s->arms; a; a = a->next) if (!strcmp(a->name, v->name)) break;
            if (!a) report_semantic(c, "T103", "type", "non_exhaustive_case", s->span, "missing case arm");
        }
    } else if (!saw_first || !saw_second) report_semantic(c, "T103", "type", "non_exhaustive_case", s->span, "missing case arm");
}
static void check_statement(Checker *c, ApxStmt *s) {
    c->expected_span=s->span; switch (s->kind) {
    case 'L': { ApxType *t, *hint = s->annotation; if (hint && !validate_type_all(c, hint, s->span, 0)) hint = &poison_type; t = infer(c, s->expr, hint); if (hint && poisoned(hint)) t = &poison_type; s->binding = bind(c, s->name, t, s->span); break; }
    case 'S': { ApxBinding *b = lookup(c, s->name, s->span); s->binding = b->id; infer(c, s->expr, b->type); break; }
    case 'R': if (c->function->is_test) mismatch(c, s->span, "source tests cannot return values"); infer(c, s->expr, c->function->returns); break;
    case '?': case 'W': infer(c, s->expr, type_new(c->ctx, APX_BOOL, NULL, NULL)); scoped_block(c, s->body, NULL); break;
    case 'E': { ApxType *t = infer(c, s->expr, NULL); ApxBinding *old = c->scope;
        if (!poisoned(t) && t->kind != APX_ARRAY) mismatch(c, s->span, "each expects an array");
        ++c->depth; s->binding = bind_recover(c, s->name, poisoned(t) ? &poison_type : t->a, s->span); check_block(c, s->body); c->scope = old; --c->depth; break;
    }
    case 'C': check_case(c, s); break;
    case 'A': infer(c, s->expr, type_new(c->ctx, APX_BOOL, NULL, NULL)); break;
    case 'Z': break;
    }
}
static void check_statement_recover(Checker *c, ApxStmt *s) {
    Checker saved = *c; ApxRecovery frame;
    frame.previous = c->ctx->recovery; c->ctx->recovery = &frame;
    if (!setjmp(frame.jump)) check_statement(c, s);
    else {
        collect_error(c->ctx); *c = saved; c->ctx->recovery = frame.previous;
        /* A failed assignment target/statement header must not hide an
         * independent expression or body. Never retry already-checked nodes. */
        c->expected_span = s->span;
        if (!s->expr->type && s->kind != 'Z') infer(c, s->expr, (s->kind == 'S' || s->kind == 'R') ? &poison_type : NULL);
        if (s->kind == 'E') {
            ApxBinding *old = c->scope; ++c->depth;
            s->binding = bind_recover(c, s->name, &poison_type, s->span);
            check_block(c, s->body); c->scope = old; --c->depth;
        } else if (s->body) scoped_block(c, s->body, NULL);
        if (s->kind == 'C') { ApxArm *a; for (a = s->arms; a; a = a->next) { case_arm_duplicate(c, s, a); unchecked_arm_body(c, a); } }
        /* An invalid initializer is known but has no teachable type. */
        if (s->kind == 'L') {
            ApxBinding *b;
            for (b = c->scope; b; b = b->next) if (!strcmp(b->name, s->name)) break;
            if (!b) s->binding = bind(c, s->name, &poison_type, s->span);
        }
    }
    c->ctx->recovery = frame.previous;
}
static void check_block(Checker *c, ApxStmt *body) {
    ApxStmt *s;
    for (s = body; s; s = s->next) {
        if (c->ctx->collect_diagnostics) check_statement_recover(c, s);
        else check_statement(c, s);
    }
}
static void check_fields(Checker *c, ApxField *fields) {
    ApxField *f, *p; for (f = fields; f; f = f->next) {
        for (p = fields; p != f; p = p->next) if (!strcmp(p->name, f->name)) { report_duplicate(c, f->span); break; }
        if (!validate_type_all(c, f->type, f->span, 1)) f->type = &poison_type;
    }
}
static int declaration_usable(Checker *c, ApxDecl *d) {
    ApxField *f; ApxVariant *v;
    for (f = d->fields; f; f = f->next) if (!usable_type(c, f->type)) return 0;
    for (v = d->variants; v; v = v->next)
        for (f = v->fields; f; f = f->next) if (!usable_type(c, f->type)) return 0;
    return 1;
}
static void propagate_invalid_declarations(Checker *c) {
    int changed; ApxDecl *d;
    do {
        changed = 0;
        for (d = c->program->types; d; d = d->next)
            if (d->visit != 3 && !declaration_usable(c, d)) { d->visit = 3; changed = 1; }
    } while (changed);
}
static void check_expansion_recover(Checker *c, ApxDecl *d) {
    Checker saved = *c; ApxRecovery frame; ApxType t;
    memset(&t, 0, sizeof t); t.kind = APX_NAMED; t.name = d->name;
    frame.previous = c->ctx->recovery; c->ctx->recovery = &frame;
    if (!setjmp(frame.jump)) expanded_height(c, &t, d->span, 0);
    else {
        ApxDecl *item; collect_error(c->ctx); *c = saved;
        /* Failed DFS paths are invalid, not still visiting. This prevents
         * later roots from reporting the same cycle as another error. */
        for (item = c->program->types; item; item = item->next)
            if (item->visit == 1) item->visit = 3;
        d->visit = 3;
    }
    c->ctx->recovery = frame.previous;
}
static void check_program(ApxContext *ctx, ApxProgram *p) {
    Checker c; ApxDecl *d, *dp; ApxFunction *f, *fp; memset(&c, 0, sizeof c); c.ctx = ctx; c.program = p;
    for (d = p->types; d; d = d->next) {
        ApxVariant *v, *vp;
        for (dp = p->types; dp != d; dp = dp->next) if (!strcmp(dp->name, d->name)) { report_duplicate(&c, d->name_span); break; }
        check_fields(&c, d->fields);
        for (v = d->variants; v; v = v->next) { for (vp = d->variants; vp != v; vp = vp->next) if (!strcmp(vp->name, v->name)) { report_duplicate(&c, v->span); break; } check_fields(&c, v->fields); }
    }
    if (ctx->collect_diagnostics) {
        propagate_invalid_declarations(&c);
        for (d = p->types; d; d = d->next) if (d->visit != 3) check_expansion_recover(&c, d);
        propagate_invalid_declarations(&c);
    } else for (d = p->types; d; d = d->next) { ApxType t; memset(&t, 0, sizeof t); t.kind = APX_NAMED; t.name = d->name; expanded_height(&c, &t, d->span, 0); }
    for (f = p->functions; f; f = f->next) {
        ApxField *param;
        if (apx_find_decl(p, f->name)) report_duplicate(&c, f->span);
        for (fp = p->functions; fp != f; fp = fp->next) if (!strcmp(fp->name, f->name)) { report_duplicate(&c, f->span); break; }
        c.function = f; c.scope = NULL; c.depth = 0; c.next_binding = 0;
        check_fields(&c, f->params);
        if (!validate_type_all(&c, f->returns, f->returns->span, 1)) f->returns = &poison_type;
        for (param = f->params; param; param = param->next) {
            ApxField *previous;
            for (previous = f->params; previous != param; previous = previous->next) if (!strcmp(previous->name, param->name)) break;
            if (previous == param) param->binding = bind(&c, param->name, usable_type(&c, param->type) ? param->type : &poison_type, param->span);
        }
        check_block(&c, f->body); f->bindings = c.next_binding;
        if (!block_exits(f->body)) report_semantic(&c, "T105", "type", "missing_return", f->name_span, "function may not return a value");
    }
    for (f = p->tests; f; f = f->next) {
        if (apx_find_decl(p, f->name) || apx_find_function(p, f->name)) report_duplicate(&c, f->span);
        for (fp = p->tests; fp != f; fp = fp->next) if (!strcmp(fp->name, f->name)) { report_duplicate(&c, f->span); break; }
        c.function = f; c.scope = NULL; c.depth = 0; c.next_binding = 0; check_block(&c, f->body); f->bindings = c.next_binding;
    }
}
static void source_check(ApxContext *ctx, const char *source) {
    size_t n = 0, i;
    if (!source) apx_fail(ctx, "P001", "parse", "empty_source", no_span, "source is missing");
    while (n <= APX_MAX_SOURCE && source[n]) ++n;
    if (n > APX_MAX_SOURCE) apx_fail(ctx, "P006", "parse", "source_limit_exceeded", no_span, "source exceeds 65536 bytes");
    ctx->source = source; ctx->source_length = n;
    for (i = 0; i < n;) { size_t used = 1; unsigned scalar = utf8_scalar((const unsigned char *)source + i, n - i, &used);
        if (scalar == 0xffffffffu) { ApxSpan span = {i, i + 1}; apx_fail(ctx, "P005", "parse", "invalid_literal", span, "source must contain valid UTF-8 Unicode scalars"); }
        i += used;
    }
}
static char *owned_copy(ApxContext *ctx, const char *s) {
    size_t n = strlen(s); char *out = malloc(n + 1);
    if (!out) apx_fail(ctx, "P006", "compile", "out_of_memory", no_span, "compiler allocation failed");
    memcpy(out, s, n + 1); return out;
}
static void location(ApxContext *ctx, size_t byte, unsigned *line, unsigned *column) {
    size_t i; *line = 1; *column = 1;
    if (byte > ctx->source_length) byte = ctx->source_length;
    for (i = 0; i < byte; ++i) { unsigned char ch = (unsigned char)ctx->source[i];
        if (ch == '\n') { ++*line; *column = 1; } else if ((ch & 0xc0) != 0x80) ++*column;
    }
}
static char *diagnostic_json(ApxContext *original) {
    ApxContext *volatile ctx = calloc(1, sizeof *ctx); char *volatile out = NULL;
    if (!ctx) return NULL;
    if (!setjmp(ctx->escape)) {
        ApxBuffer b; unsigned sl, sc, el, ec; ApxBinding *binding; int first = 1;
        apx_buf_init(&b, ctx); apx_buf_put(&b, "{\"schema\":\"apodexis.diagnostic.v1\",\"severity\":\"error\",\"code\":");
        apx_json_quote(&b, original->error_code, strlen(original->error_code)); apx_buf_put(&b, ",\"phase\":");
        apx_json_quote(&b, original->error_phase, strlen(original->error_phase)); apx_buf_put(&b, ",\"kind\":");
        apx_json_quote(&b, original->error_kind, strlen(original->error_kind)); apx_buf_put(&b, ",\"message\":");
        apx_json_quote(&b, original->error_message, strlen(original->error_message));
        location(original, original->error_span.start, &sl, &sc); location(original, original->error_span.end, &el, &ec);
        apx_buf_printf(&b, ",\"span\":{\"start_byte\":%zu,\"end_byte\":%zu,\"start_line\":%u,\"start_column\":%u,\"end_line\":%u,\"end_column\":%u}",
                       original->error_span.start, original->error_span.end, sl, sc, el, ec);
        if (!strcmp(original->error_kind, "typed_hole")) {
            apx_buf_put(&b, ",\"expected_type\":");
            if (original->error_expected) { ApxBuffer type; apx_buf_init(&type, ctx); apx_type_text(&type, original->error_expected); apx_json_quote(&b, type.data, type.length); }
            else apx_buf_put(&b, "null");
            apx_buf_put(&b, ",\"in_scope\":[");
            for (binding = original->error_scope; binding; binding = binding->next) {
                ApxBinding *newer; ApxBuffer type;
                for (newer = original->error_scope; newer != binding; newer = newer->next) if (!strcmp(newer->name, binding->name)) break;
                if (newer != binding || poisoned(binding->type)) continue;
                if (!first) apx_buf_put(&b, ",");
                first = 0;
                apx_buf_put(&b, "{\"name\":"); apx_json_quote(&b, binding->name, strlen(binding->name)); apx_buf_put(&b, ",\"type\":");
                apx_buf_init(&type, ctx); apx_type_text(&type, binding->type); apx_json_quote(&b, type.data, type.length); apx_buf_put(&b, "}");
            }
            apx_buf_put(&b, "]");
        }
        apx_buf_put(&b, "}"); out = owned_copy(ctx, b.data);
    }
    context_free(ctx); free(ctx); return out;
}
static int compile_source_mode(const char *source, char **il_out, char **canonical_out, char **diagnostic_out,int tests) {
    ApxContext *volatile ctx = calloc(1, sizeof *ctx); volatile int result = 1;
    if (il_out) *il_out = NULL;
    if (canonical_out) *canonical_out = NULL;
    if (diagnostic_out) *diagnostic_out = NULL;
    if (!ctx) return 1;
    if (!setjmp(ctx->escape)) {
        ApxProgram *p; char *il = NULL;
        source_check(ctx, source); p = parse_program(ctx); p->canonical = format_program(ctx, p);
        memcpy(p->source_hash, "apx1:", 5); apx_sha256(p->canonical, strlen(p->canonical), p->source_hash + 5);
        /* Keep parse identity available for semantic/codegen diagnostics. */
        if (canonical_out) *canonical_out = owned_copy(ctx, p->canonical);
        check_program(ctx, p);
        if (il_out) il = tests?apx_qbe_emit_tests(ctx,p):apx_qbe_emit(ctx, p);
        if (il_out) *il_out = owned_copy(ctx, il);
        result = 0;
    } else {
        if (il_out) { free(*il_out); *il_out = NULL; }
        if (diagnostic_out) *diagnostic_out = diagnostic_json(ctx);
    }
    context_free(ctx); free(ctx); return result;
}
int apx_compile_source(const char *source,char **il,char **canonical,char **diagnostic){return compile_source_mode(source,il,canonical,diagnostic,0);}
int apx_compile_tests(const char *source,char **il,char **canonical,char **diagnostic){return compile_source_mode(source,il,canonical,diagnostic,1);}
static char *diagnostics_array(ApxContext *original, int fatal) {
    ApxContext *volatile ctx = calloc(1, sizeof *ctx); char *volatile out = NULL;
    char *volatile item = NULL;
    if (!ctx) return NULL;
    if (!setjmp(ctx->escape)) {
        ApxBuffer b; ApxDiagnostic *d; int first = 1;
        apx_buf_init(&b, ctx); apx_buf_put(&b, "[");
        if (fatal) {
            item = diagnostic_json(original);
            if (!item) apx_fail(ctx, "P006", "compile", "out_of_memory", no_span, "diagnostic rendering failed");
            apx_buf_put(&b, item); free(item); item = NULL;
        } else for (d = original->diagnostics; d; d = d->next) {
            ApxContext view;
            memset(&view, 0, sizeof view); view.source = original->source;
            view.source_length = original->source_length;
            view.error_code = d->code; view.error_phase = d->phase;
            view.error_kind = d->kind; view.error_message = d->message;
            view.error_span = d->span; view.error_expected = d->expected;
            view.error_scope = d->scope;
            item = diagnostic_json(&view);
            if (!item) apx_fail(ctx, "P006", "compile", "out_of_memory", no_span, "diagnostic rendering failed");
            if (!first) apx_buf_put(&b, ",");
            first = 0; apx_buf_put(&b, item); free(item); item = NULL;
        }
        apx_buf_put(&b, "]"); out = owned_copy(ctx, b.data);
    }
    free(item); context_free(ctx); free(ctx); return out;
}
static char *diagnostics_resource_failure(void) {
    const char *text = "[{\"schema\":\"apodexis.diagnostic.v1\",\"severity\":\"error\",\"code\":\"P006\",\"phase\":\"compile\",\"kind\":\"compiler_resource_limit\",\"message\":\"diagnostic output unavailable; checking is incomplete\",\"span\":{\"start_byte\":0,\"end_byte\":0,\"start_line\":1,\"start_column\":1,\"end_line\":1,\"end_column\":1}}]";
    size_t n = strlen(text); char *out = malloc(n + 1);
    if (out) memcpy(out, text, n + 1);
    return out;
}
int apx_check_source(const char *source, char **canonical_out, char **diagnostics_out) {
    ApxContext *volatile ctx = calloc(1, sizeof *ctx);
    volatile int result = 1, fatal = 0;
    if (canonical_out) *canonical_out = NULL;
    if (diagnostics_out) *diagnostics_out = NULL;
    if (!ctx) { if (diagnostics_out) *diagnostics_out = diagnostics_resource_failure(); return 1; }
    ctx->collect_diagnostics = 1; ctx->diagnostics_tail = &ctx->diagnostics;
    if (!setjmp(ctx->escape)) {
        ApxProgram *p;
        source_check(ctx, source); p = parse_program(ctx); p->canonical = format_program(ctx, p);
        memcpy(p->source_hash, "apx1:", 5); apx_sha256(p->canonical, strlen(p->canonical), p->source_hash + 5);
        if (canonical_out) *canonical_out = owned_copy(ctx, p->canonical);
        check_program(ctx, p); result = ctx->diagnostic_count != 0;
    } else { fatal = 1; ctx->recovery = NULL; }
    if (diagnostics_out) {
        *diagnostics_out = diagnostics_array(ctx, fatal);
        if (!*diagnostics_out) { *diagnostics_out = diagnostics_resource_failure(); result = 1; }
    }
    context_free(ctx); free(ctx); return result;
}
char *apx_format_source(const char *source, char **diagnostic_out) {
    ApxContext *volatile ctx = calloc(1, sizeof *ctx); char *volatile out = NULL;
    if (diagnostic_out) *diagnostic_out = NULL;
    if (!ctx) return NULL;
    if (!setjmp(ctx->escape)) { ApxProgram *p; source_check(ctx, source); p = parse_program(ctx); out = owned_copy(ctx, format_program(ctx, p)); }
    else if(diagnostic_out)*diagnostic_out=diagnostic_json(ctx);
    context_free(ctx); free(ctx); return out;
}

int apx_describe_source(const char *source,char **explain_out,char **contract_out,
                        char **pseudocode_out,char **diagnostic_out){
    ApxContext *volatile ctx=calloc(1,sizeof *ctx);volatile int result=1;
    if(explain_out)*explain_out=NULL;
    if(contract_out)*contract_out=NULL;
    if(pseudocode_out)*pseudocode_out=NULL;
    if(diagnostic_out)*diagnostic_out=NULL;
    if(!ctx)return 1;
    if(!setjmp(ctx->escape)){
        source_check(ctx,source);ApxProgram *p=parse_program(ctx);p->canonical=format_program(ctx,p);
        memcpy(p->source_hash,"apx1:",5);apx_sha256(p->canonical,strlen(p->canonical),p->source_hash+5);
        check_program(ctx,p);char *explanation=NULL,*contract=NULL,*pseudocode=NULL;
        apx_metadata_render(ctx,p,explain_out?&explanation:NULL,contract_out?&contract:NULL,pseudocode_out?&pseudocode:NULL);
        if(explain_out)*explain_out=owned_copy(ctx,explanation);
        if(contract_out)*contract_out=owned_copy(ctx,contract);
        if(pseudocode_out)*pseudocode_out=owned_copy(ctx,pseudocode);
        result=0;
    }else{
        if(explain_out){free(*explain_out);*explain_out=NULL;}
        if(contract_out){free(*contract_out);*contract_out=NULL;}
        if(pseudocode_out){free(*pseudocode_out);*pseudocode_out=NULL;}
        if(diagnostic_out)*diagnostic_out=diagnostic_json(ctx);
    }
    context_free(ctx);free(ctx);return result;
}
