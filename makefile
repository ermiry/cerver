# TARGET      := cerver
SLIB		:= libcerver.so

PTHREAD 	:= -l pthread
MATH		:= -lm
CMONGO 		:= `pkg-config --libs --cflags libmongoc-1.0`

# print additional information
DEFINES = -D CERVER_DEBUG -D CERVER_STATS

CC          := gcc

SRCDIR      := src
INCDIR      := include
BUILDDIR    := objs
TARGETDIR   := bin

SRCEXT      := c
DEPEXT      := d
OBJEXT      := o

# CFLAGS      := -g $(DEFINES)
CFLAGS      := -g $(DEFINES) -Wall -Wno-unknown-pragmas -fPIC
LIB         := $(PTHREAD) $(MATH) $(CMONGO)
INC         := -I $(INCDIR) -I /usr/local/include
INCDEP      := -I $(INCDIR)

SOURCES     := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS     := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))

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
	@$(RM) -rf $(BUILDDIR) @$(RM) -rf $(TARGETDIR)
	@$(RM) -rf ./examples/bin

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

examples: ./examples/welcome.c ./examples/game.c ./examples/test.c ./examples/handlers.c ./examples/multi.c ./examples/requests.c ./examples/web/web.c
	@mkdir -p ./examples/bin
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/welcome.c -o ./examples/bin/welcome -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/game.c -o ./examples/bin/game -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/test.c -o ./examples/bin/test -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/handlers.c -o ./examples/bin/handlers -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/multi.c -o ./examples/bin/multi -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/requests.c -o ./examples/bin/requests -l cerver
	$(CC) -g -Wall -Wno-unknown-pragmas -I ./include -L ./bin ./examples/web/web.c $(CMONGO) -o ./examples/bin/web -l cerver

.PHONY: all clean examples