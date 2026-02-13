/** @file cli.c
 *  @author T J Atherton
 *
 *  @brief Command line interface
*/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

#include "cli.h"

#include <parse.h>
#include <file.h>

#include "debugger.h"

#ifdef CLI_USELIBUNISTRING
    #include <unigbrk.h>
#endif

#ifdef CLI_USELIBGRAPHEME
    #include <grapheme.h>
#endif

char *cli_globalsrc=NULL;

#define CLI_BUFFERSIZE 4096

#define BLU   "\x1B[34m"
#define CYN   "\x1B[36m"
#define GRY   "\x1B[38;2;128;128;128m"
#define RESET "\x1B[0m"

#define BOLD       "\x1B[1m"
#define ITALIC     "\x1B[3m"
#define UNDERLINE  "\x1B[4m"

void inline_setutf8(void);
void inline_emit(const char *seq);
void inline_emitcolor(int color);
bool inline_getterminalwidth(int *width);
bool inline_checksupported(void);

/* **********************************************************************
 * Utility functions
 * ********************************************************************** */

void cli_emitemphasis(int emph) {
    switch (emph) {
        case CLI_NOEMPHASIS: inline_emit(RESET); break;
        case CLI_BOLD: inline_emit(BOLD); break;
        case CLI_UNDERLINE: inline_emit(UNDERLINE); break;
        case CLI_ITALIC: inline_emit(ITALIC); break;
        default: break;
    }
}

bool is_supported = false;

/** Displays several strings with a specified style using linedit */
void cli_displaywithstyle(int col, int emph, int n, ...) {
    va_list args;
    va_start(args, n);
    bool is_tty = inline_checktty() && is_supported; // Only emit escape codes if stdout is a TTY
    
    for (int i=0; i<n; i++) {
        char *str = va_arg(args, char *);
        if (is_tty) cli_emitemphasis(emph);
        if (is_tty) inline_emitcolor(col);
        printf("%s",str);
    }
    if (is_tty) inline_emit(RESET);
    
    va_end(args);
}

/** Report an error if one has occurred. */
void cli_reporterror(error *err, vm *v) {
    if (err->cat!=ERROR_NONE) {
        cli_displaywithstyle(CLI_ERRORCOLOR, CLI_NOEMPHASIS, 3, "Error '", err->id, "'");
        
        if (ERROR_ISRUNTIMEERROR(*err)) {
            cli_displaywithstyle(CLI_ERRORCOLOR, CLI_NOEMPHASIS, 3, ": ", err->msg, "\n");
            morpho_stacktrace(v);
        } else {
            if (err->line!=ERROR_POSNUNIDENTIFIABLE && err->posn!=ERROR_POSNUNIDENTIFIABLE) {
                char posnbuffer[CLI_BUFFERSIZE];
                snprintf(posnbuffer, CLI_BUFFERSIZE, " [line %u char %u", err->line, err->posn+1);
                cli_displaywithstyle(CLI_ERRORCOLOR, CLI_NOEMPHASIS, 1, posnbuffer);
                
                if (err->file) {
                    cli_displaywithstyle(CLI_ERRORCOLOR, CLI_NOEMPHASIS, 3, " in module '", err->file, "'");
                }
                
                cli_displaywithstyle(CLI_ERRORCOLOR, CLI_NOEMPHASIS, 1, "] ");
            }
            
            cli_displaywithstyle(CLI_ERRORCOLOR, CLI_NOEMPHASIS, 3, ": ", err->msg, "\n");
        }
    }
}


/** Interactive help */
void cli_help(inline_editor *edit, char *query, error *err, bool avail) {
    char *q=query;
    if (help_querylength(q, NULL)==0) {
        if (err->cat!=ERROR_NONE) {
            q=err->id;
			error_clear(err);
        } else {
            q=HELP_INDEXPAGE;
        }
    }
    
    objecthelptopic *topic = help_search(q);
    if (topic) {
        help_display(edit, topic);
    } else {
        while (isspace(*q) && *q!='\0') q++;
        printf("No help found for '%s'\n", q);
    }
}

