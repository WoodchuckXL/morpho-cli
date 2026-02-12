/** @file main.c
 *  @author T J Atherton
 *
 *  @brief Main entry point and process options
 */

#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>

#include <morpho.h>

#include "cli.h"
#include "debugger.h"

/** Context passed to option handlers that need argc/argv (e.g. eval). */
typedef struct {
    int argc;
    const char **argv;
    int *idx;
} opt_ctx;

/** Option handler. arg is NULL for options that take no argument.
 *  @returns true to continue (run file/repl), false to exit without running. */
typedef bool (*optionfn)(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx);

static bool opt_version(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx) {
    (void)opt; (void)arg; (void)flags; (void)ctx;
    version v;
    morpho_version(&v);
    char buf[VERSION_MAXSTRINGLENGTH];
    version_tostring(&v, VERSION_MAXSTRINGLENGTH, buf);
    printf("Morpho v%s\n", buf);
    return false;
}

static bool opt_disassembleonly(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx) {
    (void)opt; (void)arg; (void)ctx;
    *flags ^= CLI_RUN;
    *flags |= CLI_DISASSEMBLE;
    return false;
}

static bool opt_disassemblelist(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx) {
    (void)opt; (void)arg; (void)ctx;
    *flags |= CLI_DISASSEMBLE | CLI_DISASSEMBLESHOWSRC;
    return true;
}

static bool opt_disassemble(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx) {
    (void)opt; (void)arg; (void)ctx;
    *flags |= CLI_DISASSEMBLE;
    return true;
}

static bool opt_debug(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx) {
    (void)opt; (void)arg; (void)ctx;
    *flags |= CLI_DEBUG;
    return true;
}

static bool opt_optimize(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx) {
    (void)opt; (void)arg; (void)ctx;
    *flags |= CLI_OPTIMIZE;
    return true;
}

static bool opt_profile(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx) {
    (void)opt; (void)arg; (void)ctx;
#ifdef MORPHO_PROFILER
    *flags |= CLI_PROFILE;
#endif
    return true;
}

static bool opt_workers(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx) {
    const char *c = arg ? arg : (opt + 1);
    while (*c && *c != '=' && !isdigit((unsigned char)*c)) c++;
    if (*c == '=') c++;
    int n = isdigit((unsigned char)*c) ? atoi(c) : 0;
    if (n < 0) n = 0;
    morpho_setthreadnumber(n);
    (void)flags;
    return true;
}

static bool opt_check(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx) {
    (void)opt; (void)arg; (void)ctx;
    *flags &= ~CLI_RUN; /* Clear RUN flag - compile only, don't execute */
    return true;
}

static bool opt_interactive(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx) {
    (void)opt; (void)arg; (void)ctx;
    *flags |= CLI_INTERACTIVE; // Enter REPL after running file 
    return true;
}

static bool opt_list(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx) {
    (void)opt; (void)flags; (void)ctx;
    if (!arg) return false;
    
    char *src = cli_loadsource(arg);
    if (src) {
        cli_list(src, 1, INT_MAX);
        MORPHO_FREE(src);
    } else fprintf(stderr, "morpho: Could not open file '%s'\n", arg);
    return false; // Don't run program after listing 
}

static bool opt_eval(const char *opt, const char *arg, clioptions *flags, opt_ctx *ctx) {
    (void)opt;
    clidebugger_initialize();
    if (ctx && ctx->idx && ctx->argc - *ctx->idx - 1 > 0)
        morpho_setargs(ctx->argc - *ctx->idx - 1, ctx->argv + *ctx->idx + 1);
    cli_runstring(arg, *flags);
    return false;
}

typedef struct {
    const char *s, *l;
    bool takes_arg;
    optionfn fn;
} option_t;

static const option_t opt_table[] = {
    { "-D",       NULL,            false, opt_disassembleonly },
    { "-dl",      NULL,            false, opt_disassemblelist },
    { "-d",       "--disassemble", false, opt_disassemble },
    { "-debug",   "--debug",       false, opt_debug },
    { "-c",       "--check",       false, opt_check },
    { "-e",       "--eval",        true,  opt_eval },
    { "-i",       "--interactive", false, opt_interactive },
    { "-l",       "--list",        true,  opt_list },
    { "-O",       "--optimize",    false, opt_optimize },
#ifdef MORPHO_PROFILER
    { "-profile", "--profile",     false, opt_profile },
#endif
    { "-v",       "--version",     false, opt_version },
    { "-w",       "--workers",     false, opt_workers },
    { NULL,       NULL,            false, NULL },
};

/** Parse one option at argv[*idx]. If option takes an argument, consumes *idx+1 and advances *idx. */
static bool parse_option(int argc, const char *argv[], int *idx, clioptions *flags) {
    const char *arg = argv[*idx], *opt_arg = NULL;
    for (int j = 0; opt_table[j].s || opt_table[j].l; j++) {
        const char *s = opt_table[j].s, *l = opt_table[j].l;
        if ((s && strncmp(arg, s, strlen(s)) == 0) || (l && strncmp(arg, l, strlen(l)) == 0)) {
            if (opt_table[j].takes_arg) {
                if (*idx + 1 >= argc) {
                    fprintf(stderr, "morpho: %s requires an argument.\n", arg);
                    return false;
                }
                opt_arg = argv[*idx + 1];
                (*idx)++;
            }
            opt_ctx ctx = { argc, argv, idx };
            return opt_table[j].fn(arg, opt_arg, flags, &ctx);
        }
    }
    printf("Unknown option %s.\n", arg);
    return false;
}

int main(int argc, const char *argv[]) {
    clioptions opt = CLI_RUN;
    const char *file = NULL;
    int i = 1;
    bool run = true;

    morpho_initialize();

    for (; i < argc && !file; i++) {
        const char *arg = argv[i];
        if (arg && arg[0] == '-') {
            run &= parse_option(argc, argv, &i, &opt);
        } else if (arg) file = arg;
    }

    if (run) {
        clidebugger_initialize();
        if (i < argc) morpho_setargs(argc - i - 1, argv + i); // Pass unused args to morpho
        (file ? cli_run(file, opt) : cli(opt));
    }

    morpho_finalize();
    return 0;
}
