#pragma once

char       *preprocess(const char *source, const char *base_dir);
void        preprocess_set_target(const char *triple);
const char *preprocess_get_target(void);