/* **********************************************************************
 * Morpho callbacks
 * ********************************************************************** */

/** Print callback */
void cli_printcallbackfn(vm *v, void *ref, char *string) {
    inline_editor *l = (inline_editor *) ref;
    cli_displaywithstyle(CLI_DEFAULTCOLOR, CLI_BOLD, 1, string);
}

/** Input callback */
void cli_inputcallbackfn(vm *v, void *ref, morphoinputmode mode, varray_char *str) {
    if (mode==MORPHO_INPUT_KEYPRESS) {
        int key = getchar();
        if (key!=EOF) varray_charwrite(str, (char) key);
    } else {
        inline_editor *line=inline_new("");
        char *out=inline_readline(line);
        if (out) varray_charadd(str, out, (int) strlen(out));
        free(out);
        inline_free(line);
    }
}

/** Warning callback */
void cli_warningcallbackfn(vm *v, void *ref, error *err) {
    cli_displaywithstyle(CLI_WARNINGCOLOR, CLI_NOEMPHASIS, 5, "Warning '", err->id, "': ", err->msg, "\n");
}

/** Warning callback */
void cli_debuggercallbackfn(vm *v, void *ref) {
    clidebugger_enter(v);
}

/* **********************************************************************
 * Inline callbacks
 * ********************************************************************** */

/** Define colors for different token types (256-color palette) */
int palette[] = {
    CLI_DEFAULTCOLOR,                    // 0 default
    INLINE_YELLOW,                       // 1 help
    INLINE_COLOR_ANSI216(0, 1, 5),       // 2 string/integer/number literals (darker blue, visible on both backgrounds)
    INLINE_COLOR_ANSI216(0, 3, 4),       // 3 symbol (darker cyan/teal, distinct from blue)
    INLINE_MAGENTA,                      // 4 keyword
    INLINE_GRAY_ANSI(12),                // 5 comment (mid-level gray)
    INLINE_COLOR_ANSI216(5, 3, 0)        // 6 operator (amber)
};

tokentype help[] = { TOKEN_QUESTION };
tokentype literal[] = { TOKEN_STRING, TOKEN_INTERPOLATION, TOKEN_INTEGER, TOKEN_NUMBER, TOKEN_IMAG };
tokentype symbols[] = { TOKEN_SYMBOL };
tokentype keywords[] = { TOKEN_TRUE, TOKEN_FALSE, TOKEN_NIL, TOKEN_SELF, TOKEN_SUPER, TOKEN_PRINT, TOKEN_VAR, TOKEN_IF, TOKEN_ELSE, TOKEN_IN, TOKEN_WHILE, TOKEN_FOR, TOKEN_DO, TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_FUNCTION,
    TOKEN_RETURN, TOKEN_CLASS, TOKEN_IMPORT, TOKEN_AS, TOKEN_IS, TOKEN_WITH, TOKEN_TRY, TOKEN_CATCH };
tokentype operators[] = {
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_CIRCUMFLEX,
    TOKEN_PLUSPLUS, TOKEN_MINUSMINUS,
    TOKEN_PLUSEQ, TOKEN_MINUSEQ, TOKEN_STAREQ, TOKEN_SLASHEQ,
    TOKEN_HASH, TOKEN_AT,
    TOKEN_EXCLAMATION, TOKEN_AMP, TOKEN_VBAR, TOKEN_DBLAMP, TOKEN_DBLVBAR,
    TOKEN_EQUAL, TOKEN_EQ, TOKEN_NEQ,
    TOKEN_LT, TOKEN_GT, TOKEN_LTEQ, TOKEN_GTEQ,
    TOKEN_DOTDOT, TOKEN_DOTDOTDOT
};

/** Checks if match matches any tokentype in a given list */
static bool matchtokentype(tokentype match, size_t n, tokentype *list) {
    for (size_t i=0; i<n; i++) if (list[i]==match) return true;
    return false;
}

