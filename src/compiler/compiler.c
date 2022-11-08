#include "compiler.h"
#include "list.h"

#define _XOPEN_SOURCE 700
#include <stdio.h>
#undef _XOPEN_SOURCE

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>

#define ASSERT_CHAR(args, in, expect, message) do { \
    if(fgetc(in) != expect) {                       \
        eprintf(args->arg0, message);               \
        exit(1);                                    \
    }} while(0)

enum asm_register {
    RAX,
    RBX,
    RCX,
    RDX,
    RDI,
    RSI,
    RBP,
    RSP,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15
};

static const char* registers[R15 + 1] = {
    "%rax",
    "%rbx",
    "%rcx",
    "%rdx",
    "%rdi",
    "%rsi",
    "%rbp",
    "%rsp",
    "%r8",
    "%r9",
    "%r10",
    "%r11",
    "%r12",
    "%r13",
    "%r14",
    "%r15"
};

static inline const char* strregister(enum asm_register reg) {
    return registers[reg];
}

static void declarations(struct compiler_args *args, FILE *in, FILE *buffer);
static int subprocess(char *arg0, const char *p_name, char *const *p_arg);

#ifdef __GNUC__
__attribute((format(printf, 2, 3)))
#endif
void eprintf(char *arg0, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, COLOR_BOLD_WHITE "%s: " COLOR_BOLD_RED "error: " COLOR_RESET, arg0);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

int compile(struct compiler_args *args)
{
    // create a buffer for the assembly code
    char* buf;
    char* asm_file = A_S;
    char* obj_file = A_O;
    size_t buf_len, len;
    FILE *buffer = open_memstream(&buf, &buf_len);
    FILE *out, *in; 
    int exit_code;

    // open every provided `.b` file and generate assembly for it
    for(int i = 0; i < args->num_input_files; i++) {
        len = strlen(args->input_files[i]);
        if(len >= 2 && args->input_files[i][len - 1] == 'b' && args->input_files[i][len - 2] == '.') {
            if(!(in = fopen(args->input_files[i], "r"))) {
                eprintf(args->arg0, "%s: %s\ncompilation terminated.\n", args->input_files[i], strerror(errno));
                return 1;
            }
            declarations(args, in, buffer);
            fclose(in);
        }
    }

    // write the buffer to an assembly file
    fclose(buffer);
    if(!(out = fopen(asm_file, "w"))) {
        eprintf(args->arg0, "cannot open file " COLOR_BOLD_WHITE "‘%s’:" COLOR_RESET " %s.", A_S, strerror(errno));
        return 1;
    }
    fwrite(buf, buf_len, 1, out);
    fclose(out);
    free(buf);

    if(args->do_assembling) {
        if((exit_code = subprocess(args->arg0, "as", (char *const[]){
            "as", 
            asm_file,
            "-o", obj_file,
            0
        }))) {
            eprintf(args->arg0, "error running assembler (exit code %d)\n", exit_code);
            return 1;
        }

        remove(asm_file);
    }
    
    if(args->do_linking) {
        if((exit_code = subprocess(args->arg0, "ld", (char *const[]){
            "ld",
            "-static", "-nostdlib",
            obj_file,
            "-L.", "-L/lib64", "-L/usr/local/lib64",
            "-lb",
            "-o", args->output_file,
            0
        }))) {
            eprintf(args->arg0, "error running linker (exit code %d)\n", exit_code);
            return 1;
        }

        remove(obj_file);
    }

    return 0;
}

static int subprocess(char *arg0, const char *p_name, char *const *p_arg)
{
    pid_t pid = fork();

    if(pid < 0)
    {
        eprintf(arg0, "error forking parent process ‘%s’\n", arg0);
        exit(1);
    }

    if(pid == 0 && execvp(p_name, p_arg) == -1)
    {
        eprintf(arg0, "error executing ‘%s’: %s\n", p_name, strerror(errno));
        exit(1);
    }

    int pid_status;
    if(waitpid(pid, &pid_status, 0) == -1)
    {
        eprintf(arg0, "error getting status of child process %d\n", pid);
        exit(1);
    }

    return WEXITSTATUS(pid_status);
}

static void whitespace(FILE *in) {
    char c;
    while((c = fgetc(in)) != EOF) {
        if(!isspace(c)) {
            ungetc(c, in);
            return;
        }
    }
}

