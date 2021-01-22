TYPE		:= development

NATIVE		:= 0

COVERAGE	:= 0

DEBUG		:= 0

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

DEFINES		:= -D _GNU_SOURCE

BASE_DEBUG	:= -D CERVER_DEBUG -D CERVER_STATS 				\
				-D CLIENT_DEBUG -D CLIENT_STATS 			\
				-D CONNECTION_DEBUG -D CONNECTION_STATS 	\
				-D PACKETS_DEBUG 							\
				-D AUTH_DEBUG 								\
				-D ADMIN_DEBUG								\
				-D FILES_DEBUG								\
				-D BALANCER_DEBUG							\
				-D SERVICE_DEBUG

HAND_DEBUG	:= -D HANDLER_DEBUG -D SOCKET_DEBUG
RECV_DEBUG	:= -D RECEIVE_DEBUG -D CLIENT_RECEIVE_DEBUG

EXTRA_DEBUG	:= $(HAND_DEBUG) $(RECV_DEBUG)

DEVELOPMENT := $(BASE_DEBUG)

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
	ifeq ($(COVERAGE), 1)
		CFLAGS += -fprofile-arcs -ftest-coverage
	endif
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

LIB         := -L /usr/local/lib $(PTHREAD) $(MATH)

ifeq ($(TYPE), test)
	ifeq ($(COVERAGE), 1)
		LIB += -lgcov --coverage
	endif
endif

INC         := -I $(INCDIR) -I /usr/local/include
INCDEP      := -I $(INCDIR)

SOURCES     := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS     := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))

SRCCOVS		:= $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(SRCEXT).$(COVEXT)))

# pull in dependency info for *existing* .o files
-include $(OBJECTS:.$(OBJEXT)=.$(DEPEXT))

$(SLIB):
	$(MAKE) $(OBJECTS)
	$(CC) $(OBJECTS) $(LIB) -shared -o $(TARGETDIR)/$(SLIB)

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

EXALIBS		:= -Wl,-rpath=./$(TARGETDIR) -L ./$(TARGETDIR) -l cerver
EXAINC		:= -I ./$(INCDIR) -I ./$(EXAMDIR)

EXAMPLES	:= $(shell find $(EXAMDIR) -type f -name *.$(SRCEXT))
EXOBJS		:= $(patsubst $(EXAMDIR)/%,$(EXABUILD)/%,$(EXAMPLES:.$(SRCEXT)=.$(OBJEXT)))

EXAPP		:= ./$(EXATARGET)/app/libapp.so
EXAPPSRC  	:= $(shell find $(EXAMDIR)/app -type f -name *.$(SRCEXT))

EXAPPFGS	:= $(DEFINES) -D_FORTIFY_SOURCE=2 -O2 -fPIC

ifeq ($(TYPE), development)
	EXAPPFGS += -g
endif

ifeq ($(DEBUG), 1)
	EXAPPFGS += -D EXAMPLE_APP_DEBUG
endif

# check which compiler we are using
ifeq ($(CC), g++) 
	EXAPPFGS += -std=c++11 -fpermissive
else
	EXAPPFGS += -std=c11 -Wpedantic -pedantic-errors
endif

EXAPPFGS += $(COMMON)

EXAPPLIBS := -L /usr/local/lib -L ./$(TARGETDIR) -l cerver

exapp:
	@mkdir -p ./$(EXATARGET)/app
	$(CC) $(TESTAPPFGS) -I $(INCDIR) $(TESTAPPSRC) -shared -o $(TESTAPP) $(TESTAPPLIBS)

base: $(EXOBJS)
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

load: $(EXOBJS)
	$(CC) $(EXAINC) ./$(EXABUILD)/balancer.o -o ./$(EXATARGET)/balancer $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/service.o -o ./$(EXATARGET)/service $(EXALIBS) -Wl,-rpath=./$(TESTTARGET)/app

client: $(EXOBJS)
	@mkdir -p ./$(EXATARGET)/client
	$(CC) $(EXAINC) ./$(EXABUILD)/client/client.o -o ./$(EXATARGET)/client/client $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/client/auth.o -o ./$(EXATARGET)/client/auth $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/client/files.o -o ./$(EXATARGET)/client/files $(EXALIBS)

examples:
	@mkdir -p ./$(EXATARGET)
	$(MAKE) $(EXOBJS)
	$(MAKE) exapp
	$(MAKE) base
	$(MAKE) load
	$(MAKE) client

