# TARGET      := cerver
SLIB		:= libcerver.so

PTHREAD 	:= -l pthread
MATH		:= -lm

OPENSSL		:= -l ssl -l crypto

DEFINES		:= -D _GNU_SOURCE

DEVELOPMENT	:= -g \
				-D CERVER_DEBUG -D CERVER_STATS 			\
				-D CLIENT_DEBUG -D CLIENT_STATS 			\
				-D CONNECTION_DEBUG -D CONNECTION_STATS 	\
				-D HANDLER_DEBUG 							\
				-D PACKETS_DEBUG 							\
				-D AUTH_DEBUG 								\
				-D ADMIN_DEBUG								\
				-D FILES_DEBUG								\
				-D HTTP_DEBUG -D HTTP_HEADERS_DEBUG -D HTTP_AUTH_DEBUG -D HTTP_MPART_DEBUG -D HTTP_RESPONSE_DEBUG

CC          := gcc

SRCDIR      := src
INCDIR      := include
BUILDDIR    := objs
TARGETDIR   := bin

EXAMDIR		:= examples
EXABUILD	:= $(EXAMDIR)/objs
EXATARGET	:= $(EXAMDIR)/bin

TESTDIR		:= test
TESTBUILD	:= $(TESTDIR)/objs
TESTTARGET	:= $(TESTDIR)/bin

BENCHDIR	:= benchmarks
BENCHBUILD	:= $(BENCHDIR)/objs
BENCHTARGET	:= $(BENCHDIR)/bin

SRCEXT      := c
DEPEXT      := d
OBJEXT      := o

CFLAGS      := $(DEVELOPMENT) $(DEFINES) -Wall -Wno-unknown-pragmas -fPIC
LIB         := $(PTHREAD) $(MATH) $(OPENSSL)
INC         := -I $(INCDIR) -I /usr/local/include
INCDEP      := -I $(INCDIR)

EXAFLAGS	:= -g $(DEFINES) -Wall -Wno-unknown-pragmas
EXALIBS		:= -L ./bin -l cerver
EXAINC		:= -I ./$(EXAMDIR)

TESTFLAGS	:= -g $(DEFINES) -Wall -Wno-unknown-pragmas
TESTLIBS	:= $(PTHREAD) -L ./bin -l cerver
TESTINC		:= -I ./$(TESTDIR)

BENCHFLAGS	:= -g $(DEFINES) -Wall -Wno-unknown-pragmas
BENCHLIBS	:= $(PTHREAD) -L ./bin -l cerver
BENCHINC	:= -I ./$(BENCHDIR)

SOURCES     := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS     := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))

EXAMPLES	:= $(shell find $(EXAMDIR) -type f -name *.$(SRCEXT))
EXOBJS		:= $(patsubst $(EXAMDIR)/%,$(EXABUILD)/%,$(EXAMPLES:.$(SRCEXT)=.$(OBJEXT)))

TESTS		:= $(shell find $(TESTDIR) -type f -name *.$(SRCEXT))
TESTOBJS	:= $(patsubst $(TESTDIR)/%,$(TESTBUILD)/%,$(TESTS:.$(SRCEXT)=.$(OBJEXT)))

BENCHS		:= $(shell find $(BENCHDIR) -type f -name *.$(SRCEXT))
BENCHOBJS	:= $(patsubst $(BENCHDIR)/%,$(BENCHBUILD)/%,$(BENCHS:.$(SRCEXT)=.$(OBJEXT)))

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
	@$(RM) -rf $(EXABUILD)
	@$(RM) -rf $(EXATARGET)
	@$(RM) -rf $(TESTBUILD)
	@$(RM) -rf $(TESTTARGET)
	@$(RM) -rf $(BENCHBUILD)
	@$(RM) -rf $(BENCHTARGET)

# pull in dependency info for *existing* .o files
-include $(OBJECTS:.$(OBJEXT)=.$(DEPEXT))

# link
# $(TARGET): $(OBJECTS)
# 	$(CC) $^ $(LIB) -o $(TARGETDIR)/$(TARGET)

$(SLIB): $(OBJECTS)
	$(CC) $^ $(LIB) -shared -o $(TARGETDIR)/$(SLIB)

# compile sources
$(BUILDDIR)/%.$(OBJEXT): $(SRCDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) $(LIB) -c -o $@ $<
	@$(CC) $(CFLAGS) $(INCDEP) -MM $(SRCDIR)/$*.$(SRCEXT) > $(BUILDDIR)/$*.$(DEPEXT)
	@cp -f $(BUILDDIR)/$*.$(DEPEXT) $(BUILDDIR)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(BUILDDIR)/$*.$(OBJEXT):|' < $(BUILDDIR)/$*.$(DEPEXT).tmp > $(BUILDDIR)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(BUILDDIR)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(BUILDDIR)/$*.$(DEPEXT)
	@rm -f $(BUILDDIR)/$*.$(DEPEXT).tmp

