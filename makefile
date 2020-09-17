# TARGET      := cerver
SLIB		:= libcerver.so

PTHREAD 	:= -l pthread
MATH		:= -lm

# print additional information
# print additional information
DEFINES	= -D CERVER_DEBUG -D CERVER_STATS 	\
			-D CLIENT_DEBUG 				\
			-D HANDLER_DEBUG 				\
			-D PACKETS_DEBUG 				\
			-D AUTH_DEBUG 					\
			-D ADMIN_DEBUG

CC          := gcc

SRCDIR      := src
INCDIR      := include
BUILDDIR    := objs
TARGETDIR   := bin

EXAMDIR		:= examples

SRCEXT      := c
DEPEXT      := d
OBJEXT      := o

CFLAGS      := -g $(DEFINES) -Wall -Wno-unknown-pragmas -fPIC
LIB         := $(PTHREAD) $(MATH)
INC         := -I $(INCDIR) -I /usr/local/include
INCDEP      := -I $(INCDIR)

SOURCES     := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS     := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))

EXAMPLES	:= $(shell find $(EXAMDIR) -type f -name *.$(SRCEXT))

# all: directories $(TARGET)
all: directories $(SLIB)

run: 
	./$(TARGETDIR)/$(TARGET)

install: $(SLIB)
	install -m 644 ./bin/libcerver.so /usr/local/lib/
	cp -R ./include/cerver /usr/local/include

uninstall:
	rm /usr/local/lib/libcerver.so
	rm -r /usr/local/include/cerver

directories:
	@mkdir -p $(TARGETDIR)
	@mkdir -p $(BUILDDIR)

clean:
	@$(RM) -rf $(BUILDDIR) 
	@$(RM) -rf $(TARGETDIR)
	@$(RM) -rf ./examples/bin
	@$(RM) -rf ./examples/bin/client

# pull in dependency info for *existing* .o files
-include $(OBJECTS:.$(OBJEXT)=.$(DEPEXT))

# link
# $(TARGET): $(OBJECTS)
# 	$(CC) $^ $(LIB) -o $(TARGETDIR)/$(TARGET)

$(SLIB): $(OBJECTS)
	$(CC) $^ $(LIB) -shared -o $(TARGETDIR)/$(SLIB)

# compile
$(BUILDDIR)/%.$(OBJEXT): $(SRCDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) $(LIB) -c -o $@ $<
	@$(CC) $(CFLAGS) $(INCDEP) -MM $(SRCDIR)/$*.$(SRCEXT) > $(BUILDDIR)/$*.$(DEPEXT)
	@cp -f $(BUILDDIR)/$*.$(DEPEXT) $(BUILDDIR)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(BUILDDIR)/$*.$(OBJEXT):|' < $(BUILDDIR)/$*.$(DEPEXT).tmp > $(BUILDDIR)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(BUILDDIR)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(BUILDDIR)/$*.$(DEPEXT)
	@rm -f $(BUILDDIR)/$*.$(DEPEXT).tmp

examples: $(EXAMPLES)
	@mkdir -p ./examples/bin
	@mkdir -p ./examples/bin/client
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/welcome.c -o ./examples/bin/welcome -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/test.c -o ./examples/bin/test -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/handlers.c -o ./examples/bin/handlers -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/multi.c -o ./examples/bin/multi -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/threads.c -o ./examples/bin/threads -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/advanced.c -o ./examples/bin/advanced -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/requests.c -o ./examples/bin/requests -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/auth.c -o ./examples/bin/auth -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/sessions.c -o ./examples/bin/sessions -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/admin.c -o ./examples/bin/admin -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/packets.c -o ./examples/bin/packets -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/game.c -o ./examples/bin/game -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/client/client.c -o ./examples/bin/client/client -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/client/auth.c -o ./examples/bin/client/auth -l cerver

.PHONY: all clean examples