TYPE		:= development

SLIB		:= libcerver.so

all: directories $(SLIB)

directories:
	@mkdir -p $(TARGETDIR)
	@mkdir -p $(BUILDDIR)

install: $(SLIB)
	install -m 644 ./bin/libcerver.so /usr/local/lib/
	cp -R ./include/cerver /usr/local/include

uninstall:
	rm /usr/local/lib/libcerver.so
	rm -r /usr/local/include/cerver

PTHREAD 	:= -l pthread
MATH		:= -lm

OPENSSL		:= -l ssl -l crypto

CURL		:= -l curl

DEFINES		:= -D _GNU_SOURCE

DEVELOPMENT	:= -D CERVER_DEBUG -D CERVER_STATS 				\
				-D CLIENT_DEBUG -D CLIENT_STATS 			\
				-D CONNECTION_DEBUG -D CONNECTION_STATS 	\
				-D HANDLER_DEBUG 							\
				-D PACKETS_DEBUG 							\
				-D AUTH_DEBUG 								\
				-D ADMIN_DEBUG								\
				-D FILES_DEBUG								\
				-D HTTP_DEBUG -D HTTP_HEADERS_DEBUG			\
				-D HTTP_AUTH_DEBUG -D HTTP_MPART_DEBUG		\
				-D HTTP_RESPONSE_DEBUG

CC          := gcc

GCCVGTEQ8 	:= $(shell expr `gcc -dumpversion | cut -f1 -d.` \>= 8)

SRCDIR      := src
INCDIR      := include

BUILDDIR    := objs
TARGETDIR   := bin

SRCEXT      := c
DEPEXT      := d
OBJEXT      := o

COVDIR		:= coverage
COVEXT		:= gcov

# common flags
# -Wconversion
COMMON		:= -march=native \
				-Wall -Wno-unknown-pragmas \
				-Wfloat-equal -Wdouble-promotion -Wint-to-pointer-cast -Wwrite-strings \
				-Wtype-limits -Wsign-compare -Wmissing-field-initializers \
				-Wuninitialized -Wmaybe-uninitialized -Wempty-body \
				-Wunused-parameter -Wunused-but-set-parameter -Wunused-result \
				-Wformat -Wformat-nonliteral -Wformat-security -Wformat-overflow -Wformat-signedness -Wformat-truncation

# main
CFLAGS      := $(DEFINES)

ifeq ($(TYPE), development)
	CFLAGS += -g -fasynchronous-unwind-tables $(DEVELOPMENT)
else ifeq ($(TYPE), test)
	CFLAGS += -g -fasynchronous-unwind-tables -D_FORTIFY_SOURCE=2 -fstack-protector -O2
else ifeq ($(TYPE), beta)
	CFLAGS += -g -D_FORTIFY_SOURCE=2 -O2
else
	CFLAGS += -D_FORTIFY_SOURCE=2 -O2
endif

# check which compiler we are using
ifeq ($(CC), g++) 
	CFLAGS += -std=c++11 -fpermissive
else
	CFLAGS += -std=c11 -Wpedantic -pedantic-errors
	# check for compiler version
	ifeq "$(GCCVGTEQ8)" "1"
    	CFLAGS += -Wcast-function-type
	else
		CFLAGS += -Wbad-function-cast
	endif
endif

# common flags
CFLAGS += -fPIC $(COMMON)

LIB         := -L /usr/local/lib $(PTHREAD) $(MATH) $(OPENSSL)

ifeq ($(TYPE), test)
	LIB += -lgcov --coverage
endif

INC         := -I $(INCDIR) -I /usr/local/include
INCDEP      := -I $(INCDIR)

SOURCES     := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS     := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))

SRCCOVS		:= $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(SRCEXT).$(COVEXT)))

# pull in dependency info for *existing* .o files
-include $(OBJECTS:.$(OBJEXT)=.$(DEPEXT))

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

# examples
EXAMDIR		:= examples
EXABUILD	:= $(EXAMDIR)/objs
EXATARGET	:= $(EXAMDIR)/bin

EXAFLAGS	:= $(DEFINES)

ifeq ($(TYPE), development)
	EXAFLAGS += -g -D EXAMPLES_DEBUG -fasynchronous-unwind-tables
