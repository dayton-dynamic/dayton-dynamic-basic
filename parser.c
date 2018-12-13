/*
    Dayton Dynamic BASIC
    demonstration parser
    MWA 2018
*/

#include "all.h"

int str_exp(char **ss, lego **result);
int num_exp(char **ss, lego **result);
int statements(char **ss, lego **result);

/******************************* LEXER SECTION ********************************/

/*
 *  Advance *ss past any whitespace.
 */
void eatBlanks(char **ss)
{
    while (isspace(**ss))
        ++*ss;
}

/*
 *  Detect end of input. (Note that input is a single line.)
 */
int nothingMore(char **ss)
{
    char *s = *ss;

    eatBlanks(&s);
    if (*s) return 0;
    *ss = s;
    return 1;
}

/*
 *  Two formats are supported for string literals:
 *
 *      [This is the first format.]
 *      [Most strings literals are enclosed in square brackets.]
 *
 *      ]$The second format starts with ] and lets you choose the delimiter.$
 *      ]7Here is a different example using the numeral seven.7
 *
 *  An artificial restriction is imposed for clarity: you're not allowed
 *  to choose spaces or non-printing characters as delimiters.
 *
 *  Strings in this system can't hold CHR$(0), the ASCII null.
 */
int str_lit(char **ss, lego **result)
{
    char *s = *ss, *found;
    int ender = ']';
    lego *res;

    eatBlanks(&s);
    if (*s == ender)
        ender = *++s;
    else if (*s != '[')
        return 0;
    if (!isgraph(ender))
        return 0;
    ifnot (found = strchr(++s, ender))
        return 0;

    res = newLego(wSTRLIT);
    res -> s = copySubstring(s, found);
    res -> lit_delim = ender != ']' ? ender : 0;
    *result = res;
    *ss = 1 + found;
    return 1;
}

/*
 *  Unquoted string literal for INPUT statement at runtime.
 *  Skip blank space, then read whatever's left up to comma or EOL.
 */
int unquoted_str_lit(char **ss, lego **result)
{
    char *s = *ss, *end;

    eatBlanks(&s);
    end = strchr(s, ',');
    *result = newLego(wSTRLIT);
    (*result) -> s = copySubstring(s, end);
    *ss = end ? end : s + strlen(s);
    return 1;
}

/*
 *  Detect numerals. Allows . for fractional and _ for grouping.
 *  There is no scientific notation support.
 *
 *  Examples:  0  .01  555  12  123.456  937_848_0942
 *
 *  Negative values are only seen at runtime (within the INPUT statement),
 *  because unary() gobbles any minus signs within numeric expressions.
 */
int num_lit(char **ss, lego **result)
{
    char *s = *ss;
    double r = 0, scale = 1;
    int dot = 0, dig, any = 0, sign = 1;
    lego *res;

    eatBlanks(&s);
    if (*s == '-')
        sign = -1, ++s;
    for (;; ++s) {
        if (*s == '.') {
            ++dot;
            continue;
        }
        if (*s == '_')
            continue;
        ifnot (isdigit(dig = *s))
            break;
        any = 1;
        r = 10 * r + dig - '0';
        if (dot) scale *= 10;
    }

    if (!any || dot > 1)
        return 0;

    res = newLego(wNUMLIT);
    res -> n = sign * r / scale;
    *result = res;
    *ss = s;
    return 1;
}

/*
 *  Detect line numbers, which are simply non-negative integers.
 *  Rather than write a simple integer parsing routine, I call the
 *  floating point one that already exists. Neither way is better.
 */
int line_num(char **ss, lego **result)
{
    char *s = *ss;
    lego *res;

    if (!num_lit(&s, &res))
        return 0;
    if (res -> n != trunc(res -> n)) {
        warn("fractional line numbers are not supported");
        byeLego(res);
        return 0;
    }
    res -> what = wLINENUM;
    *result = res;
    *ss = s;
    return 1;
}

/*
 *  Variable names must start with a letter. Letters are case-insensitive.
 *  Other symbols allowed are digits, dot, apostrophe, and quote. The last
 *  three aren't allowed in many languages. I support them because they
 *  allow more expressive names.
 *
 *  Examples:  Q  my25thVar  my.name  my.number  F  F'  F"
 *
 *  dollarFlag must only be 0 or 1, and indicates whether I want a
 *  numeric (0) or string (1) variable. They have separate namespaces, except
 *  that string variables have a $ at the end. Thus T and T$ are different.
 */
