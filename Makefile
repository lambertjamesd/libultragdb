#!smake -k

include $(ROOT)/usr/include/make/PRdefs

RSP2DWARF = /home/james/go/src/github.com/lambertjamesd/rsp2dwarf/rsp2dwarf

OPTIMIZER       = -O2
LCDEFS			:= -DDEBUG -g -Isrc/ -I/usr/include/n64/nustd -Werror -Wall
N64LIB			:= -lultra_rom -lnustd

APP =		build/debugger.out
TARGETS =	build/debugger.n64

DEBUGGERHFILES = debugger/serial.h \
	debugger/rsp.h \
	debugger/debugger.h

HFILES =	$(DEBUGGERHFILES) example/graph.h \
	example/nu64sys.h \
	example/thread.h

DEBUGGERFILES = debugger/serial.c \
	debugger/rsp.c \
	debugger/debugger.c

CODEFILES   = $(DEBUGGERFILES) example/nu64sys.c \
	example/main.c \
	example/graph.c \
	example/asci.c

ASMFILES    =	asm/entry.s asm/rom_header.s

ASMOBJECTS  =	$(patsubst %.s, build/%.o, $(ASMFILES))

CODEOBJECTS =	$(CODEFILES:%.c=build/%.o)

DATAFILES   =	example/cfb.c

DATAOBJECTS =	$(DATAFILES:%.c=build/%.o)

CODESEGMENT =	codesegment.o

BOOT		=	/usr/lib/n64/PR/bootcode/boot.6102
BOOT_OBJ	=	build/boot.6102.o

OBJECTS =	$(CODESEGMENT) $(DATAOBJECTS) $(ASMOBJECTS) $(BOOT_OBJ)

LCINCS =	-I. -I$(ROOT)/usr/include/PR -I $(ROOT)/usr/include
LCOPTS =	-mno-shared -G 0
LDFLAGS =	-L/usr/lib/n64 $(N64LIB)  -L$(N64_LIBGCCDIR) -lgcc

LDIRT  =	$(APP)

build/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MM $^ -MF "$(@:.o=.d)" -MT"$@"
	$(CC) $(CFLAGS) -c -o $@ $<

build/asm/%.o: asm/%.s
	@mkdir -p $(@D)
	$(AS) -Wa,-Iasm -o $@ $<

bin/dump_rsp_state bin/dump_rsp_state.dat: debugger/dump_rsp_state.s
	mkdir -p bin/rsp
	rspasm -o bin/dump_rsp_state debugger/dump_rsp_state.s

build/debugger/dump_rsp_state.o: bin/dump_rsp_state bin/dump_rsp_state.dat
	@mkdir -p $(@D)
	$(RSP2DWARF) bin/dump_rsp_state -o build/debugger/dump_rsp_state.o -n dump_rsp_state
	
$(BOOT_OBJ): $(BOOT)
	$(OBJCOPY) -I binary -B mips -O elf32-bigmips $< $@

default:	$(TARGETS)

include $(COMMONRULES)

$(CODESEGMENT):	$(CODEOBJECTS)
		$(LD) -o $(CODESEGMENT) -r $(CODEOBJECTS) $(LDFLAGS)

build/example/example.ld: example/example.ld
	@mkdir -p $(@D)
	cpp -P -Wno-trigraphs $(LCDEFS) -DCODE_SEGMENT=$(CODESEGMENT) -o $@ $<

$(TARGETS) $(APP): build/example/example.ld $(OBJECTS) build/debugger/dump_rsp_state.o
	$(LD) -L. -T build/example/example.ld -Map build/debugger.map -o build/debugger.elf
	$(OBJCOPY) --pad-to=0x100000 --gap-fill=0xFF build/debugger.elf build/debugger.n64 -O binary
	makemask $(TARGETS)

clean:
	rm -rf build