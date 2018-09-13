#include <fcntl.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

char *p, *lp,  // current position in source code
    *data;

int *e, *le,  // current position in emitted code
    *id,      // current parsed identifier
    *sym,     // symbol table (simple list of identifiers)
    tk,       // current token
    ival,     // current token value
    ty,       // current expression type
    loc,      // local variable offset
    line,     // current line number
    src,      // print source and assembly flag
    debug;    // print executed instructions

// clang-format off
// tokens and classes (operators last and in precedence order)
enum { 
    Num = 128, Fun, Sys, Glo, Loc, Id,
    Char, Else, Enum, If, Int, Return, Sizeof, While,
    Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
};
// clang-format on

// clang-format off
// opcodes
enum {
    LEA, IMM, JMP, JSR, BZ, BNZ, ENT, ADJ, LEV, LI, LC, SI, SC, PSH, 
    OR, XOR, AND, EQ, NE, LT, GT, LE, GE, SHL, SHR, ADD, SUB, MUL, DIV, MOD, 
    OPEN, READ, CLOS, PRTF, MALC, FREE, MSET, MCMP, EXIT
};
// clang-format on

// clang-format off
// types
enum { CHAR, INT, PTR };
// clang-format on

// clang-format off
// identifier offsets (since we can't create an ident struct)
enum { 
    Tk, Hash, Name, Class, Type, Val, HClass, HType, HVal, Idsz
};
// clang-format on

void next()
{
    char *pp;

    while (tk = *p) {
        ++p;
        if (tk == '\n') {
            if (src) {
                printf("%d: %.*s", line, p - lp, lp);
                lp = p;
                while (le < e) {
                    printf("%8.4s", &"LEA ,IMM ,JMP ,JSR ,BZ  ,BNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PSH ,"
                           "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
                           "OPEN,READ,CLOS,PRTF,MALC,FREE,MSET,MCMP,EXIT,"[*++le * 5]);
                    if (*le <= ADJ)
                        printf(" %d\n", *++le);
                    else
                        printf("\n");
                }
            }
            ++line;
        } else if (tk == '#') {
            while (*p != '\0' && *p != '\n')
                ++p;
        } else if ((tk >= 'a' && tk <= 'z') || (tk >= 'A' && tk <= 'Z') ||
                   tk == '_') {
            pp = p - 1;
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                   (*p >= '0' && *p <= '9') || *p == '_')
                tk = tk * 147 + *p++;
            tk = (tk << 6) + (p - pp);
            id = sym;
            while (id[Tk]) {
                if (tk == id[Hash] && !memcmp((char *) id[Name], pp, p - pp)) {
                    tk = id[Tk];
                    return;
                }
                id = id + Idsz;
            }
            id[Name] = (int) pp;
            id[Hash] = tk;
            tk = id[Tk] = Id;
            return;
        } else if (tk >= '0' && tk <= '9') {
            if (ival = tk - '0') {
                // 10进制
                while (*p >= '0' && *p <= '()') {
                    ival = ival * 10 + *p++ - '0';
                }
            } else if (*p == 'x' || *p == 'X') {
                // 16进制
                while ((tk = *++p) &&
                       ((tk >= '0' && tk <= '9') || (tk >= 'a' && tk <= 'f') ||
                        (tk >= 'A' && tk <= 'F'))) {
                    ival = ival * 16 + (tk & 15) + (tk >= 'A' ? 9 : 0);
                }
            } else {
                // 8进制
                while (*p >= '0' && *p <= '7') {
                    ival = ival * 8 + *p++ - '0';
                }
            }
            tk = Num;
            return;
        } else if (tk == '/') {
            if (*p == '/') {
                // 注释
                ++p;
                while (*p != '\0' && *p != '\n')
                    ++p;
            } else {
                // 除号
                tk = Div;
                return;
            }
        } else if (tk == '\'' || tk == '"') {
            // 保存data起始位置
            pp = data;
            while (*p != '\0' && *p != tk) {
                if ((ival = *p++) == '\\') {
                    // \n
                    if ((ival = *p++) == 'n')
                        ival = '\n';
                }
                // 字符串
                if (tk == '"')
                    *data++ = ival;
            }
            ++p;
            if (tk == '"') {
                ival = (int) pp;
            } else {
                // char
                tk = Num;
            }
            return;
        } else if (tk == '=') {
            if (*p == '=') {
                // ==
                ++p;
                tk = Eq;
            } else {
                // =
                tk = Assign;
            }
            return;
        } else if (tk == '+') {
            if (*p == '+') {
                // ++
                ++p;
                tk = Inc;
            } else {
                // +
                tk = Add;
            }
            return;
        } else if (tk == '-') {
            if (*p == '-') {
                // --
                ++p;
                tk = Dec;
            } else {
                // -
                tk = Sub;
            }
            return;
        } else if (tk == '!') {
            if (*p == '=') {
                // !=
                ++p;
                tk = Ne;
            }
            return;
        } else if (tk == '<') {
            if (*p == '=') {
                // <=
                ++p;
                tk = Le;
            } else if (*p == '<') {
                // <<
                ++p;
                tk = Shl;
            } else {
                // <
                tk = Lt;
            }
            return;
        } else if (tk == '>') {
            if (*p == '=') {
                // >=
                ++p;
                tk = Ge;
            } else if (*p == '>') {
                // >>
                ++p;
                tk = Shr;
            } else {
                // >
                tk = Gt;
            }
            return;
        } else if (tk == '|') {
            if (*p == '|') {
                // 逻辑或 ||
                ++p;
                tk = Lor;
            } else {
                // |
                tk = Or;
            }
            return;
        } else if (tk == '&') {
            if (*p == '&') {
                // 逻辑与 &&
                ++p;
                tk = Lan;
            } else {
                // 与运算 &
                tk = And;
            }
            return;
        } else if (tk == '^') {
            // 异或运算 ^
            tk = Xor;
            return;
        } else if (tk == '%') {
            // 取余数 %
            tk = Mod;
            return;
        } else if (tk == '*') {
            // *
            tk = Mul;
            return;
        } else if (tk == '[') {
            // [
            tk = Brak;
            return;
        } else if (tk == '?') {
            tk = Cond;
            return;
        } else if (tk == '~' || tk == ';' || tk == '{' || tk == '}' ||
                   tk == '(' || tk == ')' || tk == ']' || tk == ',' ||
                   tk == ':') {
            return;
        }
    }
}


