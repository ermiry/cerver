# TARGET      := cerver
SLIB		:= cerver.so

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

CFLAGS      := -g $(DEFINES) $(RUN_MAKE)
LIB         := $(PTHREAD) $(MATH) $(CMONGO)
INC         := -I $(INCDIR) -I /usr/local/include $(LIB)
INCDEP      := -I $(INCDIR)

SOURCES     := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS     := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))

# all: directories $(TARGET)
all: directories $(SLIB)

# run: 
# 	./$(TARGETDIR)/$(TARGET)

remake: cleaner all

directories:
	@mkdir -p $(TARGETDIR)
	@mkdir -p $(BUILDDIR)

# clean only Objecst
clean:
	@$(RM) -rf $(BUILDDIR) @$(RM) -rf $(TARGETDIR)
	@$(RM) -rf ./examples/bin

# full Clean, Objects and Binaries
cleaner: clean
	@$(RM) -rf $(TARGETDIR)

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
	$(CC) $(CFLAGS) $(INC) -c -fpic -o $@ $<
	@$(CC) $(CFLAGS) $(INCDEP) -MM $(SRCDIR)/$*.$(SRCEXT) > $(BUILDDIR)/$*.$(DEPEXT)
	@cp -f $(BUILDDIR)/$*.$(DEPEXT) $(BUILDDIR)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(BUILDDIR)/$*.$(OBJEXT):|' < $(BUILDDIR)/$*.$(DEPEXT).tmp > $(BUILDDIR)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(BUILDDIR)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(BUILDDIR)/$*.$(DEPEXT)
	@rm -f $(BUILDDIR)/$*.$(DEPEXT).tmp

examples: ./examples/welcome.c ./examples/game.c
	@mkdir -p ./examples/bin
	gcc -I ./include -L ./bin ./examples/welcome.c -o ./examples/bin/welcome -l:./bin/cerver.so
	gcc -I ./include -L ./bin ./examples/game.c -o ./examples/bin/game -l:./bin/cerver.so

# non-file Targets
.PHONY: all remake clean cleaner resources examples