int varName(char **ss, lego **result, int dollarFlag)
{
    char *s = *ss, *begin, *ucase;
    lego *res;

    eatBlanks(&s);
    if (!isalpha(*s))
        return 0;
    for (begin = s; isalnum(*s) || *s && strchr("\".'", *s); ++s);
    if (*s == '$' != dollarFlag) return 0;

    res = newLego(dollarFlag ? wSTRVAR : wNUMVAR);
    res -> s = copySubstring(begin, s);
    for (ucase = res -> s; *ucase; ++ucase)
        *ucase = toupper(*ucase);
    *result = res;
    *ss = s + dollarFlag;
    return 1;
}

/*
 *  Parse a numeric variable.
 */
int num_var(char **ss, lego **result)
{
    return varName(ss, result, 0);
}

/*
 *  Parse a string variable.
 */
int str_var(char **ss, lego **result)
{
    return varName(ss, result, 1);
}

/*
 *  Keywords are case-insensitive and have the same naming restrictions
 *  as variable names in order to assure separation.
 */
int keyword(char **ss, char *kw)
{
    char *s = *ss;

    eatBlanks(&s);
    for (; *s && toupper(*s) == toupper(*kw); ++s, ++kw);
    if (*kw || isalnum(*s) || *s && strchr("\".'$", *s)) return 0;
    *ss = s;
    return 1;
}

/*
 *  Symbols can have multiple characters (like ">=") and require no separation
 *  from anything. This can and does cause issues, because code that looks
 *  for "<" can conflict with code that looks for "<>". Look for the word
 *  KLUDGE to see how I worked around this.
 *
 *  As none of my symbols have letters, I don't consider case sensitivity.
 */
int symbol(char **ss, char *sym)
{
    char *s = *ss;

    eatBlanks(&s);
    for (; *s && *s == *sym; ++s, ++sym);
    if (*sym) return 0;
    *ss = s;
    return 1;
}

/*
 *  Given a list of possible symbols, parse to see which if any appears here.
 */
int general_symbol_factory(
    char **ss,
    char **symbols,
    int *which
) {
    int w;

    for (w = 0; *symbols; ++symbols, ++w) {
        if (symbol(ss, *symbols)) {
            *which = w;
            return 1;
        }
    }
    return 0;
}

/*
 *  This parser needs eleven functions that parse left-associative
 *  binary expressions. Since they all do nearly the same thing,
 *  the heavy lifting is off-loaded into this helper function.
 *
 *  A left-associative operation is something like subtraction,
 *  which is evaluated from left to right. That is,
 *
 *  A - B - C - D - E   means the same as   (((A - B) - C) - D) - E
 *
 *  We have to allow more than one symbol to separate sub-expressions,
 *  because certain left-associative operators have the same-precedence.
 *  (+ and - happen together, and / * \ MOD all happen together.)
 */
int general_left_binary(
    char **ss,
    lego **result,
    char **symbols,
    int *enums,
    int (*subFn)(char **s, lego **),
    char *errMsg
) {
    char *s = *ss, **syms, *prev;
    lego *left, *right, *parent;
    int what, *e;

    if (!subFn(&s, &left))
        return 0;

    while (1) {

        for (syms = symbols, e = enums; *syms; ++syms, ++e) {
            prev = s;
            if (isalpha(**syms)) {
                if (keyword(&s, *syms))
                    goto found;
            }
            else if (symbol(&s, *syms))
                goto found;
        }
        break;
found:  what = *e;

        if (what == wKLUDGE) {      /* don't let < break <> */
            s = prev;
            break;
        }

        if (!subFn(&s, &right)) {
            warn(errMsg);
            byeLego(left);
            return 0;
        }
        parent = newLego(what);
        parent -> a[0] = left, parent -> a[1] = right;
        left = parent;
    }

    *result = left;
    *ss = s;
    return 1;
}

/*
 *  This takes a list of built-in function names like LEN, RIGHT$,
 *  etc. and parses calls to these functions. Along with the names,
 *  an array of strings indicates the argument types. For example,
 *  MID$ takes three arguments: the first must be a string, and the 
 *  others must be numeric. So where "mid$" appears in 'names' and
 *  wMID appears in 'enums', we pass "snn" in 'args' to tell this
 *  function we expect three 's'tring and 'n'umeric arguments.
 */
