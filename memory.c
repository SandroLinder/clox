#include <stdlib.h>

#include "memory.h"

#include "compiler/compiler.h"
#include "types/object.h"
#include "types/value.h"
#include "vm/vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "tools/debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* pointer, const size_t oldSize, const size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;

    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        collectGarbage();
#endif
    }

    if (vm.bytesAllocated > vm.nextGC) {
        collectGarbage();
    }

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);

    if (result == NULL) {
        exit(1);
    }

    return result;
}

void markObject(Obj* obj) {
    if (obj == NULL) {
        return;
    }

    if (obj->isMarked) {
        return;
    }

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void *) obj);
    printValue(OBJ_VAL(obj));
    printf("\n");
#endif
    obj->isMarked = true;

    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        vm.grayStack = (Obj **) realloc(vm.grayStack, sizeof(Obj *) * vm.grayCapacity);

        if (vm.grayStack == NULL) {
            exit(1);
        }
    }

    vm.grayStack[vm.grayCount++] = obj;
}

void markValue(const Value value) {
    if (IS_OBJ(value)) {
        markObject(AS_OBJ(value));
    }
}

static void markArray(const ValueArray* arr) {
    for (int i = 0; i < arr->count; i++) {
        markValue(arr->values[i]);
    }
}

static void blackenObject(const Obj* obj) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void *) obj);
    printValue(OBJ_VAL(obj));
    printf("\n");
#endif
    switch (obj->type) {
        case OBJ_BOUND_METHOD: {
            const ObjBoundMethod* bound = (ObjBoundMethod *) obj;
            markValue(bound->receiver);
            markObject((Obj *) bound->method);
            break;
        }
        case OBJ_CLASS: {
            const ObjClass* klass = (ObjClass *) obj;
            markObject((Obj *) klass->name);
            markTable(&klass->methods);
            break;
        }
        case OBJ_INSTANCE: {
            const ObjInstance* instance = (ObjInstance *) obj;
            markObject((Obj *) instance->klass);
            markTable(&instance->fields);
            break;
        }
        case OBJ_CLOSURE:
            const ObjClosure* closure = (ObjClosure *) obj;
            markObject((Obj *) closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject((Obj *) closure->upvalues[i]);
            }
            break;
        case OBJ_FUNCTION:
            const ObjFunction* function = (ObjFunction *) obj;
            markObject((Obj *) function->name);
            markArray(&function->chunk.constants);
            break;
        case OBJ_UPVALUE:
            markValue(((ObjUpvalue *) obj)->closed);
            break;
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}

void freeObject(Obj* obj) {
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void *) obj, obj->type);
#endif

    switch (obj->type) {
        case OBJ_STRING: {
            const auto string = (ObjString *) obj;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, obj);
            break;
        }
        case OBJ_FUNCTION: {
            const auto function = (ObjFunction *) obj;
            freeChunk(&function->chunk);
            FREE(ObjFunction, obj);
            break;
        }
        case OBJ_NATIVE: {
            FREE(ObjNative, obj);
            break;
        }
        case OBJ_CLOSURE: {
            const ObjClosure* closure = (ObjClosure *) obj;
            FREE_ARRAY(ObjUpvalue*, (ObjUpvalue*)closure->upvalueCount, closure->upvalueCount);
            FREE(ObjClosure, obj);
            break;
        }
        case OBJ_UPVALUE: {
            FREE(ObjUpvalue, obj);
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass *) obj;
            freeTable(&klass->methods);
            FREE(ObjClass, obj);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance *) obj;
            freeTable(&instance->fields);
            FREE(ObjInstance, obj);
            break;
        }
        case OBJ_BOUND_METHOD: {
            FREE(ObjBoundMethod, obj);
            break;
        }
    }
}

void freeObjects() {
    Obj* object = vm.objects;

    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }

    free(vm.grayStack);
}

void markRoots() {
    for (const Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }

    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj *) vm.frames[i].closure);
    }

    for (const ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj *) upvalue);
    }

    markTable(&vm.globals);

    markCompilerRoots();
    markObject((Obj *) vm.initString);
}

static void traceReferences() {
    while (vm.grayCount > 0) {
        const Obj* obj = vm.grayStack[--vm.grayCount];
        blackenObject(obj);
    }
}

static void sweep() {
    Obj* prev = NULL;
    Obj* object = vm.objects;

    while (object != NULL) {
        if (object->isMarked) {
            object->isMarked = false;
            prev = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (prev != NULL) {
                prev->next = object;
            } else {
                vm.objects = object;
            }

            freeObject(unreached);
        }
    }
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    const size_t before = vm.bytesAllocated;
#endif

    markRoots();
    traceReferences();
    tableRemoveWhite(&vm.strings);
    sweep();

    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm.bytesAllocated, before, vm.bytesAllocated,
           vm.nextGC);
#endif
}
