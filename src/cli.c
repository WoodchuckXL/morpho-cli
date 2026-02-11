/** @file cli.c
 *  @author T J Atherton
 *
 *  @brief Command line interface
*/

#include <time.h>
#include <stdio.h>
#include <ctype.h>

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

#define CLI_BUFFERSIZE 1024

#define BLU   "\x1B[34m"
#define CYN   "\x1B[36m"
#define GRY   "\x1B[38;2;128;128;128m"
#define RESET "\x1B[0m"

#define BOLD       "\x1B[1m"
#define ITALIC     "\x1B[3m"
#define UNDERLINE  "\x1B[4m"

void inline_setutf8(void);
void inline_emitcolor(int color);
void inline_emit(const char *seq);

void cli_emitemphasis(int emph) {
    switch (emph) {
        case CLI_NOEMPHASIS: inline_emit(RESET); break;
        case CLI_BOLD: inline_emit(BOLD); break;
        case CLI_UNDERLINE: inline_emit(UNDERLINE); break;
        case CLI_ITALIC: inline_emit(ITALIC); break;
        default: break;
    }
}

/** Displays several strings with a specified style using linedit */
void cli_displaywithstyle(int col, int emph, int n, ...) {
    va_list args;
    va_start(args, n);
    for (int i=0; i<n; i++) {
        char *str = va_arg(args, char *);
        cli_emitemphasis(emph);
        inline_emitcolor(col);
        printf("%s",str);
    }
    inline_emit(RESET);
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

/* **********************************************************************
 * CLI callbacks
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
 * Interactive cli
 * ********************************************************************** */

/** Define colors for different token types */

int palette[] = {
    CLI_DEFAULTCOLOR, // 0 default
    INLINE_YELLOW,    // 1 help
    INLINE_BLUE,      // 2 string/integer/number literals
    INLINE_CYAN,      // 3 symbol
    INLINE_MAGENTA    // 4 keyword
};

tokentype help[] = { TOKEN_QUESTION };
tokentype literal[] = { TOKEN_STRING, TOKEN_INTERPOLATION, TOKEN_INTEGER, TOKEN_NUMBER, TOKEN_IMAG };
tokentype symbols[] = { TOKEN_SYMBOL };
tokentype keywords[] = { TOKEN_TRUE, TOKEN_FALSE, TOKEN_NIL, TOKEN_SELF, TOKEN_SUPER, TOKEN_PRINT, TOKEN_VAR, TOKEN_IF, TOKEN_ELSE, TOKEN_IN, TOKEN_WHILE, TOKEN_FOR, TOKEN_DO, TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_FUNCTION,
    TOKEN_RETURN, TOKEN_CLASS, TOKEN_IMPORT, TOKEN_AS, TOKEN_IS, TOKEN_VAR, TOKEN_WITH, TOKEN_TRY, TOKEN_CATCH };

/** Checks if match matches any tokentype in a given list */
static bool matchtokentype(tokentype match, size_t n, tokentype *list) {
    for (size_t i=0; i<n; i++) if (list[i]==match) return true;
    return false;
}

/** A tokenizer for syntax coloring that uses the morpho lexer */
bool cli_syntaxcolorfn(const char *in, void *ref, size_t offset, inline_colorspan_t *out) {
    bool success=false;
    lexer *l=(lexer *) ref;
    if (!l) return false;
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

/** @brief Provide a command line interface */
void cli(clioptions opt) {
    bool tty=inline_checktty();
    version morphoversion;
    morpho_version(&morphoversion);
    char morphoversionstring[VERSION_MAXSTRINGLENGTH];
    version_tostring(&morphoversion, VERSION_MAXSTRINGLENGTH, morphoversionstring);
    
    if (tty) {
        inline_setutf8();
        printf("\U0001F98B morpho %s | \U0001F44B Type 'help' or '?' for help\n", morphoversionstring);
    }

    /* Set up program and compiler */
    program *p = morpho_newprogram();
    compiler *c = morpho_newcompiler(p);
    
    bool help = help_initialize();
    
    /* Keep the line by line src as a varray */
    varray_char src;
    varray_charinit(&src);
    varray_charwrite(&src, '\0'); // Begin with zero string
    
    /* Set up VM */
    vm *v = morpho_newvm();
    
    /* Line editor */
    inline_editor *edit = inline_new(CLI_PROMPT);
    lexer l;
    inline_setpalette(edit, sizeof(palette)/sizeof(palette[0]), palette);
    inline_syntaxcolor(edit, cli_syntaxcolorfn, &l);
    inline_multiline(edit, cli_multiline, NULL, CLI_CONTINUATIONPROMPT);
    inline_autocomplete(edit, cli_complete, NULL);
#ifdef CLI_USELIBUNISTRING
    inline_setgraphemesplitter(&edit, libunistring_graphemefn);
#endif
#ifdef CLI_USELIBGRAPHEME
    inline_setgraphemesplitter(edit, libgrapheme_graphemefn);
#endif

    morpho_setinputfn(v, cli_inputcallbackfn, NULL);
    morpho_setprintfn(v, cli_printcallbackfn, &edit);
    morpho_setwarningfn(v, cli_warningcallbackfn, &edit);
    morpho_setdebuggerfn(v, cli_debuggercallbackfn, NULL);
    
    error err; /* Error structure that received messages from the compiler and VM */
    bool success=false; /* Keep track of whether compilation and execution was successful */
    
    /* Initialize the error struct */
    error_init(&err);

    /* Read-evaluate-print loop */
    for (int n=0;;n++) {
        if (!tty && n>0) break;
        char *input=NULL;
        
        while (!input) input=inline_readline(edit);
        
        /* Check for CLI commands. */
        /* Let the user quit by typing 'quit'. */
        if (strncmp(input, CLI_QUIT, strlen(CLI_QUIT))==0) {
			break;
        } else if (strncmp(input, CLI_HELP, strlen(CLI_HELP))==0) {
            cli_help(edit, input+strlen(CLI_HELP), &err, help); continue;
        } else if (strncmp(input, CLI_SHORT_HELP, strlen(CLI_SHORT_HELP))==0) {
            cli_help(edit, input+strlen(CLI_SHORT_HELP), &err, help); continue;
        }
        
        /* Compile code */
        success=morpho_compile(input, c, false, &err);
        
        if (success) { /** If compilation was successful, and we're in interactive mode, execute... */
            /** Retain input in interactive session */
            src.count--; // Remove zero terminator
            varray_charadd(&src, input, (int) strlen(input));
            varray_charwrite(&src, '\n');
            varray_charwrite(&src, '\0'); // Ensure zero terminated
            cli_globalsrc=src.data;
            
            if (opt & CLI_DISASSEMBLE) {
                morpho_disassemble(v, p, NULL);
            }
            if (opt & CLI_RUN) {
                success=morpho_debug(v, p);
                if (!success) {
                    cli_reporterror(morpho_geterror(v), v);
                    err=*morpho_geterror(v);
                }
            }
        } else {
            /** ... otherwise just raise an error. */
            cli_reporterror(&err, v);
        } 
    }
    
    inline_free(edit);
    morpho_freevm(v);
    
    varray_charclear(&src);
    
    help_finalize();
    
    morpho_freecompiler(c);
    morpho_freeprogram(p);
}

/* **********************************************************************
 * Run a file
 * ********************************************************************** */

/** Loads and runs a file. */
void cli_run(const char *in, clioptions opt) {
    program *p = morpho_newprogram();
    compiler *c = morpho_newcompiler(p);
    vm *v = morpho_newvm();
    
    /* Set up line editor for output */
    inline_editor *edit=inline_new(CLI_PROMPT);
    lexer l;
    inline_syntaxcolor(edit, cli_syntaxcolorfn, &l);

    morpho_setinputfn(v, cli_inputcallbackfn, &edit);
    morpho_setprintfn(v, cli_printcallbackfn, &edit);
    morpho_setwarningfn(v, cli_warningcallbackfn, &edit);
    morpho_setdebuggerfn(v, cli_debuggercallbackfn, NULL);
    
    char *src = cli_loadsource(in);
    if (src) cli_globalsrc = src;
    
    error err; /* Error structure that received messages from the compiler and VM */
    error_init(&err);

    bool success=false; /* Keep track of whether compilation and execution was successful */
    
    /* Open the input file if provided */
    file_setworkingdirectory(in);
    
    if (src) {
        /* Compile code */
        success=morpho_compile(src, c, (opt & CLI_OPTIMIZE), &err);
        
        /* Run code if successful */
        if (success) {
            if (opt & CLI_DISASSEMBLE) {
                if (opt & CLI_DISASSEMBLESHOWSRC) {
                    cli_disassemblewithsrc(p, src);
                } else {
                    morpho_disassemble(v, p, NULL);
                }
            }
            if (opt & CLI_RUN) {
                if (opt & CLI_DEBUG) {
                    morpho_setdebuggerfn(v, cli_debuggercallbackfn, NULL);
                    success=morpho_debug(v, p);
                } else if (opt & CLI_PROFILE) {
                    success=morpho_profile(v, p);
                } else {
                    success=morpho_run(v, p);
                }
                if (!success) cli_reporterror(morpho_geterror(v), v);
            }
        } else {
            cli_reporterror(&err, v);
        }
    } else {
        printf("Could not open file '%s'.\n", in);
    }
    
    inline_free(edit);
    
    MORPHO_FREE(src);
    morpho_freevm(v);
    morpho_freeprogram(p);
    morpho_freecompiler(c);
}

/* **********************************************************************
 * Load source code
 * ********************************************************************** */

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
void cli_disassemblewithsrc(program *p, char *src) {
    inline_editor *edit = inline_new("");
    if (!edit) return;
    lexer l;
    inline_syntaxcolor(edit, cli_syntaxcolorfn, &l);
    
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
void cli_list(const char *src, int start, int end) {
    if (src) {
        inline_editor *edit = inline_new("");
        if (!edit) return;
        lexer l;
        inline_syntaxcolor(edit, cli_syntaxcolorfn, &l);
        
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

