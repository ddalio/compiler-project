# Makefile - compilador c-tds (C-TDS, Taller de Diseño de Software)
#
#   make          compila el ejecutable ./c-tds
#   make test     compila y corre todos los tests
#   make clean    borra el ejecutable y los archivos generados

BUILD   := build
TARGET  := c-tds

CC      ?= gcc
CFLAGS  ?= -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Isrc -I$(BUILD)
LDLIBS  += -lm
FLEX    ?= flex
BISON   ?= bison

OBJS    := $(BUILD)/main.o $(BUILD)/tokens.o $(BUILD)/lex.yy.o

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDLIBS) -o $@

# bison genera build/bison.tab.h con los codigos de los tokens (TOK_*), que
# usan el scanner y main. bison.tab.c (el parser) se genera junto con el header.
$(BUILD)/bison.tab.h: src/bison.y | $(BUILD)
	$(BISON) -d -Wall -o $(BUILD)/bison.tab.c $<

# El scanner se genera con flex a partir de src/scanner.l
$(BUILD)/lex.yy.c: src/scanner.l | $(BUILD)
	$(FLEX) -o $@ $<

# El codigo generado por flex trae funciones sin usar: no es codigo nuestro.
$(BUILD)/lex.yy.o: $(BUILD)/lex.yy.c src/scanner.h src/tokens.h $(BUILD)/bison.tab.h
	$(CC) $(CFLAGS) -Wno-unused-function -c $< -o $@

$(BUILD)/%.o: src/%.c src/scanner.h src/tokens.h $(BUILD)/bison.tab.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

test: $(TARGET)
	bash tests/run_tests.sh ./$(TARGET)

clean:
	rm -rf $(BUILD) $(TARGET)