else ifeq ($(TYPE), test)
	EXAFLAGS += -g -fasynchronous-unwind-tables -D_FORTIFY_SOURCE=2 -fstack-protector -O2
else ifeq ($(TYPE), beta)
	EXAFLAGS += -g -D_FORTIFY_SOURCE=2 -O2
else
	EXAFLAGS += -D_FORTIFY_SOURCE=2 -O2
endif

# check which compiler we are using
ifeq ($(CC), g++) 
	EXAFLAGS += -std=c++11 -fpermissive
else
	EXAFLAGS += -std=c11 -Wpedantic -pedantic-errors
endif

# common flags
EXAFLAGS += -Wall -Wno-unknown-pragmas

EXALIBS		:= -L ./bin -l cerver
EXAINC		:= -I ./$(EXAMDIR)

EXAMPLES	:= $(shell find $(EXAMDIR) -type f -name *.$(SRCEXT))
EXOBJS		:= $(patsubst $(EXAMDIR)/%,$(EXABUILD)/%,$(EXAMPLES:.$(SRCEXT)=.$(OBJEXT)))

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
	$(CC) $(EXADEBUG) $(EXAFLAGS) $(INC) $(EXAINC) $(EXALIBS) -c -o $@ $<
	@$(CC) $(EXADEBUG) $(EXAFLAGS) $(INCDEP) -MM $(EXAMDIR)/$*.$(SRCEXT) > $(EXABUILD)/$*.$(DEPEXT)
	@cp -f $(EXABUILD)/$*.$(DEPEXT) $(EXABUILD)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(EXABUILD)/$*.$(OBJEXT):|' < $(EXABUILD)/$*.$(DEPEXT).tmp > $(EXABUILD)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(EXABUILD)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(EXABUILD)/$*.$(DEPEXT)
	@rm -f $(EXABUILD)/$*.$(DEPEXT).tmp

# tests
TESTDIR		:= test
TESTBUILD	:= $(TESTDIR)/objs
TESTTARGET	:= $(TESTDIR)/bin
TESTCOVDIR	:= $(COVDIR)/test

TESTFLAGS	:= -g $(DEFINES) -Wall -Wno-unknown-pragmas -Wno-format

ifeq ($(TYPE), test)
	TESTFLAGS += -fprofile-arcs -ftest-coverage
endif

TESTLIBS	:= $(PTHREAD) $(CURL) -L ./bin -l cerver

ifeq ($(TYPE), test)
	TESTLIBS += -lgcov --coverage
endif

TESTINC		:= -I ./$(TESTDIR)

TESTS		:= $(shell find $(TESTDIR) -type f -name *.$(SRCEXT))
TESTOBJS	:= $(patsubst $(TESTDIR)/%,$(TESTBUILD)/%,$(TESTS:.$(SRCEXT)=.$(OBJEXT)))

TESTCOVS	:= $(patsubst $(TESTDIR)/%,$(TESTBUILD)/%,$(TESTS:.$(SRCEXT)=.$(SRCEXT).$(COVEXT)))

