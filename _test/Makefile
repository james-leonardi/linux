CC = gcc
CFLAGS = -Wall

SRC = s2_encrypt_test.c
EXE = s2_encrypt_test

all: $(EXE)
$(EXE): $(SRC)
	$(CC) $(CFLAGS) -o $(EXE) $(SRC)

clean:
	rm -f $(EXE)

.PHONY: all clean
