TYPE		:= development

NATIVE		:= 0

COVERAGE	:= 0

DEBUG		:= 0

# address, thread or undefined
SANITIZER	:=

# 1 compiles the gzip compression of the HTTP responses with zlib
ZLIB		:= 0

# the provider of the cryptography of cerver/crypto: openssl
CRYPTO		:= openssl

WERROR		?= 0

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

ZLIBLIB		:= -l z

CURL		:= -l curl

DEFINES		:= -D _GNU_SOURCE

ifeq ($(CRYPTO), openssl)
	DEFINES += -D CERVER_CRYPTO_OPENSSL
else
$(error the provider of the cryptography $(CRYPTO) is not supported)
endif

BASE_DEBUG	:= -D CERVER_DEBUG -D CERVER_STATS 				\
				-D CLIENT_DEBUG -D CLIENT_STATS 			\
				-D CONNECTION_DEBUG -D CONNECTION_STATS 	\
				-D HANDLER_DEBUG -D SOCKET_DEBUG			\
				-D PACKETS_DEBUG 							\
				-D AUTH_DEBUG 								\
				-D ADMIN_DEBUG								\
				-D FILES_DEBUG								\
				-D THREADS_DEBUG							\
				-D BALANCER_DEBUG							\
				-D SERVICE_DEBUG

HAND_DEBUG	:= -D HANDLER_DEBUG -D SOCKET_DEBUG
RECV_DEBUG	:= -D RECEIVE_DEBUG -D CLIENT_RECEIVE_DEBUG

EXTRA_DEBUG	:= $(HAND_DEBUG) $(RECV_DEBUG)

HTTP_DEBUG	:= -D HTTP_DEBUG -D HTTP_HEADERS_DEBUG			\
				-D HTTP_AUTH_DEBUG -D HTTP_MPART_DEBUG		\
				-D HTTP_RESPONSE_DEBUG						\
				-D HTTP_ADMIN_DEBUG

HTTP_EXTRA_DEBUG = HTTP_RECEIVE_DEBUG

DEVELOPMENT := $(BASE_DEBUG) $(HTTP_DEBUG)

CC          := gcc

GCCVGTEQ8 	:= $(shell expr `$(CC) -dumpversion | cut -f1 -d.` \>= 8)

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
				-Wuninitialized -Wempty-body \
				-Wunused-parameter -Wunused-but-set-parameter -Wunused-result \
				-Wformat -Wformat-nonliteral -Wformat-security -Wformat-overflow -Wformat-signedness -Wformat-truncation

ifeq ($(findstring clang,$(CC)),)
	COMMON += -Wmaybe-uninitialized
endif

ifeq ($(WERROR), 1)
	COMMON += -Werror
endif

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
	CFLAGS += -std=c++11
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

# sanitizers are applied to the library, the examples and the tests
ifneq ($(SANITIZER),)
	SANITIZERFLAGS := -fsanitize=$(SANITIZER) -fno-omit-frame-pointer
	RECYCLEFLAGS := -D CERVER_RECYCLE_DISABLED
	CFLAGS += $(RECYCLEFLAGS)
endif

# common flags
CFLAGS += -fPIC $(COMMON) $(SANITIZERFLAGS)

LIB         := -L /usr/local/lib $(PTHREAD) $(MATH) $(OPENSSL) $(SANITIZERFLAGS)

ifeq ($(ZLIB), 1)
	CFLAGS += -D CERVER_ZLIB
	LIB += $(ZLIBLIB)
endif

ifeq ($(TYPE), test)
	ifeq ($(COVERAGE), 1)
		LIB += -lgcov --coverage
	endif
endif

INC         := -I $(INCDIR) -I $(SRCDIR) -I /usr/local/include
INCDEP      := -I $(INCDIR) -I $(SRCDIR)

SOURCES     := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS     := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))

SRCCOVS		:= $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(SRCEXT).$(COVEXT)))