# compile examples
$(EXABUILD)/%.$(OBJEXT): $(EXAMDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(EXAFLAGS) $(INC) $(EXAINC) $(EXALIBS) -c -o $@ $<
	@$(CC) $(EXAFLAGS) $(INCDEP) -MM $(EXAMDIR)/$*.$(SRCEXT) > $(EXABUILD)/$*.$(DEPEXT)
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
	ifeq ($(COVERAGE), 1)
		TESTFLAGS += -fprofile-arcs -ftest-coverage
	endif
endif

ifeq ($(NATIVE), 1)
	TESTFLAGS += -march=native
endif

TESTLIBS	:= -L /usr/local/lib $(PTHREAD)

TESTLIBS += -Wl,-rpath=./$(TARGETDIR) -L ./$(TARGETDIR) -l cerver

ifeq ($(TYPE), test)
	ifeq ($(COVERAGE), 1)
		TESTLIBS += -lgcov --coverage
	endif
endif

TESTINC		:= -I $(INCDIR) -I ./$(TESTDIR)

TESTS		:= $(shell find $(TESTDIR) -type f -name *.$(SRCEXT))
TESTOBJS	:= $(patsubst $(TESTDIR)/%,$(TESTBUILD)/%,$(TESTS:.$(SRCEXT)=.$(OBJEXT)))

TESTCOVS	:= $(patsubst $(TESTDIR)/%,$(TESTBUILD)/%,$(TESTS:.$(SRCEXT)=.$(SRCEXT).$(COVEXT)))

TESTAPP		:= ./$(TESTTARGET)/app/libapp.so
TESTAPPSRC  := $(shell find $(TESTDIR)/app -type f -name *.$(SRCEXT))

TESTAPPFGS	:= $(DEFINES) -D_FORTIFY_SOURCE=2 -O2 -fPIC

ifeq ($(TYPE), development)
	TESTAPPFGS += -g
endif

ifeq ($(DEBUG), 1)
	TESTAPPFGS += -D TEST_APP_DEBUG
endif

# check which compiler we are using
ifeq ($(CC), g++) 
	TESTAPPFGS += -std=c++11 -fpermissive
else
	TESTAPPFGS += -std=c11 -Wpedantic -pedantic-errors
endif

TESTAPPFGS += $(COMMON)

TESTAPPLIBS := -L /usr/local/lib -L ./$(TARGETDIR) -l cerver

testapp:
	@mkdir -p ./$(TESTTARGET)/app
	$(CC) $(TESTAPPFGS) -I $(INCDIR) $(TESTAPPSRC) -shared -o $(TESTAPP) $(TESTAPPLIBS)

units: testout $(TESTOBJS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/cerver/test.o -o ./$(TESTTARGET)/cerver/test $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/client/test.o -o ./$(TESTTARGET)/client/test $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/connection.o -o ./$(TESTTARGET)/connection $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/collections/*.o -o ./$(TESTTARGET)/collections $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/threads/*.o -o ./$(TESTTARGET)/threads $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/packets.o -o ./$(TESTTARGET)/packets $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/utils/*.o -o ./$(TESTTARGET)/utils $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/version.o -o ./$(TESTTARGET)/version $(TESTLIBS)

INTCERVERIN		:= ./$(TESTBUILD)/cerver
INTCERVEROUT	:= ./$(TESTTARGET)/cerver
INTCERVERLIBS	:= $(TESTLIBS) -Wl,-rpath=./$(TESTTARGET)/app -L ./$(TESTTARGET)/app -l app

integration-cerver:
	$(CC) $(TESTINC) $(INTCERVERIN)/auth.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/auth $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/packets.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/packets $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/ping.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/ping $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/sessions.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/sessions $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/threads.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/threads $(INTCERVERLIBS)

INTCLIENTIN		:= ./$(TESTBUILD)/client
INTCLIENTOUT	:= ./$(TESTTARGET)/client
INTCLIENTLIBS	:= $(TESTLIBS) -Wl,-rpath=./$(TESTTARGET)/app -L ./$(TESTTARGET)/app -l app

integration-client:
	$(CC) $(TESTINC) $(INTCLIENTIN)/auth.o $(INTCLIENTIN)/client.o -o $(INTCLIENTOUT)/auth $(INTCLIENTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/packets.o -o $(INTCLIENTOUT)/packets $(INTCLIENTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/ping.o -o $(INTCLIENTOUT)/ping $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/sessions.o $(INTCLIENTIN)/client.o -o $(INTCLIENTOUT)/sessions $(INTCLIENTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/threads.o -o $(INTCLIENTOUT)/threads $(INTCLIENTLIBS)

integration: testout $(TESTOBJS)
	$(MAKE) integration-cerver
	$(MAKE) integration-client

TESTHANDLERIN	:= ./$(TESTBUILD)/handler
TESTHANDLEROUT	:= ./$(TESTTARGET)/handler
TESTHANDLERLIBS	:= $(TESTLIBS) -Wl,-rpath=./$(TESTTARGET)/app -L ./$(TESTTARGET)/app -l app

testhandler: testout $(TESTOBJS)
	$(CC) $(TESTINC) $(TESTHANDLERIN)/cerver.o -o $(TESTHANDLEROUT)/cerver $(TESTHANDLERLIBS)
	$(CC) $(TESTINC) $(TESTHANDLERIN)/client.o -o $(TESTHANDLEROUT)/client $(TESTHANDLERLIBS)

testout:
	@mkdir -p ./$(TESTTARGET)
	@mkdir -p ./$(TESTTARGET)/cerver
	@mkdir -p ./$(TESTTARGET)/client
	@mkdir -p ./$(TESTTARGET)/handler

test: testout
	$(MAKE) $(TESTOBJS)
	$(MAKE) units
	$(MAKE) testapp
	$(MAKE) integration
	$(MAKE) testhandler

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

clear: clean-objects clean-examples clean-tests clean-coverage

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

.PHONY: all clean clear examples test coverage