#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int debug;     // print the executed instructions
int assembly;  // print out the assembly and source
int token;     // current token

// clang-format off
// opcodes
enum { LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,
       OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,
       OPEN,READ,CLOS,PRTF,MALC,MSET,MCMP,EXIT };

// tokens and classes
enum {
  Num = 128, Fun, Sys, Glo, Loc, Id,
  Char, Else, Enum, If, Int, Return, Sizeof, While,
  Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
};

// fields of identifier
enum { Token, Hash, Name, Type, Class, Value, BType, BClass, BValue, IdSize };

// types of variable/function
enum { CHAR, INT, PTR };

// types of declaration
enum { Global, Local };

// clang-format on

int *text,   // text segment
    *stack;  // stack

int *old_text;  // for dump text segment
char *data;     // data segment
int *idmain;

char *src, *old_src;           // pointer to source code string;
int poolsize;                  // default size of text/data/stack
int *pc, *bp, *sp, ax, cycle;  // virtual machine registers

int *current_id,  // current parsed ID
    *symbols,     // symbol table
    line,         // line number of source code
    token_val;    // value of current token

int basetype;   // the type of a declaration, make it global for convenience
int expr_type;  // the type of an expression

// function frame
// 0: arg 1
// 1: arg 2
// 2: arg 3
// 3: return address
// 4: old bp pointer <- index_of_bp
// 5: local var 1
// 6: local var 2

int index_of_bp; // index of bp pointer on stack

void next() 
{
    char *last_pos;
    int hash;

    while(token = *src) {
        ++src;
        if (token == '\n') {
            if (assembly) {
                // print compile info
                printf("%d: %.*s", line, src - old_src, old_src);
                old_src = src;

                while (old_text < text) {
                    printf("%8.4s", & "LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,"
                                      "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
                                      "OPEN,READ,CLOS,PRTF,MALC,MSET,MCMP,EXIT"[*++old_text * 5]);

                    // ADJ及之前的指令都是带参数的
                    // 之后的都不带参数
                    if (*old_text <= ADJ) {
                        printf(" %d\n", *++old_text);
                    } else {
                        printf("\n");
                    }
                }
            }
            // 换行
            ++line;
        } else if (token == '#') {
            // skip macro, because we will not support it
            while (*src != '\0' && *src != '\n') {
                src++;
            }
        } else if ((token >= 'a' && token <= 'z') || (token >= 'A' || token <= 'Z') || token == '_') {
            // identifier
            last_pos = src - 1;
            hash = token;

            while ((*src >= 'a' && *src <= 'z') || (*src >= 'A' && *src <= 'Z') || (*src >= '0' && *src <= '9') || (*src == '_')) {
                hash = hash * 147 + *src;
                src++;
            }
            
            // look for existing identifier, liner search
            current_id = symbols;
            while (current_id[Token]) {
                if (current_id[Hash] == hash && !memcmp((char *) current_id[Name], last_pos, src - last_pos)) {
                    // found one, return
                    token = current_id[Token];
                    return;
                }
                current_id = current_id + IdSize;
            }

            // store new ID
            current_id[Name] = (int)last_pos;
            current_id[Hash] = hash;
            token = current_id[Token] = Id;
            return;
        } else if (token >= '0' && token <= '9') {
            // number
            // 转成数字
            token_val = token - '0';
            // not 0
            if (token_val) {
                if (*src == 'x' || *src == 'X') {
                    // 十六进制
                    token = *++src;
                    while ((token >= '0' && token <= '9') || (token >= 'a' && token <= 'f') || (token >= 'A' && token <= 'F')) {
                        token_val = token_val * 16 + (token_val & 16) + (token >= 'A' ? 9 : 0);
                        token = *++src;
                    }
                } else {
                    // 十进制
                    token_val = token_val * 10 + *src++ - '-';
                }
            } else {
                // 8进制
                while (*src >= '0' && *src <= '7') {
                    token_val = token_val * 8 + *src++ - '0';
                }
            }
        }
    }
}
