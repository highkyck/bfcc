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
    Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
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
        // function
        if (tk == '(') {
            next();
            t = 0;
            // 函数参数
            while (tk != ')') {
                expr(Assign);
                *++e = PSH;
                ++t;
                if (tk == ',') {
                    next();
                }
            }
            next();
            if (d[Class] == Sys) {
                // system function
                *++e = d[Val];
            } else if (d[Class] == Fun) {
                // function call
                *++e = JSR;
                *++e = d[Val];
            } else {
                printf("%d: bad function call\n", line);
                exit(-1);
            }
            // clean the stack for arguments
            if (t) {
                *++e = ADJ;
                *++e = t;
            }
            ty = d[Type];
        } else if (d[Class] == Num) {
            // enum
            *++e = IMM;
            *++e = d[Val];
            ty = INT;
        } else {
            // variable
            if (d[Class] = Loc) {
                *++e = LEA;
                *++e = loc - d[Val];
            } else if (d[Class] == Glo) {
                *++e = IMM;
                *++e = d[Val];
            } else {
                printf("%d: undefined variable\n", line);
                exit(-1);
            }
            ty = d[Type];
            // 如果type是CHAR，则将将对应的地址中的字符载入ax中，
            // 否则将对应地址中的整数载入ax中
            *++e = ty == CHAR ? LC : LI;
        }
    } else if (tk == '(') {
        // (int) a / (int *)a
        next();
        if (tk == Int || tk == Char) {
            t = (tk == Int) ? INT : CHAR;
            next();
            while (tk == Mul) {
                next();
                t = t + PTR;
            }
            if (tk == ')') {
                next();
            } else {
                printf("%d: bad case\n", line);
                exit(-1);
            }
            expr(Inc);  // case跟++有同样的优先级
            ty = t;
        } else {
            // 普通的括号
            expr(Assign);
            if (tk == ')') {
                next();
            } else {
                printf("%d: close paren expected\n", line);
                exit(-1);
            }
        }
    } else if (tk == Mul) {
        // dereference *<add>
        next();
        expr(Inc);  // dereference跟++有同样的优先级
        if (ty > INT) {
            ty = ty - PTR;
        } else {
            printf("%d: bad dereference\n", line);
            exit(-1);
        }
        *++e = (ty == CHAR) ? LC : LI;
    } else if (tk == And) {
        // addredd of (&)
        next();
        expr(Inc);
        if (*e == LC || *e == LI) {
            --e;
        } else {
            printf("%d: bad addredd-of\n", line);
            exit(-1);
        }
        ty = ty + PTR;
    } else if (tk == '!') {
        next();
        expr(Inc);
        *++e = PSH;
        *++e = IMM;
        *++e = 0;
        *++e = EQ;
        ty = INT;
    } else if (tk == '~') {
        next();
        expr(Inc);
        *++e = PSH;
        *++e = IMM;
        *++e = -1;
        *++e = XOR;
        ty = INT;
    } else if (tk == Add) {
        next();
        expr(Inc);
        ty = INT;
    } else if (tk == Sub) {
        next();
        *++e = IMM;
        if (tk == Num) {
            *++e = -ival;
            next();
        } else {
            *++e = -1;
            *++e = PSH;
            expr(Inc);
            *++e = MUL;
        }
        ty = INT;
    } else if (tk == Inc || tk == Dec) {
        // 左值表达式
        t = tk;
        next();
        expr(Inc);
        if (*e == LC) {
            *e = PSH;
            *++e = LC;
        } else if (*e == LI) {
            *e = PSH;
            *++e = LI;
        } else {
            printf("%d: bad lvalue in pre-increment\n", line);
            exit(-1);
        }

        *++e = PSH;
        *++e = IMM;
        *++e = (ty > PTR) ? sizeof(int) : sizeof(char);
        *++e = (t == Inc) ? ADD : SUB;
        *++e = (ty == CHAR) ? SC : SI;
    } else {
        printf("%d: bad expression\n", line);
        exit(-1);
    }

    // binary
    while (tk >= lev) {
        t = ty;
        if (tk == Assign) {
            next();
            if (*e == LC || *e == LI) {
                *e = PSH;
            } else {
                printf("%d: bad lvalue in assignment\n", line);
                exit(-1);
            }
            expr(Assign);
            ty = t;
            *++e = ty == CHAR ? SC : SI;
        } else if (tk == Cond) {
            // expr a : b
            next();
            *++e = BZ;
            d = ++e;
            expr(Assign);
            if (tk == ':') {
                next();
            } else {
                printf("%d: conditional missing colon\n", line);
                exit(-1);
            }
            *d = (int) (e + 3);
            *++e = JMP;
            d = ++e;
            expr(Cond);
            *d = (int) (e + 1);
        } else if (tk == Lor) {
            next();
            *++e = BNZ;
            d = ++e;
            expr(Lan);
            *d = (int) (e + 1);
            ty = INT;
        } else if (tk == Lan) {
            next();
            *++e = BZ;
            d = ++e;
            expr(Or);
            *d = (int) (e + 1);
            ty = INT;
        } else if (tk == Or) {
            next();
            *++e = PSH;
            expr(Xor);
            *++e = OR;
            ty = INT;
        } else if (tk == Xor) {
            next();
            *++e = PSH;
            expr(And);
            *++e = XOR;
            ty = INT;
        } else if (tk == And) {
            next();
            *++e = PSH;
            expr(Eq);
            *++e = AND;
            ty = INT;
        } else if (tk == Eq) {
            next();
            *++e = PSH;
            expr(Lt);
            *++e = EQ;
            ty = INT;
        } else if (tk == Ne) {
            next();
            *++e = PSH;
            expr(Lt);
            *++e = NE;
            ty = INT;
        } else if (tk == Lt) {
            next();
            *++e = PSH;
            expr(Shl);
            *++e = LT;
            ty = INT;
        } else if (tk == Gt) {
            next();
            *++e = PSH;
            expr(Shl);
            *++e = GT;
            ty = INT;
        } else if (tk == Le) {
            next();
            *++e = PSH;
            expr(Shl);
            *++e = LE;
            ty = INT;
        } else if (tk == Ge) {
            next();
            *++e = PSH;
            expr(Shl);
            *++e = GE;
            ty = INT;
        } else if (tk == Shl) {
            next();
            *++e = PSH;
            expr(Add);
            *++e = SHL;
            ty = INT;
        } else if (tk == Shr) {
            next();
            *++e = PSH;
            expr(Add);
            *++e = SHR;
            ty = INT;
        } else if (tk == Add) {
            next();
            *++e = PSH;
            expr(Mul);
            if ((ty = t) > PTR) {
                *++e = PSH;
                *++e = IMM;
                *++e = sizeof(int);
                *++e = MUL;
            }
            *++e = ADD;
        } else if (tk == Sub) {
            next();
            *++e = PSH;
            expr(Mul);
            if (t > PTR && t == ty) {
                *++e = SUB;
                *++e = PSH;
                *++e = IMM;
                *++e = sizeof(int);
                *++e = DIV;
                ty = INT;
            } else if ((ty = t) > PTR) {
                *++e = PSH;
                *++e = IMM;
                *++e = sizeof(int);
                *++e = MUL;
                *++e = SUB;
            } else {
                *++e = SUB;
            }
        } else if (tk == Mul) {
            next();
            *++e = PSH;
            expr(Inc);
            *++e = MUL;
            ty = INT;
        } else if (tk == Div) {
            next();
            *++e = PSH;
            expr(Inc);
            *++e = DIV;
            ty = INT;
        } else if (tk == Mod) {
            next();
            *++e = PSH;
            expr(Inc);
            *++e = MOD;
            ty = INT;
        } else if (tk == Inc || tk == Dec) {
            if (*e == LC) {
                *e = PSH;
                *++e = LC;
            } else if (*e = LI) {
                *e = PSH;
                *++e = LI;
            } else {
                printf("%d: bad lvalue in post-increment\n", line);
                exit(-1);
            }
            *++e = PSH;
            *++e = IMM;
            *++e = (ty > PTR) ? sizeof(int) : sizeof(char);
            *++e = (tk == Inc) ? ADD : SUB;
            *++e = (ty == CHAR) ? SC : SI;
            *++e = PSH;
            *++e = IMM;
            *++e = (ty > PTR) ? sizeof(int) : sizeof(char);
            *++e = (tk == Inc) ? SUB : ADD;
            next();
        } else if (tk == Brak) {
            next();
            *++e = PSH;
            expr(Assign);
            if (tk == ']') {
                next();
            } else {
                printf("%d: close bracket expected\n", line);
                exit(-1);
            }
            if (t > PTR) {
                *++e = PSH;
                *++e = IMM;
                *++e = sizeof(int);
                *++e = MUL;
            } else if (t < PTR) {
                printf("%d: pointer type expected\n", line);
                exit(-1);
            }
            *++e = ADD;
            *++e = ((ty = t - PTR) == CHAR) ? LC : LI;
        } else {
            printf("%d: compiler error tk = %d\n", line, tk);
            exit(-1);
        }
    }
}

