##
## PIN tools
##

##############################################################
#
# Here are some things you might want to configure
#
##############################################################

#TARGET_COMPILER?=ms
TARGET_COMPILER?=gnu

##############################################################
#
# include *.config files
#
##############################################################

ifeq ($(TARGET_COMPILER),gnu)
    include $(PIN_HOME)/source/tools/makefile.gnu.config
    LINKER?=${CXX}
    CXXFLAGS ?= -Wall -Werror -Wno-unknown-pragmas $(DBG) $(OPT)
endif

ifeq ($(TARGET_COMPILER),ms)
    include ../makefile.ms.config
    DBG?=
endif

##############################################################
#
# Tools sets
#
##############################################################


TOOL_ROOTS = bpredictor
STATIC_TOOL_ROOTS =

# pinatrace and itrace currently hang cygwin gnu windows

TOOLS = $(TOOL_ROOTS:%=%$(PINTOOL_SUFFIX))
STATIC_TOOLS = $(STATIC_TOOL_ROOTS:%=%$(PINTOOL_SUFFIX))

##############################################################
#
# build rules
#
##############################################################

all: tools
tools: $(TOOLS) $(STATIC_TOOLS)
test: $(TOOL_ROOTS:%=%.test) $(STATIC_TOOL_ROOTS:%=%.test) 


# stand alone pin tool
statica.test: statica${PINTOOL_SUFFIX} statica.tested statica.failed statica
	./statica -i ./statica  > statica.dmp
	rm $<.failed statica.dmp

replacesigprobed.test : replacesigprobed$(PINTOOL_SUFFIX) replacesigprobed.tested replacesigprobed.failed
	$(PIN) -probe -t $< -- $(TESTAPP) makefile $<.makefile.copy >  $<.out 2>&1
	rm replacesigprobed.failed  $<.out $<.makefile.copy

## build rules

%.o : %.cpp
	$(CXX) -c $(CXXFLAGS) $(PIN_CXXFLAGS) ${OUTOPT}$@ $<
$(TOOLS): $(PIN_LIBNAMES)
$(TOOLS): %$(PINTOOL_SUFFIX) : %.o
	${LINKER} $(PIN_LDFLAGS) $(LINK_DEBUG) ${LINK_OUT}$@ $< ${PIN_LPATHS} $(PIN_LIBS) $(DBG)

$(STATIC_TOOLS): $(PIN_LIBNAMES)
$(STATIC_TOOLS): %$(PINTOOL_SUFFIX) : %.o
	${LINKER} $(PIN_LDFLAGS) $(LINK_DEBUG) ${LINK_OUT}$@ $< ${PIN_LPATHS} $(SAPIN_LIBS) $(DBG)

## cleaning
clean:
	-rm -f *.o $(STATIC_TOOLS) $(TOOLS) *.out *.tested *.failed *.d *.makefile.copy *.exp *.lib 

realclean:
	-rm -rf *.o $(STATIC_TOOLS) $(TOOLS) *.out *.tested *.failed *.d *.makefile.copy *.exp *.lib *results_* *.out
