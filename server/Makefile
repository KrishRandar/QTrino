CC = gcc
CFLAGS = -I. -I../boot -I../boot/wolfssl -I../boot/wolfssl/wolfcrypt -DWOLFSSL_USER_SETTINGS -include user_settings.h
LDFLAGS = 

# WolfSSL Sources
WOLFSSL_SRC = ../boot/wolfssl/src
WOLFCRYPT_SRC = ../boot/wolfcrypt/src

# Source files
SERVER_SRC = server.c
WOLFSSL_SRCS = $(wildcard $(WOLFSSL_SRC)/*.c)
WOLFCRYPT_SRCS = $(wildcard $(WOLFCRYPT_SRC)/*.c)

# Object files (local)
OBJS = $(SERVER_SRC:.c=.o) \
       $(notdir $(WOLFSSL_SRCS:.c=.o)) \
       $(notdir $(WOLFCRYPT_SRCS:.c=.o))

all: server

# Compile server
server: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Server built successfully!"

# Compile local server.c
server.o: server.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile wolfSSL sources locally
%.o: $(WOLFSSL_SRC)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile wolfCrypt sources locally
%.o: $(WOLFCRYPT_SRC)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f server *.o
.PHONY: all clean
