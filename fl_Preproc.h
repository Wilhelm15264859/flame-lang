#pragma once
#define VERSION "1.4"

char *preprocess(const char *source, const char *base_dir, const char *input_file);
const char **preprocess_get_imports(int *count);
void        preprocess_set_target(const char *triple);
const char *preprocess_get_target(void);