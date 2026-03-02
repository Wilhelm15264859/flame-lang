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

static int parse_pattern(Token *toks, int len, PatternToken *out) {
    int out_len = 0;
    for (int i = 0; i < len && out_len < MAX_PATTERN_TOKENS; i++) {
        Token *t = &toks[i];

        if (t->type == TOK_OP && strcmp(t->value, "%") == 0) {
            i++;
            if (i >= len) break;
            Token *next = &toks[i];

            if (strcmp(next->value, "i") == 0) {
                out[out_len++] = (PatternToken){ PAT_IDENT, "" };
            } else if (strcmp(next->value, "k") == 0) {
                out[out_len++] = (PatternToken){ PAT_KEYWORD, "" };
            } else if (strcmp(next->value, "n") == 0) {
                out[out_len++] = (PatternToken){ PAT_NUMBER, "" };
            } else if (strcmp(next->value, "s") == 0) {
                out[out_len++] = (PatternToken){ PAT_STRING, "" };
            } else if (strcmp(next->value, ":") == 0) {
                i++;
                if (i >= len) break;
                PatternToken pt;
                pt.kind = PAT_CAPTURE;
                strncpy(pt.value, toks[i].value, 63);
                out[out_len++] = pt;
            } else {
                printf("Warning: unknown pattern specifier '%%%s'\n", next->value);
            }
        } else {
            PatternToken pt;
            pt.kind = PAT_LITERAL;
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
            case PAT_CAPTURE:
                strncpy(captures[pi], t->value, 63);
                capture_types[pi] = t->type;
                break;
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

        /* уникальное имя: _ex_<cap_name>_<counter>
         * cap_name <= 48 chars, "_ex_"=4, "_"=1, counter<=10 → max 63 */
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

/* заменяет вхождения оригинальных capture-имён на уникальные в блоке токенов */
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
    int stmt_start = 0;  /* индекс в result где начался текущий statement */
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

            /* уникальные имена capture-переменных для этого матча */
            char unique_names[MAX_PATTERN_TOKENS][64];
            memset(unique_names, 0, sizeof(unique_names));

            if (ex->has_replace) {
                /* используем stmt_start — точно начало текущего statement */
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

                /* копируем replace-блок и переименовываем capture-переменные */
                Token *replace_copy = malloc(sizeof(Token) * ex->replace_len);
                memcpy(replace_copy, ex->replace, sizeof(Token) * ex->replace_len);
                rename_captures_in_block(replace_copy, ex->replace_len, ex, unique_names);

                for (int ci = 0; ci < ex->replace_len; ci++) {
                    Token *rt = &replace_copy[ci];

                    if (rt->type == TOK_OP && strcmp(rt->value, "$") == 0 &&
                        ci + 1 < ex->replace_len &&
                        strcmp(replace_copy[ci + 1].value, "source") == 0)
                    {
                        /* если после $source в replace стоит ';', не вставляем
                         * финальный ';' из source — иначе получится двойной ;; */
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
                /* используем stmt_start — точно начало текущего statement */
                int inject_pos = stmt_start;

                int    suffix_len = (int)result->size - inject_pos;
                Token *suffix     = NULL;
                if (suffix_len > 0) {
                    suffix = malloc(sizeof(Token) * suffix_len);
                    memcpy(suffix, &result->data[inject_pos], sizeof(Token) * suffix_len);
                    result->size = inject_pos;
                }

                build_capture_inits(ex, captures, capture_types, result, unique_names);

                /* копируем check-блок и переименовываем capture-переменные */
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

                /* добавляем ';' в result для check — он будет пропущен из input ниже */
                {
                    Token semi;
                    semi.type = TOK_SEMICOLON;
                    strncpy(semi.value, ";", 63);
                    vt_push_back(result, semi);
                }
            }

            i += match_len;

            /* пропускаем финальный ';' оригинального statement из входного потока
             * (он уже включён в $source или statement завершён replace-блоком) */
            if (i < tokens->size && tokens->data[i].type == TOK_SEMICOLON)
                i++;

            /* stmt_start уже обновлён выше в else-ветке */
        }

        if (!matched) {
            Token *t = &tokens->data[i];
            vt_push_back(result, *t);
            /* обновляем stmt_start после разделителей statement */
            if (t->type == TOK_SEMICOLON ||
                strcmp(t->value, "{") == 0 ||
                strcmp(t->value, "}") == 0)
            {
                stmt_start = (int)result->size;
            }
            i++;
        } else {
            /* после replace/check stmt_start = конец вставленного блока */
            stmt_start = (int)result->size;
        }
    }

    return result;
}