int general_function_factory(
    char **ss,
    lego **result,
    char **names,
    char **args,    // argument types: 'n'umeric or 's'tring
    int *enums,
    int *error
) {
    char *s = *ss, *arg;
    int i;
    lego *l, *sub = NULL;

    *error = 0;
    for (i=0; names[i]; i++) {
        if (keyword(&s, names[i]))
            goto Y;
    }
    return 0;

Y:  l = newLego(enums[i]);
    arg = args[i];
    if (!symbol(&s, "(")) {
        warn("need ( after function name");
        goto N;
    }

    for (i = 0; *arg; ++i, ++arg) {
        if (*arg == 'n') {
            if (!num_exp(&s, &sub)) {
                warn("need numeric expression in function call");
                goto N;
            }
        }
        else {
            if (!str_exp(&s, &sub)) {
                warn("need string expression in function call");
                goto N;
            }
        }
        l -> a[i] = sub;

        if (arg[1] && !symbol(&s, ",")) {
            warn("need , between function call arguments");
            goto N;
        }
    }

    if (!symbol(&s, ")")) {
        warn("need ) at end of function call");
        goto N;
    }

    *result = l;
    *ss = s;
    return 1;

N:  byeLego(l);
    *error = 1;
    return 0;
}

/*
 *  Convenience function for making a lego from a particular keyword.
 */
int general_keyword_factory(
    char **ss,
    lego **result,
    int *enums
) {
    for (; *enums; ++enums) {
        if (keyword(ss, guys[*enums])) {
            *result = newLego(*enums);
            return 1;
        }
    }
    return 0;
}

/*
 *  This parses a list of things and allows different separators to be
 *  used between items within the same call. The main use is BASIC's PRINT
 *  keyword, which separates each item printed with whitespace unless
 *  a semicolon appears. So
 *
 *      PRINT [Marc]; [was], [here]
 *
 *  will output "Marcwas here". So this routine exists to keep those
 *  separators straight.
 *
 *  Most lists don't allow a separator at the end, so an error message is
 *  specified to output when that happens. But PRINT is different; a
 *  trailing ; indicates that no newline should be added. We specify this
 *  by passing NULL via 'errMsg' instead of a desired error message.
 */
int general_list_factory(
    char **ss,
    lego **result,
    char **seps,
    int (*subFn)(char **s, lego **),
    char *errMsg
) {
    char *s = *ss;
    lego *head, *tail;
    int which;

    if (!subFn(&s, &head))
        return 0;
    tail = head;

    while (general_symbol_factory(&s, seps, &which)) {
        tail -> list_delim = which;
        if (subFn(&s, &tail -> next))
            tail = tail -> next;
        else if (errMsg) {
            warn(errMsg);
            byeLego(head);
            return 0;
        }
        else
            break;
    }

    *result = head;
    *ss = s;
    return 1;
}

/**************************** STRING EXPRESSIONS ******************************/

/*
 *  Many of the routines that follow begin with a summary of the grammar
 *  being parsed. You'll see it below and begin to figure it out.
 *
 *  String expressions for this program are  not yet complete; for
 *  example the statement "IF [marc] < [catherine] THEN STOP" doesn't work.
 *
 *  str_term:
 *      str_lit
 *      str_var
 *      ( str_exp )
 *      func_returning_str ( argument_list )
 */
int str_term(char **ss, lego **result)
{
    char *s = *ss;
    lego *sub;
    int error;

    char *fns[] = { "chr$", "left$", "mid$", "right$", "space$",
        "str$", "string$", NULL };
    char *args[] = { "n", "sn", "snn", "sn", "n", "n", "ns", NULL };
    int enums[] = { wCHR, wLEFT, wMID, wRIGHT, wSPACE, wSTR, wSTRING, 0 };

    if (general_function_factory(&s, &sub, fns, args, enums, &error))
        goto Y;
    if (error)
        return 0;

    if (str_lit(&s, &sub) || str_var(&s, &sub))
        goto Y;

    if (symbol(&s, "(") && str_exp(&s, &sub)) {
        if (!symbol(&s, ")")) {
            byeLego(sub);
            warn("right paren needed after string expression");
            return 0;
        }
        sub -> force_parens = 1;
        goto Y;
    }

    return 0;
    
Y:  *result = sub;
    *ss = s;
    return 1;
}

/*
 *  str_exp:
 *      str_exp + str_term
 *      str_term
 */
int str_exp(char **ss, lego **result)
{
    char *syms[] = { "+", NULL };
    int enums[] = { wCAT, 0 };

    return general_left_binary(ss, result, syms, enums, str_term,
        "string needed after catenation operator +");
}

/**************************** NUMBER EXPRESSIONS ******************************/

/*
 *  num_term:
 *      num_lit
 *      num_var
 *      ( num_exp )
 *      func_returning_num ( argument_list )
 */