/** Detect and parse comments manually (lexer skips them).
 * Returns the byte position after the comment, or offset if no comment found. */
static size_t detect_comment(const char *in, size_t offset) {
    const char *start = in + offset;
    
    // Skip whitespace first
    const char *p = start;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    
    // Check for // style comment
    if (p[0] == '/' && p[1] == '/') {
        p += 2; 
        while (*p != '\0' && *p != '\n' && *p != '\r') p++; // Scan to end of line
        return p - in;
    }
    
    // Check for /* style comment
    if (p[0] == '/' && p[1] == '*') {
        p += 2;
        int nesting = 1; // Track nesting depth
        
        while (nesting > 0 && *p != '\0') {
            if (p[0] == '/' && p[1] == '*') {
                nesting++;
                p += 2;
            } else if (p[0] == '*' && p[1] == '/') {
                nesting--;
                p += 2;
            } else p++;
        }
        return p - in;
    }
    
    return offset; // No comment found
}

/** A tokenizer for syntax coloring that uses the morpho lexer */
bool cli_syntaxcolorfn(const char *in, void *ref, size_t offset, inline_colorspan_t *out) {
    bool success=false;
    lexer *l=(lexer *) ref;
    if (!l) return false;
    
    // Check for comments first (before lexer processing)
    size_t comment_end = detect_comment(in, offset);
    if (comment_end > offset) {
        // Found a comment
        out->color = 5; // Comment color (gray)
        out->byte_end = comment_end;
        return true; // Successfully colored a comment
    }
    
    lex_init(l, in+offset, 0);
    
    token tok;
    error err;
    error_init(&err);
    
    if (lex(l, &tok, &err)) {
        out->color=0;
        if (tok.start>in+offset) { // Token began after offset
            out->byte_end=tok.start-in;
        } else { // A real token
            out->byte_end=offset+tok.length;
            if (matchtokentype(tok.type, sizeof(help)/sizeof(help[0]), help)) out->color=1;
            else if (matchtokentype(tok.type, sizeof(literal)/sizeof(literal[0]), literal)) out->color=2;
            else if (matchtokentype(tok.type, sizeof(symbols)/sizeof(symbols[0]), symbols)) out->color=3;
            else if (matchtokentype(tok.type, sizeof(keywords)/sizeof(keywords[0]), keywords)) out->color=4;
            else if (matchtokentype(tok.type, sizeof(operators)/sizeof(operators[0]), operators)) out->color=6;
        }
        success=(tok.type!=TOKEN_EOF);
    }
    
    lex_clear(l);
    
    return success;
}

static char *words[] = {"as", "and", "break", "class", "continue", "do", "else", "for", "false", "fn", "help", "if", "in", "import", "nil", "or", "print", "return", "true", "var", "while", "quit", "self", "super", "this", "try", "catch", NULL};

/** Autocomplete function */
const char *cli_complete(const char *in, void *ref, size_t *index) {
    size_t len=strlen(in);
    
    /* First find the last token in the input */
    const char *tok = in+len;
    /* Scan backwards from end of string over alphanumeric tokens */
    while (tok>in && !isspace(*(tok-1))) tok--;
    
    /* Ensure we have at least one character */
    if (iscntrl(*tok)) return false;
    
    /* Now try to match the token against a library of words */
    len=strlen(tok);
    
    int success=false;
    
    for (size_t i=*index; words[i]!=NULL; i++) {
        if ( (len<strlen(words[i])) &&
             (strncmp(tok, words[i], len)==0)) {
            *index=i+1;
            return words[i]+len;
        }
    }

    return NULL;
}

/** Multiline function */
bool cli_multiline(const char *in, void *ref) {
    int nb=0;

    for (const char *c=in; *c!='\0'; c++) {
        switch (*c) {
            case '(': case '{': case '[': nb+=1; break;
            case ')': case '}': case ']': nb-=1; break;
            default: break;
        }
    }

    return (nb>0);
}