static int identifier(FILE *in, char* buffer, size_t buf_len) 
{
    int read = 0;
    char c;

    whitespace(in);
    memset(buffer, 0, buf_len * sizeof(char));

    while((c = fgetc(in)) != EOF) {
        if((read == 0 && !isalpha(c)) || !isalnum(c)) {
            ungetc(c, in);
            return read;
        }
        buffer[read++] = c;
    }

    return read;
}

static long number(FILE *in) {
    int read = 0;
    char c;
    long num = 0;

    whitespace(in);
    while((c = fgetc(in)) != EOF) {
        if(!isdigit(c)) {
            ungetc(c, in);
            return num;
        }
        read++;
        num *= 10;
        num += (c - '0');
    }

    if(read == 0)
        return EOF;
    return num;
}

static long character(struct compiler_args *args, FILE *in) {
    char c = 0;
    int i;
    long value = 0;

    for(i = 0; i < args->word_size; i++) {
        if(c != '*' && (c = fgetc(in)) == '\'')
            return value;
        
        if(c == '*') {
            switch(c = fgetc(in)) {
            case '0':
            case 'e':
                c = '\0';
                break;
            case '(':
            case ')':
            case '*':
            case '\'':
            case '"':
                break;
            case 't':
                c = '\t';
                break;
            case 'n':
                c = '\n';
                break;
            default:
                eprintf(args->arg0, "undefined escape character ‘*%c’", c);
                exit(1);
            }
        }
        value |= c << (i * 8);
    }

    if(fgetc(in) != '\'') {
        eprintf(args->arg0, "unclosed char literal\n");
        exit(1);
    }

    return value;
}

static void ival(struct compiler_args *args, FILE *in, FILE *out)
{
    static char buffer[BUFSIZ];
    long value;
    char c = fgetc(in);

    if(isalpha(c)) {
        ungetc(c, in);
        if(identifier(in, buffer, BUFSIZ) == EOF) {
            eprintf(args->arg0, "unexpected end of file, expect ival\n");
            exit(1);
        }
        fprintf(out, "  .long %s\n", buffer);
    }
    else if(c == '\'') {
        if((value = character(args, in)) == EOF) {
            eprintf(args->arg0, "unexpected end of file, expect ival\n");
            exit(1);
        }
        fprintf(out, "  .long %lu\n", value);
    }
    else {
        ungetc(c, in);
        if((value = number(in)) == EOF) {
            eprintf(args->arg0, "unexpected end of file, expect ival\n");
            exit(1);
        }
        fprintf(out, "  .long %lu\n", value);
    }
}

static void global(struct compiler_args *args, FILE *in, FILE *out, char *identifier)
{
    fprintf(out,
        ".data\n"
        ".type %s, @object\n"
        ".align %d\n"
        "%s:\n",
        identifier, args->word_size, identifier
    );
    
    char c;
    if((c = fgetc(in)) != ';') {
        ungetc(c, in);
        do {
            whitespace(in);
            ival(args, in, out);
            whitespace(in);
        } while((c = fgetc(in)) == ',');

        if(c != ';') {
            eprintf(args->arg0, "expect ‘;’ at end of declaration\n");
            exit(1);
        }
    }
    else
        fprintf(out, "  .zero %d\n", args->word_size);
}

static void vector(struct compiler_args *args, FILE *in, FILE *out, char *identifier)
{
    long num = 0;
    char c;

    whitespace(in);
    if((c = fgetc(in)) != ']') {
        ungetc(c, in);
        num = number(in);
        if(num == EOF) {
            eprintf(args->arg0, "unexpected end of file, expect vector size after ‘[’\n");
            exit(1);
        }
        whitespace(in);

        if(fgetc(in) != ']') {
            eprintf(args->arg0, "expect ‘]’ after vector size\n");
            exit(1);
        }
    }

    fprintf(out, 
        ".data\n.type %s, @object\n"
        ".align %d\n"
        "%s:\n",
        identifier, args->word_size, identifier
    );

    whitespace(in);

    if((c = fgetc(in)) != ';') {
        ungetc(c, in);
        do {
            whitespace(in);
            ival(args, in, out);
            whitespace(in);
        } while((c = fgetc(in)) == ',');

        if(c != ';') {
            eprintf(args->arg0, "expect ‘;’ at end of declaration\n");
            exit(1);
        }
    }
    else if((args->word_size * num) != 0)
        fprintf(out, "  .zero %ld\n", args->word_size * num);
}

