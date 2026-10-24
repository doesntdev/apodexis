#ifndef APX_MODULES_H
#define APX_MODULES_H
#include <stddef.h>
/* Inline, caller-owned module sources; names are ASCII module keys, not paths.
 * Returns malloc-owned assembled source or one JSON diagnostic. No I/O. */
typedef struct { const char *name; const char *source; } ApxModuleSource;
int apx_modules_resolve(const char *entry,const ApxModuleSource *modules,size_t count,
                        char **assembled,char **diagnostic);
/* CLI-only loader: imports resolve to <entry-directory>/<key>.apx. Imports
 * cannot supply paths; files must be regular and not symlinks. */
int apx_modules_load_file(const char *entry_path,char **assembled,char **diagnostic);
#endif
