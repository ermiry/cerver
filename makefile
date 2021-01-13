TYPE		:= development

NATIVE		:= 0

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
COMMON		:= -Wall -Wno-unknown-pragmas \
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

ifeq ($(NATIVE), 1)
	CFLAGS += -march=native
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

ifeq ($(NATIVE), 1)
	EXAFLAGS += -march=native
endif

# common flags
EXAFLAGS += -Wall -Wno-unknown-pragmas

EXALIBS		:= -L ./$(TARGETDIR) -l cerver
EXAINC		:= -I ./$(INCDIR) -I ./$(EXAMDIR)

EXAMPLES	:= $(shell find $(EXAMDIR) -type f -name *.$(SRCEXT))
EXOBJS		:= $(patsubst $(EXAMDIR)/%,$(EXABUILD)/%,$(EXAMPLES:.$(SRCEXT)=.$(OBJEXT)))

examples: $(EXOBJS)
	@mkdir -p ./$(EXATARGET)
	@mkdir -p ./$(EXATARGET)/client
	@mkdir -p ./$(EXATARGET)/web
	$(CC) $(EXAINC) ./$(EXABUILD)/welcome.o -o ./$(EXATARGET)/welcome $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/test.o -o ./$(EXATARGET)/test $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/handlers.o -o ./$(EXATARGET)/handlers $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/multi.o -o ./$(EXATARGET)/multi $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/threads.o -o ./$(EXATARGET)/threads $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/advanced.o -o ./$(EXATARGET)/advanced $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/requests.o -o ./$(EXATARGET)/requests $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/files.o -o ./$(EXATARGET)/files $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/auth.o -o ./$(EXATARGET)/auth $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/sessions.o -o ./$(EXATARGET)/sessions $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/admin.o -o ./$(EXATARGET)/admin $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/packets.o -o ./$(EXATARGET)/packets $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/logs.o -o ./$(EXATARGET)/logs $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/game.o -o ./$(EXATARGET)/game $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/client/client.o -o ./$(EXATARGET)/client/client $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/client/auth.o -o ./$(EXATARGET)/client/auth $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/client/files.o -o ./$(EXATARGET)/client/files $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/api.o ./$(EXABUILD)/users.o ./$(EXABUILD)/web/users.o -o ./$(EXATARGET)/web/api $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/upload.o -o ./$(EXATARGET)/web/upload $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/web.o -o ./$(EXATARGET)/web/web $(EXALIBS)

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

ifeq ($(NATIVE), 1)
	TESTFLAGS += -march=native
endif

TESTLIBS	:= $(PTHREAD) $(CURL) -L ./$(TARGETDIR) -l cerver

ifeq ($(TYPE), test)
	TESTLIBS += -lgcov --coverage
endif

TESTINC		:= -I $(INCDIR) -I ./$(TESTDIR)

TESTS		:= $(shell find $(TESTDIR) -type f -name *.$(SRCEXT))
TESTOBJS	:= $(patsubst $(TESTDIR)/%,$(TESTBUILD)/%,$(TESTS:.$(SRCEXT)=.$(OBJEXT)))

TESTCOVS	:= $(patsubst $(TESTDIR)/%,$(TESTBUILD)/%,$(TESTS:.$(SRCEXT)=.$(SRCEXT).$(COVEXT)))

test: $(TESTOBJS)
	@mkdir -p ./$(TESTTARGET)
	$(CC) $(TESTINC) ./$(TESTBUILD)/collections/*.o -o ./$(TESTTARGET)/collections $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/http/*.o -o ./$(TESTTARGET)/http $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/utils/*.o -o ./$(TESTTARGET)/utils $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/json/*.o -o ./$(TESTTARGET)/json $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/jwt/*.o -o ./$(TESTTARGET)/jwt $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/web/web.o ./$(TESTBUILD)/web/curl.o -o ./$(TESTTARGET)/web $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/web/api.o ./$(TESTBUILD)/web/curl.o -o ./$(TESTTARGET)/api $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/web/upload.o ./$(TESTBUILD)/web/curl.o -o ./$(TESTTARGET)/upload $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/jwt_encode.o ./$(TESTBUILD)/users.o -o ./$(TESTTARGET)/jwt_encode $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/jwt_decode.o ./$(TESTBUILD)/users.o -o ./$(TESTTARGET)/jwt_decode $(TESTLIBS)

# compile tests
$(TESTBUILD)/%.$(OBJEXT): $(TESTDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(TESTFLAGS) $(TESTINC) $(TESTLIBS) -c -o $@ $<
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
BENCHDIR	:= benchmarks
BENCHBUILD	:= $(BENCHDIR)/objs
BENCHTARGET	:= $(BENCHDIR)/bin

BENCHFLAGS	:= $(DEFINES) -Wall -Wno-unknown-pragmas -O3

ifeq ($(NATIVE), 1)
	BENCHFLAGS += -march=native -mavx2
endif

BENCHLIBS	:= $(PTHREAD) $(CURL) -L ./$(TARGETDIR) -l cerver
BENCHINC	:= -I $(INCDIR) -I ./$(BENCHDIR)

BENCHS		:= $(shell find $(BENCHDIR) -type f -name *.$(SRCEXT))
BENCHOBJS	:= $(patsubst $(BENCHDIR)/%,$(BENCHBUILD)/%,$(BENCHS:.$(SRCEXT)=.$(OBJEXT)))

bench: $(BENCHOBJS)
	@mkdir -p ./$(BENCHTARGET)
	$(CC) $(BENCHINC) ./$(BENCHBUILD)/base64.o -o ./$(BENCHTARGET)/base64 $(BENCHLIBS)
	$(CC) $(BENCHINC) ./$(BENCHBUILD)/http-parser.o -o ./$(BENCHTARGET)/http-parser $(BENCHLIBS)
	$(CC) $(BENCHINC) ./$(BENCHBUILD)/web.o ./$(BENCHBUILD)/curl.o -o ./$(BENCHTARGET)/web $(BENCHLIBS)

# compile benchmarks
$(BENCHBUILD)/%.$(OBJEXT): $(BENCHDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(BENCHFLAGS) $(INC) $(BENCHINC) $(BENCHLIBS) -c -o $@ $<
	@$(CC) $(BENCHFLAGS) $(INCDEP) -MM $(BENCHDIR)/$*.$(SRCEXT) > $(BENCHBUILD)/$*.$(DEPEXT)
	@cp -f $(BENCHBUILD)/$*.$(DEPEXT) $(BENCHBUILD)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(BENCHBUILD)/$*.$(OBJEXT):|' < $(BENCHBUILD)/$*.$(DEPEXT).tmp > $(BENCHBUILD)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(BENCHBUILD)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(BENCHBUILD)/$*.$(DEPEXT)
	@rm -f $(BENCHBUILD)/$*.$(DEPEXT).tmp

clear: clean-objects clean-examples clean-tests clean-coverage clean-bench

clean: clear
	@$(RM) -rf $(TARGETDIR)

clean-objects:
	@$(RM) -rf $(BUILDDIR)

clean-examples:
	@$(RM) -rf $(EXABUILD)
	@$(RM) -rf $(EXATARGET)

clean-tests:
	@$(RM) -rf $(TESTBUILD)
	@$(RM) -rf $(TESTTARGET)

clean-coverage:
	@$(RM) -rf $(COVDIR)

clean-bench:
	@$(RM) -rf $(BENCHBUILD)
	@$(RM) -rf $(BENCHTARGET)

.PHONY: all clean clear examples test coverage bench