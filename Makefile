.SUFFIXES : .c .o

#include tools.mk

TARGETDEV := topst-d3-g.local

LOCAL_DIR := $(shell pwd)

Q_		   := @
PKG_CONFIG ?= pkg-config

CXXFLAGS  += -fPIC -Wno-unused-function -Wno-unused-result
CFLAGS    += -fPIC -Wno-unused-function -Wno-unused-result
LDFLAGS   += -lpthread -ldl

OUT_DIR   := out
TARGET    := Test

INCDIRS   :=
SRCDIRS   :=
SRCS      :=


INCDIRS   += $(LOCAL_DIR)
SRCDIRS   += $(LOCAL_DIR)
SRCS      += Main.cpp

INCDIRS   += $(LOCAL_DIR)/common
SRCDIRS   += $(LOCAL_DIR)/common
SRCS      += Log.cpp
SRCS      += Timer.cpp
SRCS      += WorkerThread.cpp
SRCS      += TimerThread.cpp
SRCS      += MainLoop.cpp

###############################################################################
# DO NOT MODIFY .......
###############################################################################
APP           := $(OUT_DIR)/$(TARGET)
APP_OBJS      := $(SRCS:%=$(OUT_DIR)/%.o)
APP_DEPS      := $(APP_OBJS:.o=.d)
APP_CFLAGS    := $(CFLAGS) $(DEFINES) -MMD -MP
APP_CXXFLAGS  := $(CXXFLAGS) $(DEFINES) -MMD -MP
APP_CXXFLAGS  += $(addprefix -I, $(INCDIRS))
APP_CFLAGS    += $(addprefix -I, $(INCDIRS))
APP_LDFLAGS   := $(addprefix -L, $(LIBDIRS))
APP_LDFLAGS   += $(LDFLAGS)

vpath %.cpp $(SRCDIRS)
vpath %.c $(SRCDIRS)

.PHONY: all clean

all: $(OUT_DIR) app

app: $(APP_OBJS)
	@echo "[Linking... $(notdir $(APP))]"
	$(Q_)$(CXX) -o $(APP) $(APP_OBJS) $(APP_LDFLAGS)

clean:
	@echo "[Clean... all objs]"
	$(Q_)rm -rf $(OUT_DIR)

$(OUT_DIR):
	$(Q_)mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.c.o: %.c
	@echo "[Compile... $(notdir $<)]"
	$(Q_)$(CC) $(APP_CFLAGS) -c $< -o $@

$(OUT_DIR)/%.cpp.o: %.cpp
	@echo "[Compile... $(notdir $<)]"
	$(Q_)$(CXX) $(APP_CXXFLAGS) -c $< -o $@

-include $(APP_DEPS)

install: app
	@echo "[Install .... $(notdir $(APP))]"
	scp $(APP) root@$(TARGETDEV):/home/root/
