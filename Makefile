CC     = gcc
CFLAGS = -g -no-pie -Wall -Wextra $(shell llvm-config --cflags)
LIBS   = $(shell llvm-config --libs all core analysis bitwriter target) -lm
SRCS   = flame.c fl_Lexer.c fl_Parser.c fl_Builder.c fl_Preproc.c fl_Exception.c fl_Pregen.c \
         utils/vector_node.c utils/vector_token.c
OUT    = flame

$(OUT): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) $(LIBS) -o $(OUT)

clean:
	rm -f $(OUT)
