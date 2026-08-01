# hgnfs Makefile - BEIR benchmark (all code in src/zetla/rag/)
# Build: mingw32-make build/src/beir_bench.exe
# Run:   build/src/beir_bench.exe beir_bench/scifact
#        build/src/beir_bench.exe --all

CXX      := g++
VCPKG    := vcpkg_installed/x64-mingw-dynamic
SRC      := src
RAG      := $(SRC)/zetla/rag
OUT      := build/src

CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic -O3 -march=native -ffast-math -DHGNFS_STATIC -fopenmp
CXXFLAGS += -I$(RAG) -I$(RAG)/core -I$(RAG)/index -I$(RAG)/chunker
CXXFLAGS += -I$(RAG)/projector -I$(RAG)/api -I$(RAG)/gnn -I$(RAG)/gpu
CXXFLAGS += -I$(VCPKG)/include -I$(VCPKG)/include/eigen3

CLI_LIBS := -L$(VCPKG)/lib -L$(OUT) -lvulkan-1 -lsqlite3 -static-libgcc -static-libstdc++ -fopenmp

#  objects 

OBJS := \
    $(OUT)/lorentz.o \
    $(OUT)/lorentz_index.o \
    $(OUT)/loader.o \
    $(OUT)/chunker.o \
    $(OUT)/pca.o \
    $(OUT)/hgnfs_api.o \
    $(OUT)/hgcn.o \
    $(OUT)/graph_builder.o \
    $(OUT)/contrastive.o \
    $(OUT)/vulkan_context.o \
    $(OUT)/rag_tool.o \
    $(OUT)/bert_tokenizer.o \
    $(OUT)/hyp_embedder.o \
    $(OUT)/graph_enhance.o \
    $(OUT)/bm25_index.o

BEIR_BENCH  := $(OUT)/beir_bench.exe

#  targets 

.PHONY: all clean dirs

all: $(BEIR_BENCH)

dirs:
	@mkdir $(subst /,\,$(OUT)) 2>NUL || ver>NUL

$(BEIR_BENCH): $(OUT)/beir_bench.o $(OBJS) | dirs
	$(CXX) -o $@ $(OUT)/beir_bench.o $(OBJS) $(CLI_LIBS)

#  compile rules (all from src/zetla/rag/) 

$(OUT)/lorentz.o:        $(RAG)/core/lorentz.cpp        | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/lorentz_index.o:  $(RAG)/index/lorentz_index.cpp  | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/loader.o:         $(RAG)/index/loader.cpp         | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/chunker.o:        $(RAG)/chunker/chunker.cpp      | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/pca.o:            $(RAG)/projector/pca.cpp        | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/hgnfs_api.o:      $(RAG)/api/hgnfs_api.cpp        | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/hgcn.o:           $(RAG)/gnn/hgcn.cpp             | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/graph_builder.o:  $(RAG)/gnn/graph_builder.cpp    | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/contrastive.o:    $(RAG)/gnn/contrastive.cpp      | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/vulkan_context.o: $(RAG)/gpu/vulkan_context.cpp   | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/rag_tool.o:       $(RAG)/rag_tool.cpp             | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/bert_tokenizer.o: $(RAG)/bert_tokenizer.cpp       | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/hyp_embedder.o:   $(RAG)/hyp_embedder.cpp         | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/graph_enhance.o:  $(RAG)/graph_enhance.cpp        | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/bm25_index.o:     $(RAG)/bm25_index.cpp           | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/beir_bench.o:     $(SRC)/beir_bench.cpp           | dirs ; $(CXX) $(CXXFLAGS) -c $< -o $@

#  clean 

clean:
	-@del /q $(subst /,\,$(OUT))\*.o 2>NUL
	-@del /q $(subst /,\,$(OUT))\*.exe 2>NUL
