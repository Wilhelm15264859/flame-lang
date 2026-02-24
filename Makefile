CC     = gcc
CFLAGS = -g -Wall -Wextra $(shell llvm-config --cflags)
LIBS   = $(shell llvm-config --libs) -lm
SRCS   = flame.c fl_Lexer.c fl_Parser.c fl_Builder.c \
         utils/vector_node.c utils/vector_token.c
OUT    = flame

$(OUT): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) $(LIBS) -o $(OUT)

clean:
	rm -f $(OUT)