int num_term(char **ss, lego **result)
{
    char *s = *ss;
    lego *sub;
    int error;
    char *fns[] = { "abs", "asc", "atan", "cos", "exp", "fix", "instr", "int",
        "len", "log", "rnd", "sgn", "sin", "sqrt", "tan", "val", NULL };
    char *args[] = { "n", "s", "n", "n", "n", "n", "nss", "n",
        "s", "n", "n", "n", "n", "n", "n", "s", NULL };
    int enums[] = { wABS, wASC, wATAN, wCOS, wEXP, wFIX, wINSTR,
        wINT, wLEN, wLOG, wRND, wSGN, wSIN, wSQRT, wTAN, wVAL, 0 };

    if (general_function_factory(&s, &sub, fns, args, enums, &error))
        goto Y;
    if (error)
        return 0;

    if (num_lit(&s, &sub) || num_var(&s, &sub))
        goto Y;

    if (symbol(&s, "(") && num_exp(&s, &sub)) {
        if (!symbol(&s, ")")) {
            byeLego(sub);
            warn("right paren needed after numeric expression");
            return 0;
        }
        sub -> force_parens = 1;
        goto Y;
    }

    return 0;
    
Y:  *result = sub;
    *ss = s;
    return 1;
}

/*
 *  unary:
 *      + num_term
 *      - num_term
 *      num_term
 */
int unary(char **ss, lego **result)
{
    char *s = *ss;
    int neg = 0, must = 0;
    lego *l;

    while (1) {
        eatBlanks(&s);
        switch (*s) {
            case '-': neg = !neg;
            case '+': ++s; must = 1; continue;
        }
        break;
    }

    if (!num_term(&s, result)) {
        if (must)
            warn("need something after unary + or -");
        return 0;
    }

    if (neg) {
        l = newLego(wNEGATE);
        l -> a[0] = *result;
        *result = l;
    }

    *ss = s;
    return 1;
}

/*
 *  power:
 *      power ^ unary
 *      unary
 */
int power(char **ss, lego **result)
{
    char *syms[] = { "^", NULL };
    int enums[] = { wPOWER, 0 };

    return general_left_binary(ss, result, syms, enums, unary,
        "need number after ^");
}       

/*
 *  prod:
 *      prod * power
 *      prod / power
 *      prod MOD power
 *      prod \ power
 *      prod
 */
int prod(char **ss, lego **result)
{
    char *syms[] = { "*", "/", "mod", "\\", NULL };
    int enums[] = { wMUL, wDIV, wMOD, wIDIV, 0 };

    return general_left_binary(ss, result, syms, enums, power,
        "need number after *, /, MOD, or \\");
}       

/*
 *  sum:
 *      sum + prod
 *      sum - prod
 *      prod
 */
int sum(char **ss, lego **result)
{
    char *syms[] = { "+", "-", NULL };
    int enums[] = { wADD, wSUB, 0 };

    return general_left_binary(ss, result, syms, enums, prod,
        "need number after + or -");
}

/*
 *  inequality:
 *      inequality > sum
 *      inequality >= sum
 *      inequality < sum
 *      inequality <= sum
 */
int inequality(char **ss, lego **result)
{
    char *syms[] = { ">=", ">", "<>", "<=", "<", NULL };
    int enums[] = { wGE, wGT, wKLUDGE, wLE, wLT, 0 };

    return general_left_binary(ss, result, syms, enums, sum,
        "need number after <, >, <=, or >=");
}

/*
 *  equality:
 *      equality = inequality
 *      equality <> inequality
 */
int equality(char **ss, lego **result)
{
    char *syms[] = { "=", "<>", NULL };
    int enums[] = { wEQ, wNE, 0 };

    return general_left_binary(ss, result, syms, enums, inequality,
        "need number after = or <>");
}

/*
 *  logic_not:
 *      NOT equality
 *      equality
 */
int logic_not(char **ss, lego **result)
{
    char *s = *ss;
    int neg = 0, must = 0;
    lego *l;

    while (keyword(&s, "not"))
        neg = !neg, must = 1;

    if (!equality(&s, result)) {
        if (must)
            warn("need something after NOT");
        return 0;
    }

    if (neg) {
        l = newLego(wNOT);
        l -> a[0] = *result;
        *result = l;
    }

    *ss = s;
    return 1;
}

/*
 *  logic_and:
 *      logic_and AND equality
 *      logic_and NAND equality
 *      equality
 */
int logic_and(char **ss, lego **result)
{
    char *syms[] = { "and", "nand", NULL };
    int enums[] = { wAND, wNAND, 0 };

    return general_left_binary(ss, result, syms, enums, logic_not,
        "need something after AND or NAND");
}