#ifdef CLI_USELIBUNISTRING
size_t libunistring_graphemefn(const char *in, const char *end) {
    char *next = (char *) u8_grapheme_next((uint8_t *) in, (uint8_t *) end);
    if (next>in) return next-in;
    return 0;
}
#endif

#ifdef CLI_USELIBGRAPHEME
size_t libgrapheme_graphemefn(const char *in, const char *end) {
    return grapheme_next_character_break_utf8(in, end-in);
}
#endif

/* **********************************************************************
 * Interactive mode
 * ********************************************************************** */

/** Runtime context holding VM, compiler, program, editor, and lexer */
typedef struct {
    vm *v;
    compiler *c;
    program *p;
    inline_editor *edit;
    lexer l;
} runtime_t;

/** Forward declaration */
static void cli_repl(runtime_t *rt, clioptions opt);

/** @brief Provide a command line interface */
void cli(clioptions opt) {
    cli_repl(NULL, opt); /* NULL means create new runtime */
}

/* **********************************************************************
 * Helper structures and functions
 * ********************************************************************** */

/** Set up runtime context (VM, compiler, program, editor, callbacks) */
static runtime_t cli_newruntime(clioptions opt) {
    runtime_t rt = { NULL, NULL, NULL, NULL };
    rt.p = morpho_newprogram();
    rt.c = morpho_newcompiler(rt.p);
    rt.v = morpho_newvm();
    rt.edit = inline_new(CLI_PROMPT);
    
    is_supported = inline_checksupported(); // Ensure we are using a supported terminal
    if (!(opt & CLI_NOCOLOR)) inline_syntaxcolor(rt.edit, cli_syntaxcolorfn, &rt.l);
    
    morpho_setinputfn(rt.v, cli_inputcallbackfn, NULL);
    morpho_setprintfn(rt.v, cli_printcallbackfn, &rt.edit);
    morpho_setwarningfn(rt.v, cli_warningcallbackfn, &rt.edit);
    morpho_setdebuggerfn(rt.v, cli_debuggercallbackfn, NULL);
    
    return rt;
}

/** Clean up runtime context */
static void cli_freeruntime(runtime_t *rt) {
    if (rt->edit) inline_free(rt->edit);
    if (rt->v) morpho_freevm(rt->v);
    if (rt->p) morpho_freeprogram(rt->p);
    if (rt->c) morpho_freecompiler(rt->c);
}

/** Compile and execute source code */
static bool cli_compileandrun(runtime_t *rt, const char *src, clioptions opt) {
    error err;
    error_init(&err);
    
    bool success = morpho_compile((char *)src, rt->c, (opt & CLI_OPTIMIZE), &err);
    
    if (success) {
        if (opt & CLI_DISASSEMBLE) {
            if (opt & CLI_DISASSEMBLESHOWSRC) {
                cli_disassemblewithsrc(rt->p, (char *)src, opt);
            } else {
                morpho_disassemble(rt->v, rt->p, NULL);
            }
        }
        if (opt & CLI_RUN) {
            if (opt & CLI_DEBUG) {
                success = morpho_debug(rt->v, rt->p);
            } else if (opt & CLI_PROFILE) {
                success = morpho_profile(rt->v, rt->p);
            } else {
                success = morpho_run(rt->v, rt->p);
            }
            if (!success) cli_reporterror(morpho_geterror(rt->v), rt->v);
        }
    } else {
        cli_reporterror(&err, rt->v);
    }
    
    return success;
}

/* **********************************************************************
 * Non-interactive run
 * ********************************************************************** */

/** Compile and run source string (no file). Used by -e / --eval. */
void cli_runstring(const char *src, clioptions opt) {
    runtime_t rt = cli_newruntime(opt);
    cli_globalsrc = (char *)src;
    
    cli_compileandrun(&rt, src, opt);
    
    /* If interactive mode, enter REPL with same VM (cleans up on exit) */
    if (opt & CLI_INTERACTIVE) {
        cli_repl(&rt, opt);
    }
    cli_freeruntime(&rt);
}

