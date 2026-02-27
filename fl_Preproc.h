// fl_Preprocessor.h
#ifndef FL_PREPROCESSOR_H
#define FL_PREPROCESSOR_H

// source  — исходный текст
// base_dir — папка для поиска #include файлов (например "." или dirname входного файла)
// возвращает новую строку, которую нужно free() после использования
char *preprocess(const char *source, const char *base_dir);

#endif