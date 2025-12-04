BUILD_DIR?=../build/sim/

include $(BUILD_DIR)/software/include/generated/variables.mak
include $(SOC_DIRECTORY)/software/common.mak

#Wolf includes

# Compile all wolfcrypt sources
WOLFCRYPT_SRCS = $(wildcard wolfcrypt/src/*.c)

# Compile all wolfssl sources (for DTLS support)
WOLFSSL_SRCS = $(wildcard wolfssl/src/*.c)

# Main objects
OBJECTS  = crt0.o main.o network.o

# Add wolfcrypt and wolfssl objects
OBJECTS += $(WOLFCRYPT_SRCS:.c=.o)
OBJECTS += $(WOLFSSL_SRCS:.c=.o)

INC  = -I./wolfssl -I./wolfssl/wolfcrypt -I.

all: boot.bin

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
ifneq ($(OS),Windows_NT)
	chmod -x $@
endif

vpath %.a $(PACKAGES:%=../%)

# CFLAGS += $(INC) -DWOLFSSL_USER_SETTINGS

CFLAGS += $(INC) 
CFLAGS += -DWOLFSSL_USER_SETTINGS

# Force enable session cache by undefining the blockers
# settings.h auto-defines these due to NO_ASN_TIME, we override at compile time
CFLAGS += -UNO_SESSION_CACHE
CFLAGS += -UNO_CLIENT_CACHE

boot.elf: $(OBJECTS)
	$(CC) $(LDFLAGS) -T linker.ld -N -o $@ \
		$(OBJECTS) \
		$(PACKAGES:%=-L$(BUILD_DIR)/software/%) \
		-Wl,--whole-archive \
		-Wl,--gc-sections \
		-Wl,-Map,$@.map \
		$(LIBS:lib%=-l%)

ifneq ($(OS),Windows_NT)
	chmod -x $@
endif

# pull in dependency info for *existing* .o files
-include $(OBJECTS:.o=.d)

donut.o: CFLAGS   += -w

VPATH = $(BIOS_DIRECTORY):$(BIOS_DIRECTORY)/cmds:$(CPU_DIRECTORY)


%.o: %.cpp
	$(compilexx)

%.o: %.c
	$(compile)

%.o: %.S
	$(assemble)

clean:
	$(RM) $(OBJECTS) boot.elf boot.bin .*~ *~

.PHONY: all clean