# pull in dependency info for *existing* .o files
-include $(OBJECTS:.$(OBJEXT)=.$(DEPEXT))

# the parser is internal, its objects never export their symbols:
# llhttp.c is generated and it defines its methods without a visibility
PARSEROBJS	:= $(filter $(BUILDDIR)/cerver/http/parser/%,$(OBJECTS))

$(PARSEROBJS): CFLAGS += -fvisibility=hidden

$(SLIB):
	$(MAKE) $(OBJECTS)
	$(CC) $(OBJECTS) $(LIB) -shared -o $(TARGETDIR)/$(SLIB)

# compile sources
$(BUILDDIR)/%.$(OBJEXT): $(SRCDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<
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
	EXAFLAGS += -std=c++11
else
	EXAFLAGS += -std=c11 -Wpedantic -pedantic-errors
endif

ifeq ($(NATIVE), 1)
	EXAFLAGS += -march=native
endif

# common flags
EXAFLAGS += $(COMMON) $(SANITIZERFLAGS)

EXALIBS		:= -Wl,-rpath=./$(TARGETDIR) -L ./$(TARGETDIR) -l cerver $(SANITIZERFLAGS)
EXAINC		:= -I ./$(INCDIR) -I ./$(EXAMDIR)

EXAMPLES	:= $(shell find $(EXAMDIR) -type f -name *.$(SRCEXT))
EXOBJS		:= $(patsubst $(EXAMDIR)/%,$(EXABUILD)/%,$(EXAMPLES:.$(SRCEXT)=.$(OBJEXT)))

# an example is compiled again when a header that it includes changes
-include $(EXOBJS:.$(OBJEXT)=.$(DEPEXT))

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
	EXAPPFGS += -std=c++11
else
	EXAPPFGS += -std=c11 -Wpedantic -pedantic-errors
endif

EXAPPFGS += $(COMMON) $(SANITIZERFLAGS)

EXAPPLIBS := -L /usr/local/lib -L ./$(TARGETDIR) -l cerver $(SANITIZERFLAGS)

EXAPPLINK	:= -Wl,-rpath=./$(EXATARGET)/app -L ./$(EXATARGET)/app -l app

exapp:
	@mkdir -p ./$(EXATARGET)/app
	$(CC) $(EXAPPFGS) -I $(INCDIR) $(EXAPPSRC) -shared -o $(EXAPP) $(EXAPPLIBS)

base: $(EXOBJS)
	$(CC) $(EXAINC) ./$(EXABUILD)/start.o -o ./$(EXATARGET)/start $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/welcome.o -o ./$(EXATARGET)/welcome $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/test.o -o ./$(EXATARGET)/test $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/handlers.o -o ./$(EXATARGET)/handlers $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/multi.o -o ./$(EXATARGET)/multi $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/threads.o -o ./$(EXATARGET)/threads $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/advanced.o -o ./$(EXATARGET)/advanced $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/requests.o -o ./$(EXATARGET)/requests $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/files.o -o ./$(EXATARGET)/files $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/auth.o -o ./$(EXATARGET)/auth $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/sessions.o -o ./$(EXATARGET)/sessions $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/admin.o -o ./$(EXATARGET)/admin $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/packets.o -o ./$(EXATARGET)/packets $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/logs.o -o ./$(EXATARGET)/logs $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/game.o -o ./$(EXATARGET)/game $(EXALIBS) $(EXAPPLINK)

# the load balancer and its service, with the app library
services: $(EXOBJS)
	$(CC) $(EXAINC) ./$(EXABUILD)/balancer.o -o ./$(EXATARGET)/balancer $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/service.o -o ./$(EXATARGET)/service $(EXALIBS) $(EXAPPLINK)

client: $(EXOBJS)
	@mkdir -p ./$(EXATARGET)/client
	$(CC) $(EXAINC) ./$(EXABUILD)/client/client.o -o ./$(EXATARGET)/client/client $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/client/auth.o -o ./$(EXATARGET)/client/auth $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/client/files.o -o ./$(EXATARGET)/client/files $(EXALIBS) $(EXAPPLINK)
	$(CC) $(EXAINC) ./$(EXABUILD)/client/game.o -o ./$(EXATARGET)/client/game $(EXALIBS) $(EXAPPLINK)

web: $(EXOBJS)
	@mkdir -p ./$(EXATARGET)/web
	$(CC) $(EXAINC) ./$(EXABUILD)/web/admin.o -o ./$(EXATARGET)/web/admin $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/api.o ./$(EXABUILD)/users.o ./$(EXABUILD)/web/users.o -o ./$(EXATARGET)/web/api $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/auth.o ./$(EXABUILD)/users.o -o ./$(EXATARGET)/web/auth $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/jobs.o -o ./$(EXATARGET)/web/jobs $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/json.o -o ./$(EXATARGET)/web/json $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/multiple.o -o ./$(EXATARGET)/web/multiple $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/upload.o -o ./$(EXATARGET)/web/upload $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/videos.o -o ./$(EXATARGET)/web/videos $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/web.o -o ./$(EXATARGET)/web/web $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/websocket.o -o ./$(EXATARGET)/web/websocket $(EXALIBS)
	$(CC) $(EXAINC) ./$(EXABUILD)/web/worker.o ./$(EXABUILD)/data.o -o ./$(EXATARGET)/web/worker $(EXALIBS)

examples: $(EXOBJS)
	@mkdir -p ./$(EXATARGET)
	$(MAKE) $(EXOBJS)
	$(MAKE) exapp
	$(MAKE) base
	$(MAKE) services
	$(MAKE) client
	$(MAKE) web

# compile examples
$(EXABUILD)/%.$(OBJEXT): $(EXAMDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(EXADEBUG) $(EXAFLAGS) $(INC) $(EXAINC) -c -o $@ $<
	@$(CC) $(EXADEBUG) $(EXAFLAGS) $(INCDEP) $(EXAINC) -MM $(EXAMDIR)/$*.$(SRCEXT) > $(EXABUILD)/$*.$(DEPEXT)
	@cp -f $(EXABUILD)/$*.$(DEPEXT) $(EXABUILD)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(EXABUILD)/$*.$(OBJEXT):|' < $(EXABUILD)/$*.$(DEPEXT).tmp > $(EXABUILD)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(EXABUILD)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(EXABUILD)/$*.$(DEPEXT)
	@rm -f $(EXABUILD)/$*.$(DEPEXT).tmp

# tests
TESTDIR		:= test
TESTBUILD	:= $(TESTDIR)/objs
TESTTARGET	:= $(TESTDIR)/bin
TESTCOVDIR	:= $(COVDIR)/test

TESTFLAGS	:= -g $(DEFINES) $(COMMON) $(SANITIZERFLAGS) $(RECYCLEFLAGS)

ifeq ($(TYPE), test)
	ifeq ($(COVERAGE), 1)
		TESTFLAGS += -fprofile-arcs -ftest-coverage
	endif
endif

ifeq ($(NATIVE), 1)
	TESTFLAGS += -march=native
endif

TESTZLIB	:=

ifeq ($(ZLIB), 1)
	TESTFLAGS += -D CERVER_ZLIB
	TESTZLIB += $(ZLIBLIB)
endif

TESTLIBS	:= -L /usr/local/lib $(PTHREAD) $(SANITIZERFLAGS)

TESTLIBS += -Wl,-rpath=./$(TARGETDIR) -L ./$(TARGETDIR) -l cerver

ifeq ($(TYPE), test)
	ifeq ($(COVERAGE), 1)
		TESTLIBS += -lgcov --coverage
	endif
endif

TESTINC		:= -I $(INCDIR) -I $(SRCDIR) -I ./$(TESTDIR)

TESTS		:= $(shell find $(TESTDIR) -type f -name *.$(SRCEXT) -not -path '$(TESTDIR)/fuzz/*')
TESTOBJS	:= $(patsubst $(TESTDIR)/%,$(TESTBUILD)/%,$(TESTS:.$(SRCEXT)=.$(OBJEXT)))

# a test is compiled again when a header that it includes changes
-include $(TESTOBJS:.$(OBJEXT)=.$(DEPEXT))

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
	TESTAPPFGS += -std=c++11
else
	TESTAPPFGS += -std=c11 -Wpedantic -pedantic-errors
endif

TESTAPPFGS += $(COMMON) $(SANITIZERFLAGS)

TESTAPPLIBS := -L /usr/local/lib -Wl,-rpath=./$(TARGETDIR) -L ./$(TARGETDIR) -l cerver $(SANITIZERFLAGS)

TESTAPPLIB	:= -Wl,-rpath=./$(TESTTARGET)/app -L ./$(TESTTARGET)/app -l app

testapp:
	@mkdir -p ./$(TESTTARGET)/app
	$(CC) $(TESTAPPFGS) -I $(INCDIR) $(TESTAPPSRC) -shared -o $(TESTAPP) $(TESTAPPLIBS)

units: testout $(TESTOBJS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/balancer.o -o ./$(TESTTARGET)/balancer $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/routing.o -o ./$(TESTTARGET)/routing $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/cerver/test.o -o ./$(TESTTARGET)/cerver/test $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/cerver/core.o -o ./$(TESTTARGET)/cerver/core $(TESTLIBS) $(TESTAPPLIB)
	$(CC) $(TESTINC) ./$(TESTBUILD)/cerver/lobby.o -o ./$(TESTTARGET)/cerver/lobby $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/cerver/peers.o -o ./$(TESTTARGET)/cerver/peers $(TESTLIBS) $(TESTAPPLIB)
	$(CC) $(TESTINC) ./$(TESTBUILD)/game/*.o -o ./$(TESTTARGET)/game $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/client/test.o -o ./$(TESTTARGET)/client/test $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/client/lifecycle.o -o ./$(TESTTARGET)/client/lifecycle $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/client/features.o -o ./$(TESTTARGET)/client/features $(TESTLIBS) $(TESTAPPLIB)
	$(CC) $(TESTINC) ./$(TESTBUILD)/client/files.o -o ./$(TESTTARGET)/client/files $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/connection.o -o ./$(TESTTARGET)/connection $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/network.o -o ./$(TESTTARGET)/network $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/socket.o -o ./$(TESTTARGET)/socket $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/collections/*.o -o ./$(TESTTARGET)/collections $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/crypto/*.o -o ./$(TESTTARGET)/crypto $(TESTLIBS)
ifeq ($(SANITIZER),)
	$(CC) $(TESTINC) ./$(TESTBUILD)/faults/*.o ./$(TESTBUILD)/websockets/raw.o -o ./$(TESTTARGET)/faults $(TESTLIBS)
endif
	$(CC) $(TESTINC) ./$(TESTBUILD)/http/*.o -o ./$(TESTTARGET)/http $(TESTLIBS) $(TESTAPPLIB)
	$(CC) $(TESTINC) ./$(TESTBUILD)/files.o -o ./$(TESTTARGET)/files $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/json/*.o -o ./$(TESTTARGET)/json $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/jwt/*.o -o ./$(TESTTARGET)/jwt $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/packets.o -o ./$(TESTTARGET)/packets $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/receive.o -o ./$(TESTTARGET)/receive $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/system.o -o ./$(TESTTARGET)/system $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/threads/*.o -o ./$(TESTTARGET)/threads $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/types/*.o -o ./$(TESTTARGET)/types $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/udp.o -o ./$(TESTTARGET)/udp $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/utils/*.o -o ./$(TESTTARGET)/utils $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/version.o -o ./$(TESTTARGET)/version $(TESTLIBS)
	$(CC) $(TESTINC) ./$(TESTBUILD)/websockets/*.o -o ./$(TESTTARGET)/websockets $(TESTLIBS) $(TESTZLIB)

INTCERVERIN		:= ./$(TESTBUILD)/cerver
INTCERVEROUT	:= ./$(TESTTARGET)/cerver
INTCERVERLIBS	:= $(TESTLIBS) -Wl,-rpath=./$(TESTTARGET)/app -L ./$(TESTTARGET)/app -l app

integration-cerver:
	$(CC) $(TESTINC) $(INTCERVERIN)/auth.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/auth $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/connections.o -o $(INTCERVEROUT)/connections $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/game.o -o $(INTCERVEROUT)/game $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/load.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/load $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/packets.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/packets $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/ping.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/ping $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/queue.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/queue $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/requests.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/requests $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/schemas.o -o $(INTCERVEROUT)/schemas $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/service.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/service $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/sessions.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/sessions $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/stress.o -o $(INTCERVEROUT)/stress $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/threads.o $(INTCERVERIN)/cerver.o -o $(INTCERVEROUT)/threads $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/udp.o -o $(INTCERVEROUT)/udp $(INTCERVERLIBS)
	$(CC) $(TESTINC) $(INTCERVERIN)/upstream.o -o $(INTCERVEROUT)/upstream $(TESTLIBS)

INTCLIENTIN		:= ./$(TESTBUILD)/client
INTCLIENTOUT	:= ./$(TESTTARGET)/client
INTCLIENTLIBS	:= $(TESTLIBS) -Wl,-rpath=./$(TESTTARGET)/app -L ./$(TESTTARGET)/app -l app

integration-client:
	$(CC) $(TESTINC) $(INTCLIENTIN)/auth.o $(INTCLIENTIN)/client.o -o $(INTCLIENTOUT)/auth $(INTCLIENTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/connections.o -o $(INTCLIENTOUT)/connections $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/game.o -o $(INTCLIENTOUT)/game $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/load/abandon.o $(INTCLIENTIN)/load/balancer.o -o $(INTCLIENTOUT)/load/abandon $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/load/concurrent.o $(INTCLIENTIN)/load/balancer.o -o $(INTCLIENTOUT)/load/concurrent $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/load/id.o -o $(INTCLIENTOUT)/load/id $(INTCLIENTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/load/large.o $(INTCLIENTIN)/load/balancer.o -o $(INTCLIENTOUT)/load/large $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/load/reconnect.o $(INTCLIENTIN)/load/balancer.o -o $(INTCLIENTOUT)/load/reconnect $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/load/simple.o -o $(INTCLIENTOUT)/load/simple $(INTCLIENTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/load/unavailable.o $(INTCLIENTIN)/load/balancer.o -o $(INTCLIENTOUT)/load/unavailable $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/packets.o -o $(INTCLIENTOUT)/packets $(INTCLIENTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/ping.o -o $(INTCLIENTOUT)/ping $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/queue.o -o $(INTCLIENTOUT)/queue $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/requests.o -o $(INTCLIENTOUT)/requests $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/schemas.o -o $(INTCLIENTOUT)/schemas $(INTCLIENTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/sessions.o $(INTCLIENTIN)/client.o -o $(INTCLIENTOUT)/sessions $(INTCLIENTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/stress.o -o $(INTCLIENTOUT)/stress $(TESTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/threads.o -o $(INTCLIENTOUT)/threads $(INTCLIENTLIBS)
	$(CC) $(TESTINC) $(INTCLIENTIN)/udp.o -o $(INTCLIENTOUT)/udp $(INTCLIENTLIBS)

INTWEBIN		:= ./$(TESTBUILD)/web
INTWEBOUT		:= ./$(TESTTARGET)/web
INTWEBLIBS		:= $(TESTLIBS) $(TESTAPPLIB)

integration-web:
	@mkdir -p ./$(TESTTARGET)/web
	$(CC) $(TESTINC) $(INTWEBIN)/admin.o -o $(INTWEBOUT)/admin $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/api.o -o $(INTWEBOUT)/api $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/autobahn.o -o $(INTWEBOUT)/autobahn $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/auth.o -o $(INTWEBOUT)/auth $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/disconnect.o -o $(INTWEBOUT)/disconnect $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/features.o -o $(INTWEBOUT)/features $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/jobs.o -o $(INTWEBOUT)/jobs $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/json.o -o $(INTWEBOUT)/json $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/multi.o -o $(INTWEBOUT)/multi $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/multiple.o -o $(INTWEBOUT)/multiple $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/router.o -o $(INTWEBOUT)/router $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/upload.o -o $(INTWEBOUT)/upload $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/web.o -o $(INTWEBOUT)/web $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/websocket.o -o $(INTWEBOUT)/websocket $(INTWEBLIBS)
	$(CC) $(TESTINC) $(INTWEBIN)/worker.o -o $(INTWEBOUT)/worker $(INTWEBLIBS)

INTWEBCLIENTIN		:= ./$(TESTBUILD)/client/web
INTWEBCLIENTOUT		:= ./$(TESTTARGET)/client/web
INTWEBCLIENTLIBS	:= $(TESTLIBS) $(CURL)

integration-web-client:
	@mkdir -p ./$(TESTTARGET)/client/web
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/admin.o $(INTWEBCLIENTIN)/curl.o -o $(INTWEBCLIENTOUT)/admin $(INTWEBCLIENTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/api.o $(INTWEBCLIENTIN)/curl.o -o $(INTWEBCLIENTOUT)/api $(INTWEBCLIENTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/auth.o $(INTWEBCLIENTIN)/curl.o -o $(INTWEBCLIENTOUT)/auth $(INTWEBCLIENTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/autobahn.o -o $(INTWEBCLIENTOUT)/autobahn $(TESTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/disconnect.o -o $(INTWEBCLIENTOUT)/disconnect $(TESTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/features.o $(INTWEBCLIENTIN)/curl.o -o $(INTWEBCLIENTOUT)/features $(INTWEBCLIENTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/jobs.o $(INTWEBCLIENTIN)/curl.o -o $(INTWEBCLIENTOUT)/jobs $(INTWEBCLIENTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/json.o $(INTWEBCLIENTIN)/curl.o -o $(INTWEBCLIENTOUT)/json $(INTWEBCLIENTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/multi.o $(INTWEBCLIENTIN)/curl.o -o $(INTWEBCLIENTOUT)/multi $(INTWEBCLIENTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/multiple.o $(INTWEBCLIENTIN)/curl.o -o $(INTWEBCLIENTOUT)/multiple $(INTWEBCLIENTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/router.o $(INTWEBCLIENTIN)/curl.o -o $(INTWEBCLIENTOUT)/router $(INTWEBCLIENTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/upload.o $(INTWEBCLIENTIN)/curl.o -o $(INTWEBCLIENTOUT)/upload $(INTWEBCLIENTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/web.o $(INTWEBCLIENTIN)/curl.o -o $(INTWEBCLIENTOUT)/web $(INTWEBCLIENTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/websocket.o -o $(INTWEBCLIENTOUT)/websocket $(TESTLIBS)
	$(CC) $(TESTINC) $(INTWEBCLIENTIN)/worker.o $(INTWEBCLIENTIN)/curl.o -o $(INTWEBCLIENTOUT)/worker $(INTWEBCLIENTLIBS)

integration: testout $(TESTOBJS)
	$(MAKE) integration-cerver
	$(MAKE) integration-client
	$(MAKE) integration-web
	$(MAKE) integration-web-client

TESTHANDLERIN	:= ./$(TESTBUILD)/handler
TESTHANDLEROUT	:= ./$(TESTTARGET)/handler
TESTHANDLERLIBS	:= $(TESTLIBS) -Wl,-rpath=./$(TESTTARGET)/app -L ./$(TESTTARGET)/app -l app

testhandler: testout $(TESTOBJS)
	@mkdir -p ./$(TESTTARGET)/handler
	$(CC) $(TESTINC) $(TESTHANDLERIN)/cerver.o -o $(TESTHANDLEROUT)/cerver $(TESTHANDLERLIBS)
	$(CC) $(TESTINC) $(TESTHANDLERIN)/client.o -o $(TESTHANDLEROUT)/client $(TESTHANDLERLIBS)

testout:
	@mkdir -p ./$(TESTTARGET)
	@mkdir -p ./$(TESTTARGET)/cerver
	@mkdir -p ./$(TESTTARGET)/client
	@mkdir -p ./$(TESTTARGET)/client/load
	@mkdir -p ./$(TESTTARGET)/handler

test: testout
	$(MAKE) $(TESTOBJS)
	$(MAKE) testapp
	$(MAKE) units
	$(MAKE) integration
	$(MAKE) testhandler

# compile tests
$(TESTBUILD)/%.$(OBJEXT): $(TESTDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(TESTFLAGS) $(TESTINC) -c -o $@ $<
	@$(CC) $(TESTFLAGS) $(TESTINC) -MM $(TESTDIR)/$*.$(SRCEXT) > $(TESTBUILD)/$*.$(DEPEXT)
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
	if [ -f $(notdir $@) ]; then mv $(notdir $@) ./$(COVDIR)/$<.gcov; fi

# get tests coverage reports
$(TESTBUILD)/%.$(SRCEXT).$(COVEXT): $(TESTDIR)/%.$(SRCEXT)
	gcov -r $< --object-directory $(dir $@)
	if [ -f $(notdir $@) ]; then mv $(notdir $@) ./$(TESTCOVDIR); fi

# benchmarks
BENCHDIR	:= benchmarks
BENCHBUILD	:= $(BENCHDIR)/objs
BENCHTARGET	:= $(BENCHDIR)/bin

BENCHFLAGS	:= $(DEFINES) $(COMMON) -O3

ifeq ($(NATIVE), 1)
	BENCHFLAGS += -march=native
endif

BENCHLIBS	:= $(PTHREAD) $(CURL) -Wl,-rpath=./$(TARGETDIR) -L ./$(TARGETDIR) -l cerver
BENCHINC	:= -I $(INCDIR) -I ./$(BENCHDIR)

BENCHS		:= $(shell find $(BENCHDIR) -type f -name *.$(SRCEXT))
BENCHOBJS	:= $(patsubst $(BENCHDIR)/%,$(BENCHBUILD)/%,$(BENCHS:.$(SRCEXT)=.$(OBJEXT)))

# a benchmark is compiled again when a header that it includes changes
-include $(BENCHOBJS:.$(OBJEXT)=.$(DEPEXT))

bench: $(BENCHOBJS)
	@mkdir -p ./$(BENCHTARGET)
	$(CC) $(BENCHINC) ./$(BENCHBUILD)/base64.o -o ./$(BENCHTARGET)/base64 $(BENCHLIBS)
	$(CC) $(BENCHINC) ./$(BENCHBUILD)/frames.o -o ./$(BENCHTARGET)/frames $(BENCHLIBS)
	$(CC) $(BENCHINC) ./$(BENCHBUILD)/handler.o -o ./$(BENCHTARGET)/handler $(BENCHLIBS)
	$(CC) $(BENCHINC) ./$(BENCHBUILD)/http-parser.o $(PARSEROBJS) -o ./$(BENCHTARGET)/http-parser $(BENCHLIBS)
	$(CC) $(BENCHINC) ./$(BENCHBUILD)/web.o ./$(BENCHBUILD)/curl.o -o ./$(BENCHTARGET)/web $(BENCHLIBS)

# matches real routes with the matcher of 2.0b-58 and with the routes tree
tree: $(BENCHBUILD)/tree.$(OBJEXT)
	@mkdir -p ./$(BENCHTARGET)
	$(CC) $(BENCHINC) ./$(BENCHBUILD)/tree.o -o ./$(BENCHTARGET)/tree $(BENCHLIBS)

# a web server and the load generators of its requests and its WebSocket messages, in any architecture
load: $(BENCHBUILD)/server.$(OBJEXT) $(BENCHBUILD)/load.$(OBJEXT) $(BENCHBUILD)/messages.$(OBJEXT)
	@mkdir -p ./$(BENCHTARGET)
	$(CC) $(BENCHINC) ./$(BENCHBUILD)/server.o -o ./$(BENCHTARGET)/server $(BENCHLIBS)
	$(CC) $(BENCHINC) ./$(BENCHBUILD)/load.o -o ./$(BENCHTARGET)/load $(PTHREAD)
	$(CC) $(BENCHINC) ./$(BENCHBUILD)/messages.o -o ./$(BENCHTARGET)/messages $(PTHREAD)

# compile benchmarks
$(BENCHBUILD)/%.$(OBJEXT): $(BENCHDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(BENCHFLAGS) $(INC) $(BENCHINC) -c -o $@ $<
	@$(CC) $(BENCHFLAGS) $(INCDEP) $(BENCHINC) -MM $(BENCHDIR)/$*.$(SRCEXT) > $(BENCHBUILD)/$*.$(DEPEXT)
	@cp -f $(BENCHBUILD)/$*.$(DEPEXT) $(BENCHBUILD)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(BENCHBUILD)/$*.$(OBJEXT):|' < $(BENCHBUILD)/$*.$(DEPEXT).tmp > $(BENCHBUILD)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(BENCHBUILD)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(BENCHBUILD)/$*.$(DEPEXT)
	@rm -f $(BENCHBUILD)/$*.$(DEPEXT).tmp

# fuzzing with libFuzzer: make TYPE=test CC=clang fuzz
FUZZDIR			:= $(TESTDIR)/fuzz
FUZZBUILD		:= $(BUILDDIR)/fuzz
FUZZTARGET		:= $(TESTTARGET)/fuzz

FUZZSANITIZERS	:= address,undefined
FUZZRECOVER		:= -fno-sanitize-recover=undefined
FUZZLIBFLAGS	:= -g -O1 $(DEFINES) $(COMMON) -D CERVER_RECYCLE_DISABLED -fno-omit-frame-pointer -fsanitize=fuzzer-no-link,$(FUZZSANITIZERS) $(FUZZRECOVER)
FUZZFLAGS		:= -g -O1 $(DEFINES) $(COMMON) -fno-omit-frame-pointer -fsanitize=fuzzer,$(FUZZSANITIZERS) $(FUZZRECOVER)
FUZZLIBS		:= $(PTHREAD) $(MATH) $(OPENSSL)
FUZZINC			:= -I $(INCDIR) -I $(SRCDIR) -I ./$(TESTDIR)

ifeq ($(ZLIB), 1)
	FUZZLIBFLAGS += -D CERVER_ZLIB
	FUZZLIBS += $(ZLIBLIB)
endif

FUZZERS			:= $(shell find $(FUZZDIR) -maxdepth 1 -type f -name *.$(SRCEXT) 2> /dev/null)
FUZZLIBOBJS		:= $(patsubst $(SRCDIR)/%,$(FUZZBUILD)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))

fuzz: $(FUZZLIBOBJS)
	@mkdir -p ./$(FUZZTARGET)
	$(foreach fuzzer,$(FUZZERS),$(CC) $(FUZZFLAGS) $(FUZZINC) $(fuzzer) $(FUZZLIBOBJS) -o ./$(FUZZTARGET)/$(basename $(notdir $(fuzzer))) $(FUZZLIBS) || exit 1;)

$(FUZZBUILD)/%.$(OBJEXT): $(SRCDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(FUZZLIBFLAGS) $(INC) -c -o $@ $<

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

.PHONY: all clean clear examples test coverage bench fuzz
