#!smake -k

include $(ROOT)/usr/include/make/PRdefs

FINAL = YES

ifeq ($(FINAL), YES)
# OPTIMIZER       = -O2
OPTIMIZER       = -g
LCDEFS          = -DNDEBUG -D_FINALROM -DF3DEX_GBI_2
N64LIB          = -lultra_rom
else
OPTIMIZER       = -g
LCDEFS          = -DDEBUG -DF3DEX_GBI_2
N64LIB          = -lultra_d
endif

APP =		debugger.out

TARGETS =	debugger.n64

MAKEROM = /home/james/go/src/github.com/trhodeos/spicy/cmd/spicy/spicy \
	--as_command=mips64-elf-as \
	--cpp_command=cpp \
	--ld_command=mips64-elf-ld \
	--objcopy_command=mips64-elf-objcopy

DEBUGGERHFILES = debugger/serial.h \
	debugger/debugger.h

HFILES =	$(DEBUGGERHFILES) example/graph.h \
	example/nu64sys.h \
	example/thread.h

DEBUGGERFILES = debugger/serial.c \
	debugger/debugger.c

CODEFILES   = $(DEBUGGERFILES) example/nu64sys.c \
	example/main.c \
	example/graph.c \
	example/asci.c

CODEOBJECTS =	$(CODEFILES:.c=.o)

DATAFILES   =	example/cfb.c

DATAOBJECTS =	$(DATAFILES:.c=.o)

CODESEGMENT =	codesegment.o

OBJECTS =	$(CODESEGMENT) $(DATAOBJECTS)

LCINCS =	-I. -I$(ROOT)/usr/include/PR -I $(ROOT)/usr/include
LCOPTS =	-mno-shared -G 0
LDFLAGS =	$(MKDEPOPT) -L$(ROOT)/usr/lib $(N64LIB) -L$(N64_LIBGCCDIR) -L$(N64_NEWLIBDIR) -lgcc -lc

LDIRT  =	$(APP)

default:	$(TARGETS)

include $(COMMONRULES)

$(CODESEGMENT):	$(CODEOBJECTS)
		$(LD) -o $(CODESEGMENT) -r $(CODEOBJECTS) $(LDFLAGS)

ifeq ($(FINAL), YES)
$(TARGETS) $(APP):      example/spec $(OBJECTS)
	$(MAKEROM) -s 9 -r $(TARGETS) example/spec
	makemask $(TARGETS)
else
$(TARGETS) $(APP):      example/spec $(OBJECTS)
	$(MAKEROM) -r $(TARGETS) example/spec
endif

cleanall: clean
	rm -f $(CODEOBJECTS) $(OBJECTS)