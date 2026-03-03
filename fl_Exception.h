#pragma once
#include "fl_Lexer.h"
#define MAX_PATTERN_TOKENS 64
#define MAX_CHECKER_TOKENS 512
#define MAX_EXCEPTIONS     32
#define MAX_FIELDS         16
typedef enum {
PAT_LITERAL,
PAT_IDENT,
PAT_KEYWORD,
PAT_NUMBER,
PAT_STRING,
PAT_CAPTURE,
} PatternTokenType;
typedef struct {
PatternTokenType kind;
char             value[64];
PatternTokenType capture_type;
} PatternToken;
typedef struct {
char         name[64];
Token        fields[MAX_FIELDS * 8];
int          field_count;
PatternToken pattern[MAX_PATTERN_TOKENS];
int          pattern_len;
Token        checker[MAX_CHECKER_TOKENS];
int          checker_len;
Token        replace[MAX_CHECKER_TOKENS];
int          replace_len;
int          has_replace;
} Exception;
extern Exception exceptions[MAX_EXCEPTIONS];
extern int       exception_count;
vector_token *extract_exceptions(vector_token *tokens);
vector_token *preparse(vector_token *tokens);