void expr(int lev)
{
    int t, *d;

    if (!tk) {
        printf("%d: unexpected eof in expression\n", line);
        exit(-1);
    } else if (tk == Num) {
        *++e = IMM;
        *++e = ival;
        next();
        ty = INT;
    } else if (tk == '"') {
        *++e = IMM;
        *++e = ival;
        next();
        while (tk == '"')
            next();
        data = (char *) ((int) data + sizeof(int) & -sizeof(int));
        ty = PTR;
    } else if (tk == Sizeof) {
        next();
        // sizeof后边一定是个括号，否则就是词法解析错误
        if (tk == '(') {
            next();
        } else {
            printf("%d: open paren expected in sizeof\n", line);
            exit(-1);
        }
        ty = INT;
        if (tk == Int) {
            next();
        } else if (tk == Char) {
            next();
            ty = CHAR;
        }
        while (tk == Mul) {
            next();
            ty = ty + PTR;
        }
        if (tk == ')') {
            next();
        } else {
            printf("%d: close paren expected in sizeof\n", line);
            exit(-1);
        }
    } else if (tk == Id) {
        d = id;
        next();
        if (tk == '(') {
            next();
            t = 0;
            while (tk != ')') {
                expr(Assign);
                *++e = PSH;
                ++t;
                if (tk == ',') {
                    next();
                }
                next();
                if (d[Class] == Sys) {
                    *++e = d[Val];
                } else if (d[Class] == Fun) {
                    *++e = JSR;
                    *++e = d[Val];
                } else {
                    printf("%d: bad function call\n", line);
                    exit(-1);
                }
                if (t) {
                    *++e = ADJ;
                    *++e = t;
                }
                ty = d[Type];
            }
        }
    }
}
