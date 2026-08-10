# Vulpin — scripting language
# Build with TCC: make          (release)
#                 make debug    (with symbols and no sanitizers)
#                 make clean

.PHONY: all clean debug help

CC      = tcc
CFLAGS  = -Wall -D_GNU_SOURCE
LDFLAGS = -lm

# COLORS
N    := \033[0m
G := \033[0;32m
Y    := \033[0;33m
B      := \033[0;34m
C      := \033[0;36m
###

SRCS   = vulpin.c vm.c
TARGET = vulpin

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

debug: CFLAGS += -g -b
debug: clean $(TARGET)
	@printf "$(GREEN)[ ✓ ]$(NC) $(YELLOW)Debug build ready: ./$(TARGET)$(YELLOW)$(NC)\n"

clean:
	rm -f $(TARGET) *.o

help:
	@printf "$(C)       === Vulpin Programming Languuage ===\n"
	@printf "$(B)  make        ->     $(Y)release build (fast, small)\n"
	@printf   "$(B)  make debug  ->  $(Y)debug build with symbols (-g -b)\n"
	@printf   "$(B)  make clean  ->  $(Y)remove build artifacts\n"
	@printf   "$(B)  make help   ->  $(Y)to get information about commands\n"