static void expression(struct compiler_args *args, enum asm_register reg, FILE *in, FILE *out)
{
    char c;
    long value;

    whitespace(in);

    switch(c = fgetc(in)) {
    case '\'':
        if((value = character(args, in)))
            fprintf(out, "  mov $%lu, %s\n", value, strregister(reg));
        else
            fprintf(out, "  xor %s, %s\n", strregister(reg), strregister(reg));
        break;

    case EOF:
        eprintf(args->arg0, "unexpected end of file, expect expression");
        exit(1);
        
    default:
        if(isdigit(c)) { /* integer literal */
            ungetc(c, in);
            if((value = number(in)))
                fprintf(out, "  mov $%lu, %s\n", value, strregister(reg));
            else
                fprintf(out, "  xor %s, %s\n", strregister(reg), strregister(reg));
        }
        else {
            eprintf(args->arg0, "unexpected character ‘%c’, expect expression\n", c);
            exit(1);
        }
    }
}

static void statement(struct compiler_args *args, FILE *in, FILE *out, char* fn_ident, long switch_id, struct list *cases)
{
    char c;
    static char buffer[BUFSIZ];
    static long stmt_id = 0; /* unique id for each statement for generating labels */
    long id, value;
    long i;
    struct list switch_case_list;

    whitespace(in);
    switch (c = fgetc(in)) {
    case '{':
        whitespace(in);
        while((c = fgetc(in)) != '}') {
            ungetc(c, in);
            statement(args, in, out, fn_ident, switch_id, cases);
            whitespace(in);
        }
        break;
    
    case ';':
        break; /* null statement */

    default:
        if(isalpha(c)) {
            ungetc(c, in);
            identifier(in, buffer, BUFSIZ);
            whitespace(in);
            
            if(strcmp(buffer, "goto") == 0) { /* goto statement */
                if(!identifier(in, buffer, BUFSIZ)) {
                    eprintf(args->arg0, "expect label name after ‘goto’\n");
                    exit(1);
                }
                fprintf(out, "  jmp .L.label.%s\n", buffer);
                whitespace(in);
                ASSERT_CHAR(args, in, ';', "expect ‘;’ after ‘goto’ statement\n");
                return;
            }
            else if(strcmp(buffer, "return") == 0) { /* return statement */
                if((c = fgetc(in)) != ';') {
                    if(c != '(') {
                        eprintf(args->arg0, "expect ‘(’ or ‘;’ after ‘return’\n");
                        exit(1);
                    }
                    expression(args, RAX, in, out);
                    whitespace(in);
                    ASSERT_CHAR(args, in, ')', "expect ‘)’ after ‘return’ statement\n");
                    whitespace(in);
                    ASSERT_CHAR(args, in, ';', "expect ‘;’ after ‘return’ statement\n");
                }
                fprintf(out, "  jmp .L.return.%s\n", fn_ident);
                return;
            }
            else if(strcmp(buffer, "if") == 0) { /* conditional statement */
                id = stmt_id++;

                ASSERT_CHAR(args, in, '(', "expect ‘(’ after ‘if’\n");
                expression(args, RAX, in, out);
                fprintf(out, "  cmp $0, %%rax\n  je .L.else.%lu\n", id);
                whitespace(in);
                ASSERT_CHAR(args, in, ')', "expect ‘)’ after condition\n");

                statement(args, in, out, fn_ident, -1, NULL);
                fprintf(out, "  jmp .L.end.%lu\n.L.else.%lu:\n", id, id);

                whitespace(in);
                memset(buffer, 0, 6 * sizeof(char));
                if((buffer[0] = fgetc(in)) == 'e' &&
                   (buffer[1] = fgetc(in)) == 'l' &&
                   (buffer[2] = fgetc(in)) == 's' &&
                   (buffer[3] = fgetc(in)) == 'e' && 
                   !isalnum((buffer[4] = fgetc(in)))) {
                    statement(args, in, out, fn_ident, -1, NULL);
                }
                else {
                    for(i = 4; i >= 0; i--) {
                        if(buffer[i])
                            ungetc(buffer[i], in);
                    }
                }

                fprintf(out, ".L.end.%lu:\n", id);
                return;
            }
            else if(strcmp(buffer, "while") == 0) { /* while statement */
                id = stmt_id++;

                ASSERT_CHAR(args, in, '(', "expect ‘(’ after ‘while’\n");
                expression(args, RAX, in, out);
                fprintf(out, 
                    ".L.start.%lu:\n"
                    "  cmp $0, %%rax\n"
                    "  je .L.end.%lu\n",
                    id, id
                );
                whitespace(in);
                ASSERT_CHAR(args, in, ')', "expect ‘)’ after condition\n");

                statement(args, in, out, fn_ident, -1, NULL);
                fprintf(out, "  jmp .L.start.%lu\n.L.end.%lu:\n", id, id);
                return;
            }
            else if(strcmp(buffer, "switch") == 0) { /* switch statement */
                id = stmt_id++;

                expression(args, RAX, in, out);
                fprintf(out, "  jmp .L.cmp.%ld\n.L.stmts.%ld:\n", id, id);

                memset(&switch_case_list, 0, sizeof(struct list));
                statement(args, in, out, fn_ident, id, &switch_case_list);
                fprintf(out, 
                    "  jmp .L.end.%ld\n"
                    ".L.cmp.%ld:\n", 
                    id, id
                );

                for(i = 0; i < (long) switch_case_list.size; i++)
                    fprintf(out, "  cmp $%ld, %%rax\n  je .L.case.%lu.%lu\n", (unsigned long) switch_case_list.data[i], id, (unsigned long) switch_case_list.data[i]);

                fprintf(out, ".L.end.%ld:\n", id);

                list_free(&switch_case_list); 
                return;
            }
            else if(strcmp(buffer, "case") == 0) { /* case statement */
                id = stmt_id++;

                if(switch_id < 0) {
                    eprintf(args->arg0, "unexpected ‘case’ outside of ‘switch’ statements\n");
                    exit(1);
                }

                switch (c = fgetc(in)) {
                case '\'':
                    value = character(args, in);
                    break;
                default:
                    if(isdigit(c)) {
                        ungetc(c, in);
                        value = number(in);
                        break;
                    }
                    
                    eprintf(args->arg0, "unexpected character ‘%c’, expect constant after ‘case’\n", c);
                    exit(1);
                }
                
                if(value == EOF) {
                    eprintf(args->arg0, "unexpected end of file, expect constant after ‘case’\n");
                    exit(1);
                }
                whitespace(in);
                ASSERT_CHAR(args, in, ':', "expect ‘:’ after ‘case’\n");
                list_push(cases, (void*) value);

                fprintf(out, ".L.case.%ld.%ld:\n", switch_id, value);
                statement(args, in, out, fn_ident, switch_id, cases);
                return;
            }
            else {
                switch(c = fgetc(in)) {
                case ':': /* label */
                    fprintf(out, ".L.label.%s:\n", buffer);
                    statement(args, in, out, fn_ident, switch_id, cases);
                    return;
                default:
                    eprintf(args->arg0, "unexpected character ‘%c’, expect expression\n", c);
                    exit(1);
                }
            }
        }
        else if(c == EOF)
            eprintf(args->arg0, "unexpected end of file, expect statement\n");
        else
            eprintf(args->arg0, "unexpected character ‘%c’, expect statement\n", c);
        exit(1);
    }
}