/*
 *  logic_xor:
 *      logic_xor XOR logic_and
 *      logic_and
 */
int logic_xor(char **ss, lego **result)
{
    char *syms[] = { "xor", NULL };
    int enums[] = { wXOR, 0 };

    return general_left_binary(ss, result, syms, enums, logic_and,
        "need something after XOR");
}

/*
 *  logic_or:
 *      logic_or OR logic_xor
 *      logic_or NOR logic_xor
 *      logic_xor
 */
int logic_or(char **ss, lego **result)
{
    char *syms[] = { "or", "nor", NULL };
    int enums[] = { wOR, wNOR, 0 };

    return general_left_binary(ss, result, syms, enums, logic_xor,
        "need something after OR or NOR");
}

/*
 *  logic_eqv:
 *      logic_eqv EQV logic_or
 *      logic_or
 */
int logic_eqv(char **ss, lego **result)
{
    char *syms[] = { "eqv", NULL };
    int enums[] = { wEQV, 0 };

    return general_left_binary(ss, result, syms, enums, logic_or,
        "need something after EQV");
}

/*
 *  logic_imp:
 *      logic_imp IMP logic_eqv
 *      logic_eqv
 */
int logic_imp(char **ss, lego **result)
{
    char *syms[] = { "imp", NULL };
    int enums[] = { wIMP, 0 };

    return general_left_binary(ss, result, syms, enums, logic_eqv,
        "need something after IMP");
}

/**************************** PROGRAM STRUCTURE *******************************/

/*
 *  num_exp:
 *      logic_imp
 */
int num_exp(char **ss, lego **result)
{
    return logic_imp(ss, result);
}

/*
 *  mixed_exp:
 *      num_exp
 *      str_exp
 */
int mixed_exp(char **ss, lego **result)
{
    if (num_exp(ss, result))
        return 1;
    if (str_exp(ss, result))
        return 1;
    return 0;
}

/*
 *  mixed_var:
 *      str_var
 *      num_var
 */
int mixed_var(char **ss, lego **result)
{
    if (num_var(ss, result))
        return 1;
    if (str_var(ss, result))
        return 1;
    return 0;
}

/*
 *  line_list:
 *      line_list , line_num
 *      line_num
 */
int line_list(char **ss, lego **result)
{
    char *seps[] = { ",", NULL };

    return general_list_factory(
        ss, result, seps, line_num, "need line number after ,");
}

/*
 *  line_range:
 *      line_num -
 *      line_num
 *      - line_num
 *      -
 *
 *  Returned as a two-lego list in all four cases.
 *  Unspecified values are expressed as -1, not infinity.
 */
int line_range(char **ss, lego **result)
{
    char *s = *ss;
    lego *head = NULL, *tail = NULL;
    double default_tail = -1;

    line_num(&s, &head);
    if (symbol(&s, "-")) {
        line_num(&s, &tail);
        if (!head) {
            head = newLego(wLINENUM);
            head -> n = -1;
        }
    }
    else {
        if (!head)
            return 0;
        default_tail = head -> n;
    }

    if (!tail) {
        tail = newLego(wLINENUM);
        tail -> n = default_tail;
    }
    head -> next = tail;
    *ss = s;
    *result = head;
    return 1;
}

/*
 *  var_list:
 *      var_list , mixed_var
 *      mixed_var
 */
int var_list(char **ss, lego **result)
{
    char *seps[] = { ",", NULL };

    return general_list_factory(
        ss, result, seps, mixed_var, "need variable after ,");
}

/*
 *  exp_list:
 *      exp_list , mixed_exp
 *      mixed_exp
 */
int exp_list(char **ss, lego **result)
{
    char *seps[] = { ",", NULL };

    return general_list_factory(
        ss, result, seps, mixed_exp, "need expression after ,");
}

/*
 *  print_list:
 *      print_list ; mixed_exp
 *      print_list , mixed_exp
 *      mixed_exp
 */
int print_list(char **ss, lego **result)
{
    char *seps[] = { ",", ";", NULL };

    return general_list_factory(
        ss, result, seps, mixed_exp, NULL);
}

/*
 *  trivial_st:
 *      NEW
 *      END
 *      STOP
 *      CONT
 *      RETURN
 *      CLS
 */
int trivial_st(char **ss, lego **result)
{
    int enums[] = { wNEW, wEND, wSTOP, wCONT, wRETURN, wCLS, 0 };

    return general_keyword_factory(ss, result, enums);
}

/*
 *  line_range_st:
 *      LIST [line_range]
 *      DEL [line_range]
 *
 *  If the range is omitted, a default range list is added for convenience.
 */