void stmt()
{
    int *a, *b;
    if (tk == If) {
        // if () ...
        next();
        if (tk == '(') {
            next();
        } else {
            printf("%d: open paren expected\n", line);
            exit(-1);
        }
        expr(Assign);
        if (tk == ')') {
            next();
        } else {
            printf("%d: close paren expected\n", line);
            exit(-1);
        }
        *++e = BZ;
        b = ++e;
        stmt();
        if (tk == Else) {
            // else ...
            *b = (int) (e + 3);
            *++e = JMP;
            b = ++e;
            next();
            stmt();
        }
        *b = (int) (e + 1);
    } else if (tk == While) {
        // while ()
        next();
        a = e + 1;
        if (tk == '(') {
            next();
        } else {
            printf("%d: open paren expected\n", line);
            exit(-1);
        }
        expr(Assign);
        if (tk == ')') {
            next();
        } else {
            printf("%d: close paren expected\n", line);
            exit(-1);
        }
        *++e = BZ;
        b = ++e;
        stmt();
        *++e = JMP;
        *++e = (int) a;
        *b = (int) (e + 1);
    } else if (tk == Return) {
        next();
        if (tk != ';') {
            expr(Assign);
        }
        *++e = LEV;
        if (tk == ';') {
            next();
        } else {
            printf("%d: semicolon expected\n", line);
            exit(-1);
        }
    } else if (tk == '{') {
        next();
        while (tk != '}') {
            stmt();
            next();
        }
    } else if (tk == ';') {
        next();
    } else {
        expr(Assign);
        if (tk == ';') {
            next();
        } else {
            printf("%d: semicolon expected\n", line);
            exit(-1);
        }
    }
}