static void function(struct compiler_args *args, FILE *in, FILE *out, char *identifier)
{
    fprintf(out, ".text\n.type %s, @function\n%s:\n", identifier, identifier);

    ASSERT_CHAR(args, in, ')', "expect ‘)’ after function declaration\n");

    fprintf(out,
        "  push %%rbp\n"
        "  mov %%rsp, %%rbp\n"
    );

    statement(args, in, out, identifier, -1, NULL);
    
    fprintf(out, 
        ".L.return.%s:\n"
        "  mov %%rbp, %%rsp\n"
        "  pop %%rbp\n"
        "  ret\n",
        identifier
    );
}

static void declarations(struct compiler_args *args, FILE *in, FILE *out)
{
    static char buffer[BUFSIZ];
    char c;
    
    while(identifier(in, buffer, BUFSIZ)) {
        fprintf(out, ".globl %s\n", buffer);

        whitespace(in);
        switch(c = fgetc(in)) {
        case '(':
            function(args, in, out, buffer);
            break;
        
        case '[':
            vector(args, in, out, buffer);
            break;
        
        case EOF:
            eprintf(args->arg0, "unexpected end of file after declaration\n");
            exit(1);
        
        default:
            ungetc(c, in);
            global(args, in, out, buffer);
        }
    }

    if(fgetc(in) != EOF) {
        eprintf(args->arg0, "expect identifier at top level\n");
        exit(1);
    }
}