test: $(TESTOBJS)
	@mkdir -p ./$(TESTTARGET)
	$(CC) -g -I ./$(INCDIR) $(TESTINC) -L ./$(TARGETDIR) ./$(TESTBUILD)/collections/*.o -o ./$(TESTTARGET)/collections $(TESTLIBS)
	$(CC) -g -I ./$(INCDIR) $(TESTINC) -L ./$(TARGETDIR) ./$(TESTBUILD)/utils/*.o -o ./$(TESTTARGET)/utils $(TESTLIBS)
	$(CC) -g -I ./$(INCDIR) $(TESTINC) -L ./$(TARGETDIR) ./$(TESTBUILD)/json/*.o -o ./$(TESTTARGET)/json $(TESTLIBS)
	$(CC) -g -I ./$(INCDIR) $(TESTINC) -L ./$(TARGETDIR) ./$(TESTBUILD)/jwt/*.o -o ./$(TESTTARGET)/jwt $(TESTLIBS)
	$(CC) -g -I ./$(INCDIR) $(TESTINC) -L ./$(TARGETDIR) ./$(TESTBUILD)/web/web.o ./$(TESTBUILD)/web/curl.o -o ./$(TESTTARGET)/web $(TESTLIBS)
	$(CC) -g -I ./$(INCDIR) $(TESTINC) -L ./$(TARGETDIR) ./$(TESTBUILD)/web/api.o ./$(TESTBUILD)/web/curl.o -o ./$(TESTTARGET)/api $(TESTLIBS)
	$(CC) -g -I ./$(INCDIR) $(TESTINC) -L ./$(TARGETDIR) ./$(TESTBUILD)/web/upload.o ./$(TESTBUILD)/web/curl.o -o ./$(TESTTARGET)/upload $(TESTLIBS)
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

# test-run:
# 	@bash test/run.sh

#coverage
COVOBJS		:= $(SRCCOVS) $(TESTCOVS)

test-coverage: $(COVOBJS)

coverage-init:
	@mkdir -p ./$(COVDIR)
	@mkdir -p ./$(TESTCOVDIR)

coverage: coverage-init test-coverage

# get lib coverage reports
$(BUILDDIR)/%.$(SRCEXT).$(COVEXT): $(SRCDIR)/%.$(SRCEXT)
	@mkdir -p ./$(COVDIR)/$(dir $<)
	gcov -r $< --object-directory $(dir $@)
	mv $(notdir $@) ./$(COVDIR)/$<.gcov

# get tests coverage reports
$(TESTBUILD)/%.$(SRCEXT).$(COVEXT): $(TESTDIR)/%.$(SRCEXT)
	gcov -r $< --object-directory $(dir $@)
	mv $(notdir $@) ./$(TESTCOVDIR)

# benchmarks 
BENCHFLAGS	:= $(DEFINES) -Wall -Wno-unknown-pragmas -O3 -march=native -mavx2
BENCHLIBS	:= $(PTHREAD) $(CURL) -L ./bin -l cerver
BENCHINC	:= -I ./$(BENCHDIR)

BENCHS		:= $(shell find $(BENCHDIR) -type f -name *.$(SRCEXT))
BENCHOBJS	:= $(patsubst $(BENCHDIR)/%,$(BENCHBUILD)/%,$(BENCHS:.$(SRCEXT)=.$(OBJEXT)))

bench: $(BENCHOBJS)
	@mkdir -p ./$(BENCHTARGET)
	$(CC) -g -I ./$(INCDIR) $(BENCHINC) -L ./$(TARGETDIR) ./$(BENCHBUILD)/base64.o -o ./$(BENCHTARGET)/base64 $(BENCHLIBS)
	$(CC) -g -I ./$(INCDIR) $(BENCHINC) -L ./$(TARGETDIR) ./$(BENCHBUILD)/http-parser.o -o ./$(BENCHTARGET)/http-parser $(BENCHLIBS)
	$(CC) -g -I ./$(INCDIR) $(BENCHINC) -L ./$(TARGETDIR) ./$(BENCHBUILD)/web.o ./$(BENCHBUILD)/curl.o -o ./$(BENCHTARGET)/web $(BENCHLIBS)

# compile benchmarks
$(BENCHBUILD)/%.$(OBJEXT): $(BENCHDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(BENCHFLAGS) $(INC) $(BENCHINC) $(BENCHLIBS) -c -o $@ $<
	@$(CC) $(BENCHFLAGS) $(INCDEP) -MM $(BENCHDIR)/$*.$(SRCEXT) > $(BENCHBUILD)/$*.$(DEPEXT)
	@cp -f $(BENCHBUILD)/$*.$(DEPEXT) $(BENCHBUILD)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(BENCHBUILD)/$*.$(OBJEXT):|' < $(BENCHBUILD)/$*.$(DEPEXT).tmp > $(BENCHBUILD)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(BENCHBUILD)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(BENCHBUILD)/$*.$(DEPEXT)
	@rm -f $(BENCHBUILD)/$*.$(DEPEXT).tmp

clear:
	@$(RM) -rf $(BUILDDIR) 
	@$(RM) -rf $(EXABUILD)
	@$(RM) -rf $(EXATARGET)
	@$(RM) -rf $(TESTBUILD)
	@$(RM) -rf $(COVDIR)
	@$(RM) -rf $(BENCHBUILD)
	@$(RM) -rf $(BENCHTARGET)

clean: clear
	@$(RM) -rf $(TARGETDIR)

.PHONY: all clean clear examples test coverage bench