int line_range_st(char **ss, lego **result)
{
    int enums[] = { wLIST, wDEL, 0 };
    lego *range;

    if (!general_keyword_factory(ss, result, enums))
        return 0;

    if (!line_range(ss, &range)) {
        range = newLego(wLINENUM);
        range -> next = newLego(wLINENUM);
        range -> next -> n = range -> n = -1;
    }

    (*result) -> a[0] = range;
    return 1;
}

/*
 *  line_num_st:
 *      GOSUB line_num
 *      GOTO line_num
 *      RUN [line_num]
 *      RESTORE [line_num]
 */
int line_num_st(char **ss, lego **result)
{
    char *s = *ss;
    int enums[] = { wGOSUB, wGOTO, wRUN, wRESTORE, 0 };
    lego *l, *ln = NULL;

    if (!general_keyword_factory(&s, &l, enums))
        return 0;
    if (!line_num(&s, &ln) && (l -> what == wGOTO || l -> what == wGOSUB)) {
        warn("need line number after GOTO or GOSUB");
        byeLego(l);
        return 0;
    }
    l -> a[0] = ln;
    *ss = s;
    *result = l;
    return 1;
}

/*
 *  line_list_st:
 *      ON num_exp GOTO line_list
 *      ON num_exp GOSUB line_list
 */
int line_list_st(char **ss, lego **result)
{
    char *s = *ss;
    lego *res = NULL, *expr = NULL, *lines = NULL;

    if (!keyword(&s, "on"))
        goto N;
    if (!num_exp(&s, &expr)) {
        warn("need numeric expression after ON");
        goto N;
    }

    if (keyword(&s, "goto"))
        res = newLego(wONGOTO);
    else if (keyword(&s, "gosub"))
        res = newLego(wONGOSUB);
    else
        goto N;     /* no warning because might be ON ... ALTER */

    if (!line_list(&s, &lines)) {
        warn("need list of lines after ON ... GOTO or ON ... GOSUB");
        goto N;
    }

    res -> a[0] = expr;
    res -> a[1] = lines;
    *result = res;
    *ss = s;
    return 1;

N:  byeLego(res);
    byeLego(expr);
    byeLego(lines);
    return 0;
}

/*
 *  rem_st:
 *      REM flush_input_line
 *      ' flush_input_line
 */
int rem_st(char **ss, lego **result)
{
    int abbrev;
    char *end;

    if (symbol(ss, "'"))
        abbrev = 1;
    else if (keyword(ss, "rem"))
        abbrev = 0;
    else
        return 0;

    eatBlanks(ss);
    for (end = *ss + strlen(*ss); end > *ss && isspace(end[-1]); --end);
    *result = newLego(wREM);
    (*result) -> abbrev = abbrev;
    (*result) -> s = copySubstring(*ss, end);
    *ss = end;
    eatBlanks(ss);
    return 1;
}

/*
 *  for_st:
 *      FOR num_var = num_exp TO num_exp [STEP num_exp]
 */
int for_st(char **ss, lego **result)
{
    char *s = *ss;
    lego *a0 = NULL, *a1 = NULL, *a2 = NULL, *a3 = NULL, *res = NULL;

    if (!keyword(&s, "for"))
        return 0;
    if (!num_var(&s, &a0)) {
        warn("need numeric variable after FOR");
        goto N;
    }
    if (!symbol(&s, "=")) {
        warn("need = after FOR ...");
        goto N;
    }
    if (!num_exp(&s, &a1)) {
        warn("need number after =");
        goto N;
    }
    if (!keyword(&s, "to")) {
        warn("need TO after first number");
        goto N;
    }
    if (!num_exp(&s, &a2)) {
        warn("need number after TO");
        goto N;
    }

    if (keyword(&s, "step")) {
        if (!num_exp(&s, &a3)) {
            warn("need number after STEP");
            goto N;
        }
    }

    res = newLego(wFOR);
    res -> a[0] = a0, res -> a[1] = a1, res -> a[2] = a2, res -> a[3] = a3;
    *result = res;
    *ss = s;
    return 1;

N:  byeLego(a0); byeLego(a1); byeLego(a2); byeLego(a3);
    return 0;
}

/*
 *  next_st:
 *      NEXT [num_var]
 */
int next_st(char **ss, lego **result)
{
    char *s = *ss;
    lego *var = NULL, *res = NULL;

    if (!keyword(&s, "next"))
        return 0;
    num_var(&s, &var);
    res = newLego(wNEXT);
    res -> a[0] = var;
    *result = res;
    *ss = s;
    return 1;
}

