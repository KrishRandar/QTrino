CC = gcc
WOLFSSL_DIR = ../boot/wolfssl
WOLFCRYPT_DIR = ../boot/wolfcrypt

# Include paths for local wolfSSL
CFLAGS = -I$(WOLFSSL_DIR) -I$(WOLFSSL_DIR)/wolfcrypt -I../boot
CFLAGS += -DWOLFSSL_USER_SETTINGS 
CFLAGS += -DWOLFSSL_WC_DILITHIUM -DWOLFSSL_HAVE_DILITHIUM -DDILITHIUM_LEVEL2

# Link against wolfcrypt object files
WOLFCRYPT_OBJS = $(wildcard $(WOLFCRYPT_DIR)/src/*.o)

# If no .o files exist, we need to compile wolfcrypt sources
WOLFCRYPT_SRCS = $(wildcard $(WOLFCRYPT_DIR)/src/*.c)

all: gen_pqc_certs

gen_pqc_certs: gen_pqc_certs.c
	@echo "Building certificate generator..."
	@echo "Note: This may fail if wolfSSL's Dilithium API is not available"
	$(CC) $(CFLAGS) -o gen_pqc_certs gen_pqc_certs.c $(WOLFCRYPT_SRCS) -lm

certs: gen_pqc_certs
	./gen_pqc_certs

clean:
	rm -f gen_pqc_certs *.der *.h

.PHONY: all certs clean
