# Makefile - compilador c-tds (C-TDS, Taller de Diseño de Software)
#
#   make          compila el ejecutable ./c-tds
#   make test     compila y corre todos los tests (unitarios y de programas)
#   make test-asan  corre los tests con sanitizers (memoria)
#   make clean    borra el ejecutable y los archivos generados

BUILD   := build
TARGET  := c-tds

CC      ?= gcc
CFLAGS  ?= -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Isrc -I$(BUILD)
LDLIBS  += -lm
FLEX    ?= flex
BISON   ?= bison

OBJS    := $(BUILD)/main.o $(BUILD)/tokens.o $(BUILD)/tree.o \
           $(BUILD)/types.o $(BUILD)/ast.o $(BUILD)/symtab.o $(BUILD)/semantic.o \
           $(BUILD)/lex.yy.o $(BUILD)/bison.tab.o

# Tests unitarios (tests/unit): cada uno es un ejecutable que se arma en $(BUILD).
# Usan todos los objetos del compilador menos main.o.
LIBOBJS    := $(filter-out $(BUILD)/main.o,$(OBJS))
UNIT_TESTS := $(BUILD)/test_symtab $(BUILD)/test_ast

.PHONY: all test test-asan clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDLIBS) -o $@

# El parser se genera con bison a partir de src/bison.y. Una sola corrida
# produce build/bison.tab.c (el parser) y build/bison.tab.h (con los codigos
# TOK_* que usan el scanner y main); por eso el header es el objetivo y el .c
# se declara dependiente de el.
$(BUILD)/bison.tab.h: src/bison.y | $(BUILD)
	$(BISON) -d -Wall -o $(BUILD)/bison.tab.c $<

$(BUILD)/bison.tab.c: $(BUILD)/bison.tab.h ;

$(BUILD)/bison.tab.o: $(BUILD)/bison.tab.c src/parser.h src/scanner.h src/tree.h
	$(CC) $(CFLAGS) -c $< -o $@

# El scanner se genera con flex a partir de src/scanner.l
$(BUILD)/lex.yy.c: src/scanner.l | $(BUILD)
	$(FLEX) -o $@ $<

# El codigo generado por flex trae funciones sin usar: no es codigo nuestro.
$(BUILD)/lex.yy.o: $(BUILD)/lex.yy.c src/scanner.h src/tokens.h src/tree.h $(BUILD)/bison.tab.h
	$(CC) $(CFLAGS) -Wno-unused-function -c $< -o $@

$(BUILD)/%.o: src/%.c src/scanner.h src/parser.h src/tokens.h src/tree.h src/types.h src/ast.h src/symtab.h src/semantic.h \
               $(BUILD)/bison.tab.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_symtab: tests/unit/test_symtab.c $(BUILD)/symtab.o $(BUILD)/types.o \
                      src/symtab.h src/types.h | $(BUILD)
	$(CC) $(CFLAGS) tests/unit/test_symtab.c $(BUILD)/symtab.o $(BUILD)/types.o -o $@

$(BUILD)/test_ast: tests/unit/test_ast.c $(LIBOBJS) | $(BUILD)
	$(CC) $(CFLAGS) tests/unit/test_ast.c $(LIBOBJS) $(LDLIBS) -o $@

test: $(TARGET) $(UNIT_TESTS)
	UNIT_DIR=$(BUILD) bash tests/run_tests.sh ./$(TARGET)

# Corre los mismos tests con un ejecutable compilado con AddressSanitizer y
# UBSan (detectan perdidas de memoria y accesos invalidos). Usa carpetas y
# ejecutable propios para no pisar el build normal.
test-asan:
	$(MAKE) BUILD=build-asan TARGET=c-tds-asan CC="$(CC) -fsanitize=address,undefined" test

clean:
	rm -rf $(BUILD) $(TARGET) build-asan c-tds-asan