/*
 *  if_st:
 *      IF num_var THEN statements [ELSE statements]
 */
int if_st(char **ss, lego **result)
{
    char *s = *ss;
    lego *a0 = NULL, *a1 = NULL, *a2 = NULL, *res = NULL;

    if (!keyword(&s, "if"))
        return 0;
    if (!num_exp(&s, &a0)) {
        warn("need numeric expression after IF");
        goto N;
    }
    if (!keyword(&s, "then")) {
        warn("need THEN after IF ...");
        goto N;
    }
    if (!statements(&s, &a1)) {
        warn("need statement after THEN");
        goto N;
    }

    if (keyword(&s, "else")) {
        if (!statements(&s, &a2)) {
            warn("need statements after ELSE");
            goto N;
        }
    }

    res = newLego(wIF);
    res -> a[0] = a0, res -> a[1] = a1, res -> a[2] = a2;
    *result = res;
    *ss = s;
    return 1;

N:  byeLego(a0); byeLego(a1); byeLego(a2);
    return 0;
}

/*
 *  read_data_st:
 *      READ var_list
 *      DATA exp_list
 */
int read_data_st(char **ss, lego **result)
{
    char *s = *ss;
    lego *res, *list;
    int enums[] = { wREAD, wDATA, 0 };

    if (!general_keyword_factory(&s, &res, enums))
        return 0;

    if (res -> what == wREAD) {
        if (!var_list(&s, &list)) {
            warn("need list of variables to READ");
            goto N;
        }
    }
    else if (!exp_list(&s, &list)) {
        warn("need list of expressions for DATA");
        goto N;
    }

    res -> a[0] = list;
    *result = res;
    *ss = s;
    return 1;

N:  byeLego(res);
    return 0;
}

/*
 *  let_st:
 *      LET str_var = str_exp
 *      LET num_var = num_exp
 *      str_var = str_exp
 *      num_var = num_exp
 */
int let_st(char **ss, lego **result)
{
    char *s = *ss;
    int abbrev = 1;
    lego *var = NULL, *exp = NULL, *res;

    if (keyword(&s, "let"))
        abbrev = 0;

    if (num_var(&s, &var))
        ;
    else if (str_var(&s, &var))
        ;
    else {
        if (!abbrev)
            warn("need variable after LET");
        return 0;
    }

    if (!symbol(&s, "=")) {
        warn("need = after LET variable");
        goto N;
    }

    if (!(var -> what == wNUMVAR ? num_exp : str_exp)(&s, &exp)) {
        warn("need same-type expression after LET ... =");
        goto N;
    }

    res = newLego(wLET);
    res -> abbrev = abbrev;
    res -> a[0] = var, res -> a[1] = exp;
    *result = res;
    *ss = s;
    return 1;

N:  byeLego(var);
    byeLego(exp);
    return 0;
}

/*
 *  print_st:
 *      PRINT [print_list]
 *      ? [print_list]
 */
int print_st(char **ss, lego **result)
{
    char *s = *ss;
    int abbrev;
    lego *res, *list = NULL;

    if (symbol(&s, "?"))
        abbrev = 1;
    else if (keyword(&s, "print"))
        abbrev = 0;
    else
        return 0;

    print_list(&s, &list);
    res = newLego(wPRINT);
    res -> abbrev = abbrev;
    res -> a[0] = list;
    *result = res;
    *ss = s;
    return 1;
}

/*
 *  input_st:
 *      INPUT str_exp ; var_list
 *      INPUT var_list
 */
int input_st(char **ss, lego **result)
{
    char *s = *ss, *undo;
    lego *res, *prompt = NULL, *list = NULL;

    if (!keyword(&s, "input"))
        return 0;

    undo = s;
    if (str_exp(&s, &prompt)) {
        if (!symbol(&s, ";")) {
            byeLego(prompt);
            prompt = NULL;
            s = undo;
        }
    }

    if (!var_list(&s, &list)) {
        warn("need INPUT variables");
        goto N;
    }

    res = newLego(wINPUT);
    res -> a[0] = prompt;
    res -> a[1] = list;
    *result = res;
    *ss = s;
    return 1;

N:  byeLego(prompt);
    byeLego(list);
    return 0;
}

/*
 *  line_in_st:
 *      LINE INPUT str_var
 */
int line_in_st(char **ss, lego **result)
{
    char *s = *ss;
    lego *var;

    if (!keyword(&s, "line") || !keyword(&s, "input") || !str_var(&s, &var))
        return 0;
    *ss = s;
    *result = newLego(wLINEINPUT);
    (*result) -> a[0] = var;
    return 1;
}