/** Enter REPL mode, optionally reusing existing runtime */
static void cli_repl(runtime_t *rt, clioptions opt) {
    bool own_runtime = (rt == NULL);
    runtime_t runtime_storage;
    if (own_runtime) {
        runtime_storage = cli_newruntime(opt);
        rt = &runtime_storage;
    }
    
    bool tty = inline_checktty();
    version morphoversion;
    morpho_version(&morphoversion);
    char morphoversionstring[VERSION_MAXSTRINGLENGTH];
    version_tostring(&morphoversion, VERSION_MAXSTRINGLENGTH, morphoversionstring);
    
    if (tty) {
        inline_setutf8();
        printf("\U0001F98B morpho %s | \U0001F44B Type 'help' or '?' for help\n", morphoversionstring);
    }
    
    bool help = help_initialize();
    
    varray_char src;
    varray_charinit(&src);
    varray_charwrite(&src, '\0');
    
    /* Configure editor for REPL (if not already configured) */
    inline_setpalette(rt->edit, sizeof(palette)/sizeof(palette[0]), palette);
    if (!(opt & CLI_NOCOLOR)) {
        inline_syntaxcolor(rt->edit, cli_syntaxcolorfn, &rt->l);
    }
    inline_multiline(rt->edit, cli_multiline, NULL, CLI_CONTINUATIONPROMPT);
    inline_autocomplete(rt->edit, cli_complete, NULL);
#ifdef CLI_USELIBUNISTRING
    inline_setgraphemesplitter(&rt->edit, libunistring_graphemefn);
#endif
#ifdef CLI_USELIBGRAPHEME
    inline_setgraphemesplitter(rt->edit, libgrapheme_graphemefn);
#endif
    
    error err;
    error_init(&err);
    
    for (int n = 0; ; n++) {
        if (!tty && n > 0) break;
        char *input = NULL;
        while (!input) input = inline_readline(rt->edit);
        
        if (strncmp(input, CLI_QUIT, strlen(CLI_QUIT)) == 0) {
            free(input);
            break;
        } else if (strncmp(input, CLI_HELP, strlen(CLI_HELP)) == 0) {
            cli_help(rt->edit, input + strlen(CLI_HELP), &err, help);
            free(input);
            continue;
        } else if (strncmp(input, CLI_SHORT_HELP, strlen(CLI_SHORT_HELP)) == 0) {
            cli_help(rt->edit, input + strlen(CLI_SHORT_HELP), &err, help);
            free(input);
            continue;
        }
        
        bool success = morpho_compile(input, rt->c, false, &err);
        
        if (success) {
            src.count--;
            varray_charadd(&src, input, (int)strlen(input));
            varray_charwrite(&src, '\n');
            varray_charwrite(&src, '\0');
            cli_globalsrc = src.data;
            
            if (opt & CLI_DISASSEMBLE) {
                morpho_disassemble(rt->v, rt->p, NULL);
            }
            if (opt & CLI_RUN) {
                success = morpho_debug(rt->v, rt->p);
                if (!success) {
                    cli_reporterror(morpho_geterror(rt->v), rt->v);
                    err = *morpho_geterror(rt->v);
                }
            }
        } else {
            cli_reporterror(&err, rt->v);
        }
        
        free(input);
    }
    
    varray_charclear(&src);
    help_finalize();
    
    if (own_runtime) {
        cli_freeruntime(rt);
    }
}

/* **********************************************************************
 * Load and run a file
 * ********************************************************************** */

void cli_run(const char *in, clioptions opt) {
    runtime_t rt = cli_newruntime(opt);
    
    char *src = cli_loadsource(in);
    if (src) cli_globalsrc = src;
    
    file_setworkingdirectory(in);
    
    if (src) {
        cli_compileandrun(&rt, src, opt);
        MORPHO_FREE(src);
    } else {
        printf("Could not open file '%s'.\n", in);
        cli_freeruntime(&rt);
        return;
    }
    
    /* If interactive mode, enter REPL with same VM (cleans up on exit) */
    if (opt & CLI_INTERACTIVE) {
        cli_repl(&rt, opt);
    }
    cli_freeruntime(&rt);
}

