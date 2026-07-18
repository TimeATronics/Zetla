# Toolchain
CXX := C:/Users/Aradhya/MyFiles/Applications/mingw64/bin/x86_64-w64-mingw32-g++.exe

SRC   := src
ZETLA := $(SRC)/zetla
VCPKG := build/vcpkg_installed/x64-mingw-dynamic
OUT   := build/src

# Flags
CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic -I$(SRC) -I$(VCPKG)/include -I$(ZETLA)/thirdparty -I$(ZETLA)/thirdparty/zip -I$(ZETLA)/route -I$(ZETLA)/protocols/openai_chat -I$(ZETLA)/network -I$(ZETLA)/tools -I$(ZETLA)/providers/opencode -I$(ZETLA)/providers/nvidia -I$(ZETLA)/providers/deepseek -I$(ZETLA)/thirdparty -I$(ZETLA)/thirdparty/zip -I$(VCPKG)/../nanopdf/src -I$(VCPKG)/../nanopdf/src/c -I$(VCPKG)/../nanopdf/src/third_party
LDFLAGS  := -L$(VCPKG)/lib -lcurl -lz -lbcrypt -liphlpapi

# Sources
DLL_SRCS := $(ZETLA)/search/exa_provider.cpp                  \
            $(ZETLA)/search/web_search_tool.cpp                \
            $(ZETLA)/session/session_manager.cpp              \
            $(ZETLA)/api/dll_api.cpp                          \
            $(ZETLA)/storage/local/file_storage_backend.cpp   \
            $(ZETLA)/storage/local/serialization.cpp          \
            $(ZETLA)/storage/local/encryption.cpp             \
            $(ZETLA)/storage/storage_manager.cpp              \
            $(ZETLA)/file_handlers/base/file_handler_factory.cpp \
            $(ZETLA)/file_handlers/text/text_file_handler.cpp    \
            $(ZETLA)/file_handlers/image/image_file_handler.cpp  \
            $(ZETLA)/file_handlers/pdf/pdf_file_handler.cpp      \
            $(ZETLA)/file_handlers/office/office_file_handler.cpp \
            $(ZETLA)/thirdparty/zip/zip.c

# Objects
DLL_OBJS := $(OUT)/search/exa_provider.o                   \
            $(OUT)/search/web_search_tool.o                \
            $(OUT)/session/session_manager.o               \
            $(OUT)/api/dll_api.o                           \
            $(OUT)/storage/local/file_storage_backend.o    \
            $(OUT)/storage/local/serialization.o           \
            $(OUT)/storage/local/encryption.o              \
            $(OUT)/storage/storage_manager.o               \
            $(OUT)/file_handlers/base/file_handler_factory.o \
            $(OUT)/file_handlers/text/text_file_handler.o    \
            $(OUT)/file_handlers/image/image_file_handler.o  \
            $(OUT)/file_handlers/pdf/pdf_file_handler.o      \
            $(OUT)/file_handlers/office/office_file_handler.o \
            $(OUT)/thirdparty/zip/zip.o

CLI_OBJ := $(OUT)/main_cli.o

# Targets
DLL        := $(OUT)/zetla.dll
DLL_IMPLIB := $(OUT)/libzetla.dll.a
CLI        := $(OUT)/zetla_cli.exe

# Auto-search for sources across dirs
VPATH := $(SRC) \
         $(ZETLA) \
         $(ZETLA)/search \
         $(ZETLA)/session \
         $(ZETLA)/api \
         $(ZETLA)/storage \
         $(ZETLA)/storage/base \
         $(ZETLA)/storage/local \
         $(ZETLA)/file_handlers/base \
         $(ZETLA)/file_handlers/text \
         $(ZETLA)/file_handlers/image \
         $(ZETLA)/file_handlers/pdf \
         $(ZETLA)/file_handlers/office \
         $(ZETLA)/thirdparty/zip

# Build
.PHONY: all clean dll cli dirs

all: $(CLI)

dll: $(DLL)

cli: $(CLI)

# Create output directories
dirs:
	@if not exist "$(OUT)" mkdir "$(OUT)"
	@if not exist "$(OUT)\\search" mkdir "$(OUT)\\search"
	@if not exist "$(OUT)\\session" mkdir "$(OUT)\\session"
	@if not exist "$(OUT)\\api" mkdir "$(OUT)\\api"
	@if not exist "$(OUT)\\storage\\local" mkdir "$(OUT)\\storage\\local"
	@if not exist "$(OUT)\\file_handlers\\base" mkdir "$(OUT)\\file_handlers\\base"
	@if not exist "$(OUT)\\file_handlers\\text" mkdir "$(OUT)\\file_handlers\\text"
	@if not exist "$(OUT)\\file_handlers\\image" mkdir "$(OUT)\\file_handlers\\image"
	@if not exist "$(OUT)\\file_handlers\\pdf" mkdir "$(OUT)\\file_handlers\\pdf"
	@if not exist "$(OUT)\\file_handlers\\office" mkdir "$(OUT)\\file_handlers\\office"
	@if not exist "$(OUT)\\thirdparty\\zip" mkdir "$(OUT)\\thirdparty\\zip"

# DLL + import library
$(DLL): $(DLL_OBJS) | dirs
	$(CXX) -shared -o $@ $(DLL_OBJS) $(LDFLAGS) -Wl,--out-implib,$(DLL_IMPLIB)

# CLI: compiles main_cli.cpp, links against DLL directly
$(CLI): $(CLI_OBJ) $(DLL) | dirs
	$(CXX) -o $@ $(CLI_OBJ) -L$(OUT) -lzetla $(LDFLAGS) -static-libgcc -static-libstdc++

# Generic compile rule (uses VPATH to find sources)
$(OUT)/%.o: %.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

# C compile rule for zip.c
$(OUT)/%.o: %.c | dirs
	$(CXX) -std=c11 -I$(SRC) -I$(ZETLA)/thirdparty/zip -c $< -o $@

# Clean
clean:
	-del /q build\src\*.o build\src\*.a build\src\zetla_cli.exe 2>nul
	-del /q build\src\search\*.o 2>nul
	-del /q build\src\session\*.o 2>nul
	-del /q build\src\api\*.o 2>nul
	-del /q build\src\storage\*.o 2>nul
	-del /q build\src\storage\local\*.o 2>nul
	-del /q build\src\file_handlers\base\*.o 2>nul
	-del /q build\src\file_handlers\text\*.o 2>nul
	-del /q build\src\file_handlers\image\*.o 2>nul
	-del /q build\src\file_handlers\pdf\*.o 2>nul
	-del /q build\src\file_handlers\office\*.o 2>nul