examples: $(EXOBJS)
	@mkdir -p ./$(EXATARGET)
	@mkdir -p ./$(EXATARGET)/client
	@mkdir -p ./$(EXATARGET)/web
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/welcome.o -o ./$(EXATARGET)/welcome -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/test.o -o ./$(EXATARGET)/test -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/handlers.o -o ./$(EXATARGET)/handlers -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/multi.o -o ./$(EXATARGET)/multi -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/threads.o -o ./$(EXATARGET)/threads -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/advanced.o -o ./$(EXATARGET)/advanced -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/requests.o -o ./$(EXATARGET)/requests -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/files.o -o ./$(EXATARGET)/files -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/auth.o -o ./$(EXATARGET)/auth -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/sessions.o -o ./$(EXATARGET)/sessions -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/admin.o -o ./$(EXATARGET)/admin -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/packets.o -o ./$(EXATARGET)/packets -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/logs.o -o ./$(EXATARGET)/logs -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/game.o -o ./$(EXATARGET)/game -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/client/client.o -o ./$(EXATARGET)/client/client -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/client/auth.o -o ./$(EXATARGET)/client/auth -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/client/files.o -o ./$(EXATARGET)/client/files -l cerver
	$(CC) -I ./$(INCDIR) -I $(EXAMDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/web/api.o ./$(EXABUILD)/users.o ./$(EXABUILD)/web/users.o -o ./$(EXATARGET)/web/api -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/web/upload.o -o ./$(EXATARGET)/web/upload -l cerver
	$(CC) -I ./$(INCDIR) -L ./$(TARGETDIR) ./$(EXABUILD)/web/web.o -o ./$(EXATARGET)/web/web -l cerver

# compile examples
$(EXABUILD)/%.$(OBJEXT): $(EXAMDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(EXAFLAGS) $(INC) $(EXAINC) $(EXALIBS) -c -o $@ $<
	@$(CC) $(EXAFLAGS) $(INCDEP) -MM $(EXAMDIR)/$*.$(SRCEXT) > $(EXABUILD)/$*.$(DEPEXT)
	@cp -f $(EXABUILD)/$*.$(DEPEXT) $(EXABUILD)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(EXABUILD)/$*.$(OBJEXT):|' < $(EXABUILD)/$*.$(DEPEXT).tmp > $(EXABUILD)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(EXABUILD)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(EXABUILD)/$*.$(DEPEXT)
	@rm -f $(EXABUILD)/$*.$(DEPEXT).tmp

test: $(TESTOBJS)
	@mkdir -p ./$(TESTTARGET)
	$(CC) -g -I ./$(INCDIR) $(TESTINC) -L ./$(TARGETDIR) ./$(TESTBUILD)/dlist.o -o ./$(TESTTARGET)/dlist $(TESTLIBS)
	$(CC) -g -I ./$(INCDIR) $(TESTINC) -L ./$(TARGETDIR) ./$(TESTBUILD)/json/*.o -o ./$(TESTTARGET)/json $(TESTLIBS)
	$(CC) -g -I ./$(INCDIR) $(TESTINC) -L ./$(TARGETDIR) ./$(TESTBUILD)/jwt/*.o -o ./$(TESTTARGET)/jwt $(TESTLIBS)
	$(CC) -g -I ./$(INCDIR) $(TESTINC) -L ./$(TARGETDIR) ./$(TESTBUILD)/jwt_encode.o ./$(TESTBUILD)/users.o -o ./$(TESTTARGET)/jwt_encode $(TESTLIBS)
	$(CC) -g -I ./$(INCDIR) $(TESTINC) -L ./$(TARGETDIR) ./$(TESTBUILD)/jwt_decode.o ./$(TESTBUILD)/users.o -o ./$(TESTTARGET)/jwt_decode $(TESTLIBS)

# compile tests
$(TESTBUILD)/%.$(OBJEXT): $(TESTDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(TESTFLAGS) $(INC) $(TESTINC) $(TESTLIBS) -c -o $@ $<
	@$(CC) $(TESTFLAGS) $(INCDEP) -MM $(TESTDIR)/$*.$(SRCEXT) > $(TESTBUILD)/$*.$(DEPEXT)
	@cp -f $(TESTBUILD)/$*.$(DEPEXT) $(TESTBUILD)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(TESTBUILD)/$*.$(OBJEXT):|' < $(TESTBUILD)/$*.$(DEPEXT).tmp > $(TESTBUILD)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(TESTBUILD)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(TESTBUILD)/$*.$(DEPEXT)
	@rm -f $(TESTBUILD)/$*.$(DEPEXT).tmp

bench: $(BENCHOBJS)
	@mkdir -p ./$(BENCHTARGET)
	$(CC) -g -I ./$(INCDIR) $(BENCHINC) -L ./$(TARGETDIR) ./$(BENCHBUILD)/http-parser.o -o ./$(BENCHTARGET)/http-parser $(BENCHLIBS)

# compile BENCHs
$(BENCHBUILD)/%.$(OBJEXT): $(BENCHDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(BENCHFLAGS) $(INC) $(BENCHINC) $(BENCHLIBS) -c -o $@ $<
	@$(CC) $(BENCHFLAGS) $(INCDEP) -MM $(BENCHDIR)/$*.$(SRCEXT) > $(BENCHBUILD)/$*.$(DEPEXT)
	@cp -f $(BENCHBUILD)/$*.$(DEPEXT) $(BENCHBUILD)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(BENCHBUILD)/$*.$(OBJEXT):|' < $(BENCHBUILD)/$*.$(DEPEXT).tmp > $(BENCHBUILD)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(BENCHBUILD)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(BENCHBUILD)/$*.$(DEPEXT)
	@rm -f $(BENCHBUILD)/$*.$(DEPEXT).tmp

.PHONY: all clean examples test bench