/* **********************************************************************
 * Load source code
 * ********************************************************************** */

/** Loads source from stdin, returning it as a C-string. Call MORPHO_FREE on it when finished. */
char *cli_loadstdin(void) {
    varray_char buffer;
    varray_charinit(&buffer);
    
    /* Read from stdin in chunks */
    char chunk[CLI_BUFFERSIZE];
    size_t nread;
    
    while ((nread = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
        varray_charadd(&buffer, chunk, (int)nread);
    }
    
    /* Ensure null termination */
    varray_charwrite(&buffer, '\0');
    
    return buffer.data;
}

/** Loads a source file, returning it as a C-string. Call MORPHO_FREE on it when finished. */
char *cli_loadsource(const char *in) {
    FILE *f = NULL; /* Input file */
    int size=0;
    
    varray_char buffer;
    varray_charinit(&buffer);
    
    /* Open the input file if provided */
    if (in) f=file_openrelative(in,"r"); // Try opening relative to the working directory
    if (!f) f=fopen(in, "r");
    if (!f) return NULL;
    
    /* Determine the file size */
    fseek(f, 0L, SEEK_END);
    size = ((int) ftell(f))+1;
    rewind(f);
    
    if (size) {
        /* Size the buffer to match */
        if (!varray_charresize(&buffer, size+1)) {
            return NULL;
        }
        
        /* Read in the file */
        for (char *c=buffer.data; !feof(f); c=c+strlen(c)) {
            if (!fgets(c, (int) (buffer.data+buffer.capacity-c), f)) { c[0]='\0'; break; }
        }
        
        fclose(f);
    }
    
    return buffer.data;
}

/* **********************************************************************
 * Source listing and disassembly
 * ********************************************************************** */

/** Displays a single line of source */
static void cli_printline(inline_editor *edit, int line, char *prompt, const char *src, int length) {
    printf("%s %4u : ", prompt, line);
    /* Display the src line */
    char srcline[length];
    strncpy(srcline, src, length-1);
    srcline[length-1]='\0';
    inline_displaywithsyntaxcoloring(edit, srcline);
    printf("\n");
}

/** Disassembles the program showing syntax colored lines of source */
void cli_disassemblewithsrc(program *p, char *src, clioptions opt) {
    inline_editor *edit = inline_new("");
    if (!edit) return;
    lexer l;
    if (!(opt & CLI_NOCOLOR)) {
        inline_syntaxcolor(edit, cli_syntaxcolorfn, &l);
    }
    
    int line=1, length=0;
    for (unsigned int i=0; src[i]!='\0'; i++) {
        length++;
        if (src[i]=='\n' || src[i]=='\0') {
            cli_printline(edit, line, ">>>", src+i-length+1, length);
            morpho_disassemble(NULL, p, NULL);
            line++; length=0;
        }
    }
    
    inline_free(edit);
}

/** Displays a source listing from source lines start to end */
void cli_list(const char *src, int start, int end, clioptions opt) {
    if (src) {
        inline_editor *edit = inline_new("");
        if (!edit) return;
        lexer l;
        if (!(opt & CLI_NOCOLOR)) {
            inline_syntaxcolor(edit, cli_syntaxcolorfn, &l);
            inline_setpalette(edit, sizeof(palette)/sizeof(palette[0]), palette);
        }
        
        int line=1, length=0;
        for (unsigned int i=0; src[i]!='\0'; i++) {
            length++;
            if (src[i]=='\n' || src[i]=='\0') {
                if (line>=start && line <=end) cli_printline(edit, line, "", src+i-length+1, length);
                line++;
                length=0;
            }
        }
        inline_free(edit);
    }
}

