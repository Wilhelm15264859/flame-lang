#include "fl_Exception.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Exception exceptions[MAX_EXCEPTIONS];
int       exception_count = 0;

static int find_closing_brace(vector_token *tokens, int start) {
    int depth = 1;
    for (int i = start; (unsigned)i < tokens->size; i++) {
        if (strcmp(tokens->data[i].value, "{") == 0) depth++;
        if (strcmp(tokens->data[i].value, "}") == 0) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

static PatternTokenType spec_to_pat(const char *s) {
    if (strcmp(s, "n") == 0) return PAT_NUMBER;
    if (strcmp(s, "i") == 0) return PAT_IDENT;
    if (strcmp(s, "k") == 0) return PAT_KEYWORD;
    if (strcmp(s, "s") == 0) return PAT_STRING;
    return PAT_LITERAL;
}

static TokenType tok_type_from_name(const char *name) {
    if (strcmp(name, "TOK_INT")       == 0) return TOK_INT;
    if (strcmp(name, "TOK_FLOAT")     == 0) return TOK_FLOAT;
    if (strcmp(name, "TOK_STRING")    == 0) return TOK_STRING;
    if (strcmp(name, "TOK_IDENT")     == 0) return TOK_IDENT;
    if (strcmp(name, "TOK_KEYWORD")   == 0) return TOK_KEYWORD;
    if (strcmp(name, "TOK_OP")        == 0) return TOK_OP;
    if (strcmp(name, "TOK_PAREN")     == 0) return TOK_PAREN;
    if (strcmp(name, "TOK_SEMICOLON") == 0) return TOK_SEMICOLON;
    if (strcmp(name, "TOK_COMMA")     == 0) return TOK_COMMA;
    if (strcmp(name, "TOK_TYPE")      == 0) return TOK_TYPE;
    printf("Warning: unknown token type name '%s'\n", name);
    return TOK_IDENT;
}

static int parse_pattern(Token *toks, int len, PatternToken *out) {
    int out_len = 0;
    for (int i = 0; i < len && out_len < MAX_PATTERN_TOKENS; i++) {
        Token *t = &toks[i];

        if (t->type == TOK_OP && strcmp(t->value, "%") == 0) {
            i++;
            if (i >= len) break;
            Token *spec = &toks[i];

            PatternTokenType left_type;
            char             left_val[64] = "";
            int              left_is_str  = 0;

            if (spec->type == TOK_STRING) {
                left_is_str = 1;
                left_type   = PAT_LITERAL;
                strncpy(left_val, spec->value, 63);
            } else {
                left_type = spec_to_pat(spec->value);
                if (left_type == PAT_LITERAL)
                    printf("Warning: unknown pattern specifier '%%%s'\n", spec->value);
            }

            int has_caret = (i + 1 < len && strcmp(toks[i + 1].value, "^") == 0);
            int has_colon = (i + 1 < len && strcmp(toks[i + 1].value, ":") == 0);

            if (has_caret) {
                i += 2;
                if (i >= len) break;
                Token *until_spec = &toks[i];

                PatternTokenType until_type;
                char             until_val[64] = "";

                if (until_spec->type == TOK_STRING) {
                    until_type = PAT_LITERAL;
                    strncpy(until_val, until_spec->value, 63);
                } else {
                    until_type = spec_to_pat(until_spec->value);
                }

                PatternTokenType stop_type  = PAT_LITERAL;
                char             stop_val[64] = "";
                int              has_stop   = 0;

                if (i + 1 < len && strcmp(toks[i + 1].value, "!") == 0) {
                    i += 2;
                    if (i >= len) break;
                    Token *stop_spec = &toks[i];
                    has_stop = 1;
                    if (stop_spec->type == TOK_STRING) {
                        stop_type = PAT_LITERAL;
                        strncpy(stop_val, stop_spec->value, 63);
                    } else {
                        stop_type = spec_to_pat(stop_spec->value);
                    }
                }

                if (i + 2 < len && strcmp(toks[i + 1].value, ":") == 0) {
                    i += 2;
                    PatternToken pt;
                    memset(&pt, 0, sizeof(pt));
                    pt.kind         = PAT_CAPTURE_UNTIL;
                    pt.capture_type = left_type;
                    pt.until_type   = until_type;
                    pt.has_stop     = has_stop;
                    pt.stop_type    = stop_type;
                    strncpy(pt.value,       toks[i].value, 63);
                    strncpy(pt.until_value, until_val,     63);
                    strncpy(pt.stop_value,  stop_val,      63);

                    if (left_is_str) {
                        PatternToken lit;
                        memset(&lit, 0, sizeof(lit));
                        lit.kind         = PAT_LITERAL;
                        lit.capture_type = PAT_LITERAL;
                        strncpy(lit.value, left_val, 63);
                        out[out_len++] = lit;
                        pt.capture_type = PAT_LITERAL;
                    }
                    out[out_len++] = pt;
                } else {
                    printf("Warning: expected ':varName' after '^' in pattern\n");
                }
            } else if (has_colon && !left_is_str) {
                i += 2;
                PatternToken pt;
                memset(&pt, 0, sizeof(pt));
                pt.kind         = PAT_CAPTURE;
                pt.capture_type = left_type;
                strncpy(pt.value, toks[i].value, 63);
                out[out_len++] = pt;
            } else if (left_is_str) {
                PatternToken pt;
                memset(&pt, 0, sizeof(pt));
                pt.kind         = PAT_LITERAL;
                pt.capture_type = PAT_LITERAL;
                strncpy(pt.value, left_val, 63);
                out[out_len++] = pt;
            } else {
                PatternToken pt;
                memset(&pt, 0, sizeof(pt));
                pt.kind         = left_type;
                pt.capture_type = left_type;
                out[out_len++] = pt;
            }
        } else {
            PatternToken pt;
            memset(&pt, 0, sizeof(pt));
            pt.kind         = PAT_LITERAL;
            pt.capture_type = PAT_LITERAL;
            strncpy(pt.value, t->value, 63);
            out[out_len++] = pt;
        }
    }
    return out_len;
}

vector_token *extract_exceptions(vector_token *tokens) {
    vector_token *result = malloc(sizeof(vector_token));
    if (!result) {
        fprintf(stderr, "Error: out of memory in extract_exceptions\n");
        return NULL;
    }
    vt_init(result, tokens->size);

    unsigned i = 0;
    while (i < tokens->size) {
        if (tokens->data[i].type == TOK_KEYWORD &&
            strcmp(tokens->data[i].value, "exception") == 0)
        {
            i++;
            if (i >= tokens->size) break;

            if (exception_count >= MAX_EXCEPTIONS) {
                fprintf(stderr, "Error: too many exceptions\n");
                break;
            }
            int saved_exception_count = exception_count;
            Exception *ex = &exceptions[exception_count++];
            memset(ex, 0, sizeof(Exception));
            strncpy(ex->name, tokens->data[i].value, 63);
            ex->name[63] = '\0';
            i++;

            if (i >= tokens->size || strcmp(tokens->data[i].value, "{") != 0) {
                fprintf(stderr, "Error: expected '{' after exception name '%s'\n",
                        ex->name);
                exception_count = saved_exception_count;
                break;
            }
            i++;  /* пропускаем '{' */

            int parse_error = 0;
            while (i < tokens->size && strcmp(tokens->data[i].value, "}") != 0) {
                /* Поле: TYPE name ;
                 * Первый токен — тип (TOK_TYPE или TOK_IDENT для struct-типов),
                 * второй — имя (TOK_IDENT), третий — точка с запятой. */
                if ((tokens->data[i].type == TOK_TYPE ||
                     tokens->data[i].type == TOK_IDENT) &&
                    i + 2 < tokens->size &&
                    tokens->data[i + 1].type == TOK_IDENT &&
                    tokens->data[i + 2].type == TOK_SEMICOLON)
                {
                    if (ex->field_count + 2 <= MAX_FIELDS) {
                        ex->fields[ex->field_count++] = tokens->data[i];     /* тип  */
                        ex->fields[ex->field_count++] = tokens->data[i + 1]; /* имя  */
                    } else {
                        fprintf(stderr, "Error: exception '%s' field table overflow\n",
                                ex->name);
                    }
                    i += 3; /* пропускаем тип, имя и ';' */
                }
                else if (tokens->data[i].type == TOK_KEYWORD &&
                         strcmp(tokens->data[i].value, "instruction") == 0)
                {
                    i++;
                    if (i >= tokens->size || strcmp(tokens->data[i].value, "{") != 0) {
                        fprintf(stderr, "Error: expected '{' after instruction in '%s'\n",
                                ex->name);
                        parse_error = 1; break;
                    }
                    i++;

                    Token pat_toks[MAX_PATTERN_TOKENS];
                    int   pat_len = 0;
                    while (i < tokens->size && strcmp(tokens->data[i].value, "}") != 0) {
                        if (pat_len < MAX_PATTERN_TOKENS)
                            pat_toks[pat_len++] = tokens->data[i];
                        i++;
                    }
                    if (i >= tokens->size) {
                        fprintf(stderr, "Error: unclosed instruction block in '%s'\n",
                                ex->name);
                        parse_error = 1; break;
                    }
                    i++;

                    ex->pattern_len = parse_pattern(pat_toks, pat_len, ex->pattern);
                }
                else if (tokens->data[i].type == TOK_KEYWORD &&
                         strcmp(tokens->data[i].value, "check") == 0)
                {
                    i++;
                    if (i >= tokens->size || strcmp(tokens->data[i].value, "{") != 0) {
                        fprintf(stderr, "Error: expected '{' after check in '%s'\n",
                                ex->name);
                        parse_error = 1; break;
                    }
                    int close = find_closing_brace(tokens, (int)i + 1);
                    if (close < 0) {
                        fprintf(stderr, "Error: unclosed check block in '%s'\n", ex->name);
                        parse_error = 1; break;
                    }
                    i++;
                    while ((int)i < close) {
                        if (ex->checker_len < MAX_CHECKER_TOKENS)
                            ex->checker[ex->checker_len++] = tokens->data[i];
                        i++;
                    }
                    i++;
                }
                else if (tokens->data[i].type == TOK_KEYWORD &&
                        strcmp(tokens->data[i].value, "replace") == 0)
                {
                    i++;
                    if (i >= tokens->size || strcmp(tokens->data[i].value, "{") != 0) {
                        fprintf(stderr, "Error: expected '{' after replace in '%s'\n",
                                ex->name);
                        parse_error = 1; break;
                    }
                    int close = find_closing_brace(tokens, (int)i + 1);
                    if (close < 0) {
                        fprintf(stderr, "Error: unclosed replace block in '%s'\n", ex->name);
                        parse_error = 1; break;
                    }
                    i++;
                    while ((int)i < close) {
                        if (ex->replace_len < MAX_CHECKER_TOKENS)
                            ex->replace[ex->replace_len++] = tokens->data[i];
                        i++;
                    }
                    i++;
                    ex->has_replace = 1;
                }
                else { i++; }
            }
            i++; /* пропускаем закрывающую '}' исключения */

            if (parse_error) {
                exception_count = saved_exception_count;
                continue;
            }

            if (ex->checker_len > 0 && ex->has_replace) {
                fprintf(stderr, "Error: exception '%s' cannot have both 'check' and 'replace'\n",
                        ex->name);
                exception_count = saved_exception_count;
            } else if (ex->checker_len == 0 && !ex->has_replace) {
                fprintf(stderr, "Warning: exception '%s' has neither 'check' nor 'replace'\n",
                        ex->name);
            }
        }
        else {
            vt_push_back(result, tokens->data[i]);
            i++;
        }
    }

    return result;
}

static int try_match(vector_token *tokens, int pos, Exception *ex,
                     char    captures[MAX_PATTERN_TOKENS][64],
                     TokenType capture_types[MAX_PATTERN_TOKENS])
{
    int ti = pos;
    for (int pi = 0; pi < ex->pattern_len; pi++) {
        if ((unsigned)ti >= tokens->size) return -1;

        PatternToken *pt = &ex->pattern[pi];
        Token        *t  = &tokens->data[ti];

        switch (pt->kind) {
            case PAT_LITERAL:
                if (strcmp(t->value, pt->value) != 0) return -1;
                break;
            case PAT_IDENT:
                if (t->type != TOK_IDENT) return -1;
                break;
            case PAT_KEYWORD:
                if (t->type != TOK_KEYWORD) return -1;
                break;
            case PAT_NUMBER:
                if (t->type != TOK_INT && t->type != TOK_FLOAT) return -1;
                break;
            case PAT_STRING:
                if (t->type != TOK_STRING) return -1;
                break;
            case PAT_CAPTURE_UNTIL: {
                if (pt->capture_type == PAT_NUMBER &&
                    t->type != TOK_INT && t->type != TOK_FLOAT) return -1;
                if (pt->capture_type == PAT_IDENT   && t->type != TOK_IDENT)   return -1;
                if (pt->capture_type == PAT_KEYWORD  && t->type != TOK_KEYWORD) return -1;
                if (pt->capture_type == PAT_STRING   && t->type != TOK_STRING)  return -1;

                char buf[512] = "";
                int  found    = 0;
                while ((unsigned)ti < tokens->size) {
                    Token *cur = &tokens->data[ti];

                    int is_until = 0;
                    if (pt->until_type == PAT_LITERAL)
                        is_until = (strcmp(cur->value, pt->until_value) == 0);
                    else if (pt->until_type == PAT_NUMBER)
                        is_until = (cur->type == TOK_INT || cur->type == TOK_FLOAT);
                    else if (pt->until_type == PAT_IDENT)
                        is_until = (cur->type == TOK_IDENT);
                    else if (pt->until_type == PAT_KEYWORD)
                        is_until = (cur->type == TOK_KEYWORD);
                    else if (pt->until_type == PAT_STRING)
                        is_until = (cur->type == TOK_STRING);

                    if (is_until) { found = 1; break; }

                    if (pt->has_stop) {
                        int is_stop = 0;
                        if (pt->stop_type == PAT_LITERAL)
                            is_stop = (strcmp(cur->value, pt->stop_value) == 0);
                        else if (pt->stop_type == PAT_NUMBER)
                            is_stop = (cur->type == TOK_INT || cur->type == TOK_FLOAT);
                        else if (pt->stop_type == PAT_IDENT)
                            is_stop = (cur->type == TOK_IDENT);
                        else if (pt->stop_type == PAT_KEYWORD)
                            is_stop = (cur->type == TOK_KEYWORD);
                        else if (pt->stop_type == PAT_STRING)
                            is_stop = (cur->type == TOK_STRING);
                        if (is_stop) { found = 1; break; }
                    }

                    if (buf[0] != '\0')
                        strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
                    strncat(buf, cur->value, sizeof(buf) - strlen(buf) - 1);
                    ti++;
                }
                if (!found) return -1;

                strncpy(captures[pi], buf, 63);
                capture_types[pi] = TOK_IDENT;
                continue;
            }
            case PAT_CAPTURE: {
                int type_ok = 1;
                switch (pt->capture_type) {
                    case PAT_NUMBER:  type_ok = (t->type == TOK_INT || t->type == TOK_FLOAT); break;
                    case PAT_IDENT:   type_ok = (t->type == TOK_IDENT);   break;
                    case PAT_KEYWORD: type_ok = (t->type == TOK_KEYWORD); break;
                    case PAT_STRING:  type_ok = (t->type == TOK_STRING);  break;
                    default:          type_ok = 1; break;
                }
                if (!type_ok) return -1;
                strncpy(captures[pi], t->value, 63);
                capture_types[pi] = t->type;
                break;
            }
        }
        ti++;
    }
    return ti - pos;
}

static int g_match_counter = 0;

static int capture_has_field(Exception *ex, const char *cap_name,
                              char *field_type_out)
{
    /* Поля хранятся парами: [тип][имя][тип][имя]...
     * Итерируем по парам — шаг 2. */
    for (int fi = 0; fi + 1 < ex->field_count; fi += 2) {
        if (strcmp(ex->fields[fi + 1].value, cap_name) == 0) {
            if (field_type_out)
                strncpy(field_type_out, ex->fields[fi].value, 63);
            return 1;
        }
    }
    return 0;
}

static void build_capture_inits(Exception *ex,
                                 char      captures[MAX_PATTERN_TOKENS][64],
                                 TokenType capture_types[MAX_PATTERN_TOKENS],
                                 vector_token *out,
                                 char      unique_names[MAX_PATTERN_TOKENS][64])
{
    for (int pi = 0; pi < ex->pattern_len; pi++) {
        if (ex->pattern[pi].kind != PAT_CAPTURE &&
            ex->pattern[pi].kind != PAT_CAPTURE_UNTIL) continue;

        const char *cap_name = ex->pattern[pi].value;
        const char *cap_val  = captures[pi];
        TokenType   cap_type = capture_types[pi];

        char field_type[64] = "";
        if (!capture_has_field(ex, cap_name, field_type)) {
            strncpy(unique_names[pi], cap_val, 63);
            continue;
        }

        char cap_trunc[49];
        strncpy(cap_trunc, cap_name, 48);
        cap_trunc[48] = '\0';
        snprintf(unique_names[pi], 64, "_ex_%s_%d", cap_trunc, g_match_counter);

        Token t;
        t.line = 0; t.col = 0;
        t.type = TOK_TYPE;      strncpy(t.value, field_type,       63); t.value[63] = '\0'; vt_push_back(out, t);
        t.type = TOK_IDENT;     strncpy(t.value, unique_names[pi], 63); t.value[63] = '\0'; vt_push_back(out, t);
        t.type = TOK_OP;        strncpy(t.value, "=",              63); vt_push_back(out, t);
        t.type = cap_type;      strncpy(t.value, cap_val,          63); t.value[63] = '\0'; vt_push_back(out, t);
        t.type = TOK_SEMICOLON; strncpy(t.value, ";",              63); vt_push_back(out, t);
    }
}

static void rename_captures_in_block(Token *toks, int len,
                                      Exception *ex,
                                      char unique_names[MAX_PATTERN_TOKENS][64])
{
    for (int ti = 0; ti < len; ti++) {
        if (toks[ti].type != TOK_IDENT) continue;
        for (int pi = 0; pi < ex->pattern_len; pi++) {
            if (ex->pattern[pi].kind != PAT_CAPTURE &&
                ex->pattern[pi].kind != PAT_CAPTURE_UNTIL) continue;
            if (strcmp(toks[ti].value, ex->pattern[pi].value) == 0) {
                strncpy(toks[ti].value, unique_names[pi], 63);
                break;
            }
        }
    }
}

vector_token *preparse(vector_token *tokens) {
    vector_token *result = malloc(sizeof(vector_token));
    if (!result) {
        fprintf(stderr, "Error: out of memory in preparse\n");
        return NULL;
    }
    vt_init(result, tokens->size * 2);

    unsigned i = 0;
    int stmt_start = 0;
    while (i < tokens->size) {
        int matched = 0;

        for (int ei = 0; ei < exception_count && !matched; ei++) {
            Exception *ex = &exceptions[ei];
            if (ex->pattern_len == 0) continue;

            char      captures[MAX_PATTERN_TOKENS][64];
            TokenType capture_types[MAX_PATTERN_TOKENS];
            memset(captures,      0, sizeof(captures));
            memset(capture_types, 0, sizeof(capture_types));

            int match_len = try_match(tokens, (int)i, ex, captures, capture_types);
            if (match_len < 0) continue;

            matched = 1;
            g_match_counter++;

            if (ex->checker_len > 0 && ex->has_replace) {
                fprintf(stderr, "Error: exception '%s' has both check and replace, skipping\n",
                        ex->name);
                matched = 0;
                continue;
            }

            char unique_names[MAX_PATTERN_TOKENS][64];
            memset(unique_names, 0, sizeof(unique_names));

            if (ex->has_replace) {
                int inject_pos = stmt_start;

                int    suffix_len = (int)result->size - inject_pos;
                Token *suffix     = NULL;
                if (suffix_len > 0) {
                    suffix = malloc(sizeof(Token) * suffix_len);
                    if (!suffix) { fprintf(stderr, "Error: out of memory in preparse\n"); break; }
                    memcpy(suffix, &result->data[inject_pos], sizeof(Token) * suffix_len);
                    result->size = inject_pos;
                }

                int    source_len = suffix_len + match_len + 1;
                Token *source     = malloc(sizeof(Token) * (source_len > 0 ? source_len : 1));
                if (!source) { free(suffix); fprintf(stderr, "Error: out of memory in preparse\n"); break; }
                int    si2        = 0;
                for (int s2 = 0; s2 < suffix_len; s2++)
                    source[si2++] = suffix[s2];
                /* suffix уже скопирован в source — теперь можно использовать оба независимо */
                for (int m2 = 0; m2 < match_len; m2++)
                    source[si2++] = tokens->data[i + m2];
                source[si2].type = TOK_SEMICOLON;
                strncpy(source[si2].value, ";", 63);
                si2++;

                build_capture_inits(ex, captures, capture_types, result, unique_names);

                Token *replace_copy = malloc(sizeof(Token) * (ex->replace_len > 0 ? ex->replace_len : 1));
                if (!replace_copy) {
                    free(suffix); free(source);
                    fprintf(stderr, "Error: out of memory in preparse\n");
                    break;
                }
                memcpy(replace_copy, ex->replace, sizeof(Token) * ex->replace_len);
                rename_captures_in_block(replace_copy, ex->replace_len, ex, unique_names);

                for (int ci = 0; ci < ex->replace_len; ci++) {
                    Token *rt = &replace_copy[ci];

                    if (rt->type == TOK_OP && strcmp(rt->value, "$") == 0 &&
                        ci + 1 < ex->replace_len &&
                        strcmp(replace_copy[ci + 1].value, "source") == 0)
                    {
                        int next_is_semi = (ci + 2 < ex->replace_len &&
                            replace_copy[ci + 2].type == TOK_SEMICOLON);
                        int emit_len = next_is_semi ? source_len - 1 : source_len;
                        for (int s2 = 0; s2 < emit_len; s2++)
                            vt_push_back(result, source[s2]);
                        ci++;
                    } else if (rt->type == TOK_OP && strcmp(rt->value, "$") == 0 &&
                               ci + 1 < ex->replace_len &&
                               replace_copy[ci + 1].type == TOK_OP &&
                               strcmp(replace_copy[ci + 1].value, "%") == 0 &&
                               ci + 2 < ex->replace_len &&
                               replace_copy[ci + 2].type == TOK_IDENT &&
                               ci + 3 < ex->replace_len &&
                               replace_copy[ci + 3].type == TOK_STRING)
                    {
                        Token synth;
                        synth.type = tok_type_from_name(replace_copy[ci + 2].value);
                        synth.line = 0;
                        synth.col  = 0;
                        strncpy(synth.value, replace_copy[ci + 3].value, 63);
                        synth.value[63] = '\0';
                        vt_push_back(result, synth);
                        ci += 3;
                    } else if (rt->type == TOK_OP && strcmp(rt->value, "$") == 0 &&
                               ci + 1 < ex->replace_len &&
                               replace_copy[ci + 1].type == TOK_OP &&
                               strcmp(replace_copy[ci + 1].value, "-") == 0 &&
                               ci + 2 < ex->replace_len &&
                               replace_copy[ci + 2].type == TOK_INT)
                    {
                        int offset  = -atoi(replace_copy[ci + 2].value);
                        int abs_idx = (int)i + offset;
                        if (abs_idx >= 0 && (unsigned)abs_idx < tokens->size)
                            vt_push_back(result, tokens->data[abs_idx]);
                        else
                            printf("Warning: $-%s out of bounds (pos=%d)\n",
                                   replace_copy[ci + 2].value, (int)i);
                        ci += 2;
                    } else if (rt->type == TOK_OP && strcmp(rt->value, "$") == 0 &&
                               ci + 1 < ex->replace_len &&
                               replace_copy[ci + 1].type == TOK_INT)
                    {
                        int offset  = atoi(replace_copy[ci + 1].value);
                        int abs_idx = (int)i + offset;
                        if (abs_idx >= 0 && (unsigned)abs_idx < tokens->size)
                            vt_push_back(result, tokens->data[abs_idx]);
                        else
                            printf("Warning: $%d out of bounds (pos=%d, tokens=%d)\n",
                                   offset, (int)i, (int)tokens->size);
                        ci++;
                    } else {
                        vt_push_back(result, *rt);
                    }
                }

                free(replace_copy);
                if (suffix) free(suffix);
                free(source);

                i += match_len;
                if (i < tokens->size && tokens->data[i].type == TOK_SEMICOLON)
                    i++;
                stmt_start = (int)result->size;

            } else {
                int inject_pos = stmt_start;

                int    suffix_len = (int)result->size - inject_pos;
                Token *suffix     = NULL;
                if (suffix_len > 0) {
                    suffix = malloc(sizeof(Token) * suffix_len);
                    if (!suffix) { fprintf(stderr, "Error: out of memory in preparse\n"); break; }
                    memcpy(suffix, &result->data[inject_pos], sizeof(Token) * suffix_len);
                    result->size = inject_pos;
                }

                build_capture_inits(ex, captures, capture_types, result, unique_names);

                if (ex->checker_len > 0) {
                    Token *checker_copy = malloc(sizeof(Token) * ex->checker_len);
                    if (!checker_copy) {
                        free(suffix);
                        fprintf(stderr, "Error: out of memory in preparse\n");
                        break;
                    }
                    memcpy(checker_copy, ex->checker, sizeof(Token) * ex->checker_len);
                    rename_captures_in_block(checker_copy, ex->checker_len, ex, unique_names);
                    for (int ci = 0; ci < ex->checker_len; ci++)
                        vt_push_back(result, checker_copy[ci]);
                    free(checker_copy);
                }

                if (suffix_len > 0) {
                    for (int s2 = 0; s2 < suffix_len; s2++)
                        vt_push_back(result, suffix[s2]);
                    free(suffix);
                }

                for (int mi = 0; mi < match_len; mi++)
                    vt_push_back(result, tokens->data[i + mi]);

                i += match_len;
            }
        }

        if (!matched) {
            Token *t = &tokens->data[i];
            vt_push_back(result, *t);
            if (t->type == TOK_SEMICOLON ||
                strcmp(t->value, "{") == 0 ||
                strcmp(t->value, "}") == 0)
            {
                stmt_start = (int)result->size;
            }
            i++;
        }
    }

    return result;
}