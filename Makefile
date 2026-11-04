# Makefile - compilador c-tds (C-TDS, Taller de Diseño de Software)
#
#   make          compila el ejecutable ./c-tds
#   make test     compila y corre todos los tests
#   make clean    borra el ejecutable y los archivos generados

CC      ?= gcc
CFLAGS  ?= -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Isrc
LDLIBS  += -lm
FLEX    ?= flex

BUILD   := build
TARGET  := c-tds
OBJS    := $(BUILD)/main.o $(BUILD)/tokens.o $(BUILD)/lex.yy.o

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDLIBS) -o $@

# El scanner se genera con flex a partir de src/scanner.l
$(BUILD)/lex.yy.c: src/scanner.l | $(BUILD)
	$(FLEX) -o $@ $<

# El codigo generado por flex trae funciones sin usar: no es codigo nuestro.
$(BUILD)/lex.yy.o: $(BUILD)/lex.yy.c src/scanner.h src/tokens.h
	$(CC) $(CFLAGS) -Wno-unused-function -c $< -o $@

$(BUILD)/%.o: src/%.c src/scanner.h src/tokens.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

test: $(TARGET)
	bash tests/run_tests.sh ./$(TARGET)

clean:
	rm -rf $(BUILD) $(TARGET)
