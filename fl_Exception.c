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
                if (left_type == PAT_LITERAL && strcmp(spec->value, "n") != 0 &&
                    strcmp(spec->value, "i") != 0 && strcmp(spec->value, "k") != 0 &&
                    strcmp(spec->value, "s") != 0) {
                    printf("Warning: unknown pattern specifier '%%%s'\n", spec->value);
                }
            }

            int has_caret  = (i + 1 < len && strcmp(toks[i + 1].value, "^") == 0);
            int has_colon  = (i + 1 < len && strcmp(toks[i + 1].value, ":") == 0);

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
                        pt.capture_type = PAT_LITERAL;
                        PatternToken lit;
                        memset(&lit, 0, sizeof(lit));
                        lit.kind         = PAT_LITERAL;
                        lit.capture_type = PAT_LITERAL;
                        strncpy(lit.value, left_val, 63);
                        out[out_len++] = lit;
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
    vt_init(result, tokens->size);

    unsigned i = 0;
    while (i < tokens->size) {
        if (tokens->data[i].type == TOK_KEYWORD &&
            strcmp(tokens->data[i].value, "exception") == 0)
        {
            i++;
            if (i >= tokens->size) break;

            if (exception_count >= MAX_EXCEPTIONS) {
                printf("Error: too many exceptions\n");
                break;
            }
            Exception *ex = &exceptions[exception_count++];
            memset(ex, 0, sizeof(Exception));
            strncpy(ex->name, tokens->data[i].value, 63);
            i++;

            if (i >= tokens->size || strcmp(tokens->data[i].value, "{") != 0) {
                printf("Error: expected '{' after exception name\n");
                break;
            }
            i++;

            while (i < tokens->size && strcmp(tokens->data[i].value, "}") != 0) {

                if (tokens->data[i].type == TOK_KEYWORD &&
                    strcmp(tokens->data[i].value, "var") == 0)
                {
                    while (i < tokens->size &&
                           tokens->data[i].type != TOK_SEMICOLON)
                    {
                        if (ex->field_count < MAX_FIELDS * 8)
                            ex->fields[ex->field_count++] = tokens->data[i];
                        i++;
                    }
                    if (i < tokens->size) {
                        if (ex->field_count < MAX_FIELDS * 8)
                            ex->fields[ex->field_count++] = tokens->data[i];
                        i++;
                    }
                }
                else if (tokens->data[i].type == TOK_KEYWORD &&
                         strcmp(tokens->data[i].value, "instruction") == 0)
                {
                    i++;
                    if (i >= tokens->size || strcmp(tokens->data[i].value, "{") != 0) {
                        printf("Error: expected '{' after instruction\n");
                        break;
                    }
                    i++;

                    Token pat_toks[MAX_PATTERN_TOKENS];
                    int   pat_len = 0;
                    while (i < tokens->size && strcmp(tokens->data[i].value, "}") != 0) {
                        if (pat_len < MAX_PATTERN_TOKENS)
                            pat_toks[pat_len++] = tokens->data[i];
                        i++;
                    }
                    i++;

                    ex->pattern_len = parse_pattern(pat_toks, pat_len, ex->pattern);
                }
                else if (tokens->data[i].type == TOK_KEYWORD &&
                         strcmp(tokens->data[i].value, "check") == 0)
                {
                    i++;
                    if (i >= tokens->size || strcmp(tokens->data[i].value, "{") != 0) {
                        printf("Error: expected '{' after checker\n");
                        break;
                    }
                    int close = find_closing_brace(tokens, (int)i + 1);
                    if (close < 0) {
                        printf("Error: unclosed checker block\n");
                        break;
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
                        printf("Error: expected '{' after replace\n");
                        break;
                    }
                    int close = find_closing_brace(tokens, (int)i + 1);
                    if (close < 0) {
                        printf("Error: unclosed replace block\n");
                        break;
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
                else {
                    i++;
                }
            }
            i++;

            if (ex->checker_len > 0 && ex->has_replace) {
                printf("Error: exception '%s' cannot have both 'check' and 'replace'\n", ex->name);
                exception_count--;
            } else if (ex->checker_len == 0 && !ex->has_replace) {
                printf("Warning: exception '%s' has neither 'check' nor 'replace'\n", ex->name);
            }

            printf("DEBUG extract: registered exception '%s', pattern_len=%d, checker_len=%d, has_replace=%d\n",
                ex->name, ex->pattern_len, ex->checker_len, ex->has_replace);
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
                    if (pt->until_type == PAT_LITERAL) {
                        is_until = (strcmp(cur->value, pt->until_value) == 0);
                    } else if (pt->until_type == PAT_NUMBER) {
                        is_until = (cur->type == TOK_INT || cur->type == TOK_FLOAT);
                    } else if (pt->until_type == PAT_IDENT) {
                        is_until = (cur->type == TOK_IDENT);
                    } else if (pt->until_type == PAT_KEYWORD) {
                        is_until = (cur->type == TOK_KEYWORD);
                    } else if (pt->until_type == PAT_STRING) {
                        is_until = (cur->type == TOK_STRING);
                    }

                    if (is_until) { found = 1; break; }

                    if (pt->has_stop) {
                        int is_stop = 0;
                        if (pt->stop_type == PAT_LITERAL) {
                            is_stop = (strcmp(cur->value, pt->stop_value) == 0);
                        } else if (pt->stop_type == PAT_NUMBER) {
                            is_stop = (cur->type == TOK_INT || cur->type == TOK_FLOAT);
                        } else if (pt->stop_type == PAT_IDENT) {
                            is_stop = (cur->type == TOK_IDENT);
                        } else if (pt->stop_type == PAT_KEYWORD) {
                            is_stop = (cur->type == TOK_KEYWORD);
                        } else if (pt->stop_type == PAT_STRING) {
                            is_stop = (cur->type == TOK_STRING);
                        }
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
                    case PAT_NUMBER:
                        type_ok = (t->type == TOK_INT || t->type == TOK_FLOAT);
                        break;
                    case PAT_IDENT:
                        type_ok = (t->type == TOK_IDENT);
                        break;
                    case PAT_KEYWORD:
                        type_ok = (t->type == TOK_KEYWORD);
                        break;
                    case PAT_STRING:
                        type_ok = (t->type == TOK_STRING);
                        break;
                    default:
                        type_ok = 1;
                        break;
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

static void build_capture_inits(Exception *ex,
                                 char      captures[MAX_PATTERN_TOKENS][64],
                                 TokenType capture_types[MAX_PATTERN_TOKENS],
                                 vector_token *out,
                                 char      unique_names[MAX_PATTERN_TOKENS][64])
{
    for (int pi = 0; pi < ex->pattern_len; pi++) {
        if (ex->pattern[pi].kind != PAT_CAPTURE) continue;

        const char *cap_name = ex->pattern[pi].value;
        const char *cap_val  = captures[pi];
        TokenType   cap_type = capture_types[pi];
        char field_type[64] = "int";
        for (int fi = 0; fi < ex->field_count - 2; fi++) {
            if (ex->fields[fi].type == TOK_KEYWORD &&
                strcmp(ex->fields[fi].value, "var") == 0 &&
                strcmp(ex->fields[fi + 2].value, cap_name) == 0)
            {
                strncpy(field_type, ex->fields[fi + 1].value, 63);
                break;
            }
        }

        char cap_trunc[49];
        strncpy(cap_trunc, cap_name, 48);
        cap_trunc[48] = '\0';
        snprintf(unique_names[pi], 64, "_ex_%s_%d", cap_trunc, g_match_counter);

        printf("DEBUG capture: var %s %s = %s (tok_type=%d) -> %s\n",
               field_type, cap_name, cap_val, cap_type, unique_names[pi]);

        Token t;
        t.type = TOK_KEYWORD;   strncpy(t.value, "var",              63); vt_push_back(out, t);
        t.type = TOK_TYPE;      strncpy(t.value, field_type,         63); vt_push_back(out, t);
        t.type = TOK_IDENT;     strncpy(t.value, unique_names[pi],   63); vt_push_back(out, t);
        t.type = TOK_OP;        strncpy(t.value, "=",                63); vt_push_back(out, t);
        t.type = cap_type;      strncpy(t.value, cap_val,            63); vt_push_back(out, t);
        t.type = TOK_SEMICOLON; strncpy(t.value, ";",                63); vt_push_back(out, t);
    }
}

static void rename_captures_in_block(Token *toks, int len,
                                      Exception *ex,
                                      char unique_names[MAX_PATTERN_TOKENS][64])
{
    for (int ti = 0; ti < len; ti++) {
        if (toks[ti].type != TOK_IDENT) continue;
        for (int pi = 0; pi < ex->pattern_len; pi++) {
            if (ex->pattern[pi].kind != PAT_CAPTURE) continue;
            if (strcmp(toks[ti].value, ex->pattern[pi].value) == 0) {
                strncpy(toks[ti].value, unique_names[pi], 63);
                break;
            }
        }
    }
}

vector_token *preparse(vector_token *tokens) {
    vector_token *result = malloc(sizeof(vector_token));
    vt_init(result, tokens->size * 2);

    printf("DEBUG preparse: total tokens=%d, exceptions=%d\n", (int)tokens->size, exception_count);
    for (unsigned di = 0; di < tokens->size && di < 30; di++)
        printf("  [%d] type=%d value='%s'\n", di, tokens->data[di].type, tokens->data[di].value);

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
            printf("DEBUG preparse: matched exception '%s' at pos %d (match #%d)\n", ex->name, i, g_match_counter);

            if (ex->checker_len > 0 && ex->has_replace) {
                printf("Error: exception '%s' has both check and replace, skipping\n", ex->name);
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
                    memcpy(suffix, &result->data[inject_pos], sizeof(Token) * suffix_len);
                    result->size = inject_pos;
                }

                int    source_len = suffix_len + match_len + 1;
                Token *source     = malloc(sizeof(Token) * source_len);
                int    si2        = 0;
                for (int s2 = 0; s2 < suffix_len; s2++)
                    source[si2++] = suffix[s2];
                for (int m2 = 0; m2 < match_len; m2++)
                    source[si2++] = tokens->data[i + m2];
                source[si2].type = TOK_SEMICOLON;
                strncpy(source[si2].value, ";", 63);
                si2++;

                build_capture_inits(ex, captures, capture_types, result, unique_names);

                Token *replace_copy = malloc(sizeof(Token) * ex->replace_len);
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
                    } else {
                        vt_push_back(result, *rt);
                    }
                }

                free(replace_copy);
                if (suffix) free(suffix);
                free(source);

            } else {
                int inject_pos = stmt_start;

                int    suffix_len = (int)result->size - inject_pos;
                Token *suffix     = NULL;
                if (suffix_len > 0) {
                    suffix = malloc(sizeof(Token) * suffix_len);
                    memcpy(suffix, &result->data[inject_pos], sizeof(Token) * suffix_len);
                    result->size = inject_pos;
                }

                build_capture_inits(ex, captures, capture_types, result, unique_names);

                Token *checker_copy = malloc(sizeof(Token) * ex->checker_len);
                memcpy(checker_copy, ex->checker, sizeof(Token) * ex->checker_len);
                rename_captures_in_block(checker_copy, ex->checker_len, ex, unique_names);

                for (int ci = 0; ci < ex->checker_len; ci++)
                    vt_push_back(result, checker_copy[ci]);
                free(checker_copy);

                if (suffix_len > 0) {
                    for (int s2 = 0; s2 < suffix_len; s2++)
                        vt_push_back(result, suffix[s2]);
                    free(suffix);
                }

                for (int mi = 0; mi < match_len; mi++)
                    vt_push_back(result, tokens->data[i + mi]);
                {
                    Token semi;
                    semi.type = TOK_SEMICOLON;
                    strncpy(semi.value, ";", 63);
                    vt_push_back(result, semi);
                }
            }

            i += match_len;

            if (i < tokens->size && tokens->data[i].type == TOK_SEMICOLON)
                i++;
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
        } else {
            stmt_start = (int)result->size;
        }
    }

    return result;
}