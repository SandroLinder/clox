#ifndef clox_debug_h
#define clox_debug_h

#include "../chunk.h"

void disassembleChunk(const Chunk* chunk, const char* name);

int disassembleInstruction(const Chunk* chunk, int offset);

void printValue(Value value);

#endif