/*
 *  alter_st:
 *      ALTER line_num TO [PROCEED TO] line_num
 */
int alter_st(char **ss, lego **result)
{
    char *s = *ss;
    lego *vi = NULL, *de = NULL;
    int abbrev = 1;

    if (!keyword(&s, "alter"))
        goto N;

    if (!line_num(&s, &vi)) {
        warn("need line number after ALTER");
        goto N;
    }

    if (   !keyword(&s, "to")
         || keyword(&s, "proceed") && abbrev-- && !keyword(&s, "to") ) {
        warn("need TO [PROCEED TO] after line number");
        goto N;
    }

    if (!line_num(&s, &de)) {
        warn("need line number after TO [PROCEED TO]");
        goto N;
    }

    *result = newLego(wALTER);
    (*result) -> a[0] = vi;
    (*result) -> a[1] = de;
    (*result) -> abbrev = abbrev;
    *ss = s;
    return 1;

N:
    byeLego(vi);
    byeLego(de);
    return 0;
}

/*
 *  on_alter_st:
 *      ON num_exp ALTER line_num TO [PROCEED TO] line_num
 */
int on_alter_st(char **ss, lego **result)
{
    char *s = *ss;
    lego *res = NULL, *expr = NULL, *vi = NULL, *lines = NULL;
    int abbrev = 1;

    if (!keyword(&s, "on"))
        goto N;

    if (!num_exp(&s, &expr)) {
        warn("need numeric expression after ON");
        goto N;
    }

    if (!keyword(&s, "alter")) {
        warn("need ALTER, GOSUB, or GOTO after ON ...");
        goto N;
    }

    if (!line_num(&s, &vi)) {
        warn("need line number after ALTER");
        goto N;
    }

    if (   !keyword(&s, "to")
         || keyword(&s, "proceed") && abbrev-- && !keyword(&s, "to") ) {
        warn("need TO [PROCEED TO] after line number");
        goto N;
    }

    if (!line_list(&s, &lines)) {
        warn("need list of lines after TO [PROCEED TO]");
        goto N;
    }

    res = newLego(wONALTER);
    res -> a[0] = expr;
    res -> a[1] = vi;
    res -> a[2] = lines;
    res -> abbrev = abbrev;
    *result = res;
    *ss = s;
    return 1;

N:  byeLego(res);
    byeLego(expr);
    byeLego(vi);
    byeLego(lines);
    return 0;
}

/*
 *  statement:
 *      trivial_st
 *      line_range_st
 *      line_num_st
 *      line_list_s
 *      rem_st
 *      for_st
 *      next_st
 *      if_st
 *      read_data_st
 *      print_st
 *      input_st
 *      line_in_st
 *      alter_st
 *      on_alter_st
 *      let_st
 */
int statement(char **ss, lego **result)
{
    int (**f)(char **, lego **), (*fn[])(char **, lego **) = {
        trivial_st,    line_range_st,  line_num_st,  line_list_st, 
        rem_st,        for_st,         next_st,      if_st,
        read_data_st,  print_st,       input_st,     line_in_st,
        alter_st,      on_alter_st,    let_st,       NULL
    };

    for (f=fn; *f; f++)
        if ((*f)(ss, result)) return 1;
    return 0;
}

/*
 *  statements:
 *      statements : statement
 *      statement
 */
int statements(char **ss, lego **result)
{
    char *seps[] = { ":", NULL };

    return general_list_factory(
        ss, result, seps, statement, "need statement after :");
}

/*
 *  command:
 *      line_num statements
 *      statements
 *      line_num
 */
int command(char **ss, lego **result)
{
    char *s = *ss;
    lego *ln = NULL, *sts = NULL;

    line_num(&s, &ln);
    statements(&s, &sts);

    if (ln) {
        ln -> what = wNUMBEREDLINE;
        ln -> a[0] = sts;
        *result = ln;
        *ss = s;
        return 1;
    }

    if (!sts) return 0;

    *result = sts;
    *ss = s;
    return 1;
}

/*
 *  command_line:
 *      command eol
 *      eol
 */
int command_line(char **ss, lego **result)
{
    char *s = *ss;
    lego *res = NULL;

    command(&s, &res);
    if (!nothingMore(&s)) {
        if (res) {
            warn("ignoring command line with extra input at end");
            byeLego(res);
        }
        else
            warn("unknown command");
        return 0;
    }

    if (!res)
        return 0;

    *result = res;
    *ss = s;
    return 1;
}