int main(int argc, char *argv[])
{
    int fd, bt, ty, poolsz, *idmain;

    // vm registers
    int *pc,  // 程序计数器
        *sp,  // 指针寄存器
        *bp,  // 基址指针
        a,    // ax 通用寄存器
        cycle;

    int i, *t;

    // 第一个参数是程序本身
    --argc;
    ++argv;

    // -s
    if (argc > 0 && **argv == '-' && (*argv)[1] == 's') {
        src = 1;
        --argc;
        ++argv;
    }

    // -d
    if (argc > 0 && **argv == '-' && (*argv)[1] == 'd') {
        debug = 1;
        --argc;
        ++argv;
    }

    if (argc < 1) {
        printf("usage: bfcc [-s] [-d] file ...\n");
        return -1;
    }

    if ((fd = open(*argv, 0)) < 0) {
        printf("could not open(%s)\n", *argv);
        return -1;
    }

    poolsz = 256 * 1024;
    if (!(sym = malloc(poolsz))) {
        printf("could not malloc(%d) symbol area\n", poolsz);
        return -1;
    }
    if (!(le = e = malloc(poolsz))) {
        printf("could not malloc(%d) text area\n", poolsz);
        return -1;
    }
    if (!(data = malloc(poolsz))) {
        printf("could not malloc(%d) data area\n", poolsz);
        return -1;
    }
    if (!(sp = malloc(poolsz))) {
        printf("could not malloc(%d) stack area\n", poolsz);
        return -1;
    }

    memset(sym, 0, poolsz);
    memset(e, 0, poolsz);
    memset(data, 0, poolsz);

    p = "char else enum if int return sizeof while"
        "open read close printf malloc free memset memcmp exit void main";

    // add keywords to symbol table
    i = Char;
    while (i <= While) {
        next();
        id[Tk] = i++;
    }

    // add library to symbol table
    i = OPEN;
    while (i <= EXIT) {
        next();
        id[Class] = Sys;
        id[Type] = INT;
        id[Val] = i++;
    }

    next();
    id[Tk] = Char;  // handle void type
    next();
    idmain = id;  // keep track of main

    if (!(lp = p = malloc(poolsz))) {
        printf("could not malloc(%d) source area\n", poolsz);
        return -1;
    }

    if ((i = read(fd, p, poolsz - 1)) <= 0) {
        printf("read() returned %d\n", i);
        return -1;
    }

    // add eof character
    p[i] = '\0';
    close(fd);

    // parse declarations
    line = 1;
    next();
    while (tk) {
        bt = INT;  // basetype
        if (tk == Int) {
            next();
        } else if (tk == Char) {
            next();
            bt = CHAR;
        } else if (tk == Enum) {
            next();
            if (tk != '{') {
                // enum name
                next();
            }
            if (tk == '{') {
                next();
                i = 0;
                while (tk != '}') {
                    if (tk != Id) {
                        printf("%d: bad enum identifier %d\n", line, tk);
                        return -1;
                    }
                    next();
                    if (tk == Assign) {
                        next();
                        if (tk != Num) {
                            printf("%d: bad enum initializer\n", line);
                            return -1;
                        }
                        i = ival;
                        next();
                    }
                    id[Class] = Num;
                    id[Type] = INT;
                    id[Val] = i++;
                    if (tk == ',') {
                        next();
                    }
                }
                next();
            }
        }

        while (tk != ';' && tk != '}') {
            ty = bt;
            while (tk == Mul) {
                next();
                ty = ty + PTR;
            }
            if (tk != Id) {
                printf("%d: bad global declaration\n", line);
                return -1;
            }
            if (id[Class]) {
                printf("%d: duplicate global definition\n", line);
                return -1;
            }
            next();
            id[Type] = ty;
            if (tk == '(') {  // function
                id[Class] = Fun;
                id[Val] = (int) (e + 1);
                next();
                i = 0;
                while (tk != ')') {
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
                    if (tk != Id) {
                        printf("%d: bad parameter declaration\n", line);
                        return -1;
                    }
                    if (id[Class] == Loc) {
                        printf("%d: duplicate parameter definition\n", line);
                        return -1;
                    }
                    id[HClass] = id[Class];
                    id[Class] = Loc;
                    id[HType] = id[Type];
                    id[Type] = ty;
                    id[HVal] = id[Val];
                    id[Val] = i++;
                    next();
                    if (tk == ',') {
                        next();
                    }
                }
                next();
                if (tk != '{') {
                    printf("%d: bad function definition\n", line);
                    return -1;
                }
                loc = ++i;
                next();
                while (tk == Inc || tk == Char) {
                    bt = (tk == Int) ? INT : CHAR;
                    next();
                    while (tk != ';') {
                        ty = bt;
                        while (tk == Mul) {
                            next();
                            ty = ty + PTR;
                        }
                        if (tk != Id) {
                            printf("%d: bad local declaration\n", line);
                            return -1;
                        }
                        if (id[Class] == Loc) {
                            printf("%d: duplicate local definition\n", line);
                            return -1;
                        }
                        id[HClass] = id[Class];
                        id[Class] = Loc;
                        id[HType] = id[Type];
                        id[Type] = ty;
                        id[HVal] = id[Val];
                        id[Val] = ++i;
                        next();
                        if (tk == ',')
                            next();
                    }
                    next();
                }
                *++e = ENT;
                *++e = i - loc;
                while (tk != '}') {
                    stmt();
                }
                *++e = LEV;
                id = sym;  // unwind symbol table local
                while (id[Tk]) {
                    if (id[Class] == Loc) {
                        id[Class] = id[HClass];
                        id[Type] = id[HType];
                        id[Val] = id[HVal];
                    }
                    id = id + Idsz;
                }
            } else {
                id[Class] = Glo;
                id[Val] = (int) data;
                data = data + sizeof(int);
            }
            if (tk == ',') {
                next();
            }
        }
        next();
    }

    if (!(pc = (int *) idmain[Val])) {
        printf("main() mot defined\n");
        return -1;
    }

    if (src) {
        return 0;
    }

    // setup stack
    bp = sp = (int *) ((int) sp + poolsz);
    *--sp = EXIT;  // call exit if main returns
    *--sp = PSH;
    t = sp;
    *--sp = argc;
    *--sp = (int) argv;
    *--sp = (int) t;

    // run...
    cycle = 0;
    while (1) {
        i = *pc++;
        ++cycle;
        if (debug) {
            printf("%d> %.4s", cycle, &"LEA ,IMM ,JMP ,JSR ,BZ  ,BNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PSH ,"
                                       "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
                                       "OPEN,READ,CLOS,PRTF,MALC,FREE,MSET,MCMP,EXIT,"[i * 5]);
        }
        if (i <= ADJ) {
            printf(" %d\n", *pc);
        } else {
            printf("\n");
        }

        if (i == LEA) {
            // load local address
            a = (int) (bp + *pc++);
        } else if (i == IMM) {
            // load global address or immediate
            a = *pc++;
        } else if (i == JMP) {
            // jump
            pc = (int *) *pc;
        } else if (i == JSR) {
            // jump to subroutine
            *--sp = (int) (pc + 1);
            pc = (int *) *pc;
        } else if (i == BZ) {
            // branch if zero
            pc = a ? pc + 1 : (int *) *pc;
        } else if (i == BNZ) {
            // branch if not zero
            pc = a ? (int *) *pc : pc + 1;
        } else if (i == ENT) {
            // enter subroutine
            *--sp = (int) bp;
            bp = sp;
            sp = sp - *pc++;
        } else if (i == ADJ) {
            // stack adjust
            sp = sp + *pc++;
        } else if (i == LEV) {
            // leave subroutine
            sp = bp;
            bp = (int *) *sp++;
            pc = (int *) *sp++;
        } else if (i == LI) {
            // load int
            a = *(int *) a;
        } else if (i == LC) {
            // load char
            a = *(char *) a;
        } else if (i == SI) {
            // store int
            *(int *) *sp++ = a;
        } else if (i == SC) {
            // store char
            a = *(char *) *sp++ = a;
        } else if (i == PSH) {
            // push
            *--sp = a;
        }

        else if (i == OR) {
            a = *sp++ | a;
        } else if (i == XOR) {
            a = *sp++ ^ a;
        } else if (i == AND) {
            a = *sp++ & a;
        } else if (i == EQ) {
            a = *sp++ == a;
        } else if (i == NE) {
            a = *sp++ != a;
        } else if (i == LT) {
            a = *sp++ < a;
        } else if (i == GT) {
            a = *sp++ > a;
        } else if (i == LE) {
            a = *sp++ <= a;
        } else if (i == GE) {
            a = *sp++ >= a;
        } else if (i == SHL) {
            a = *sp++ << a;
        } else if (i == SHR) {
            a = *sp++ >> a;
        } else if (i == ADD) {
            a = *sp++ + a;
        } else if (i == SUB) {
            a = *sp++ - a;
        } else if (i == MUL) {
            a = *sp++ * a;
        } else if (i == DIV) {
            a = *sp++ / a;
        } else if (i == MOD) {
            a = *sp++ % a;
        }
        // system function call
        else if (i == OPEN) {
            a = open((char *) sp[1], *sp);
        } else if (i == READ) {
            a = read(sp[2], (char *) sp[1], *sp);
        } else if (i == CLOS) {
            a = close(*sp);
        } else if (i == PRTF) {
            t = sp + pc[1];
            a = printf((char *) t[-1], t[-2], t[-3], t[-4], t[-5], t[-6]);
        } else if (i == MALC) {
            a = (int) malloc(*sp);
        } else if (i == FREE) {
            free((void *) *sp);
        } else if (i == MSET) {
            a = (int) memset((char *) sp[2], sp[1], *sp);
        } else if (i == MCMP) {
            a = memcmp((char *) sp[2], (char *) sp[1], *sp);
        } else if (i == EXIT) {
            printf("exit(%d) cycle = %d\n", *sp, cycle);
            return *sp;
        } else {
            printf("unknown instruction = %d, cycle = %d\n", i, cycle);
            return -1;
        }
    }

    return 0;
}
