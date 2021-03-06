#ifndef wren_h
#define wren_h

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct WrenVM WrenVM;

// A generic allocation function that handles all explicit memory management
// used by Wren. It's used like so:
//
// - To allocate new memory, [memory] is NULL and [oldSize] is zero. It should
//   return the allocated memory or NULL on failure.
//
// - To attempt to grow an existing allocation, [memory] is the memory,
//   [oldSize] is its previous size, and [newSize] is the desired size.
//   It should return [memory] if it was able to grow it in place, or a new
//   pointer if it had to move it.
//
// - To shrink memory, [memory], [oldSize], and [newSize] are the same as above
//   but it will always return [memory].
//
// - To free memory, [memory] will be the memory to free and [newSize] and
//   [oldSize] will be zero. It should return NULL.
typedef void* (*WrenReallocateFn)(void* memory, size_t oldSize, size_t newSize);

// A function callable from Wren code, but implemented in C.
typedef void (*WrenForeignMethodFn)(WrenVM* vm);

// Loads and returns the source code for the module [name].
typedef char* (*WrenLoadModuleFn)(WrenVM* vm, const char* name);

typedef struct
{
  // The callback Wren will use to allocate, reallocate, and deallocate memory.
  //
  // If `NULL`, defaults to a built-in function that uses `realloc` and `free`.
  WrenReallocateFn reallocateFn;

  // The callback Wren uses to load a module.
  //
  // Since Wren does not talk directly to the file system, it relies on the
  // embedder to phyisically locate and read the source code for a module. The
  // first time an import appears, Wren will call this and pass in the name of
  // the module being imported. The VM should return the soure code for that
  // module. Memory for the source should be allocated using [reallocateFn] and
  // Wren will take ownership over it.
  //
  // This will only be called once for any given module name. Wren caches the
  // result internally so subsequent imports of the same module will use the
  // previous source and not call this.
  //
  // If a module with the given name could not be found by the embedder, it
  // should return NULL and Wren will report that as a runtime error.
  WrenLoadModuleFn loadModuleFn;

  // The number of bytes Wren will allocate before triggering the first garbage
  // collection.
  //
  // If zero, defaults to 10MB.
  size_t initialHeapSize;

  // After a collection occurs, the threshold for the next collection is
  // determined based on the number of bytes remaining in use. This allows Wren
  // to shrink its memory usage automatically after reclaiming a large amount
  // of memory.
  //
  // This can be used to ensure that the heap does not get too small, which can
  // in turn lead to a large number of collections afterwards as the heap grows
  // back to a usable size.
  //
  // If zero, defaults to 1MB.
  size_t minHeapSize;

  // Wren will grow (and shrink) the heap automatically as the number of bytes
  // remaining in use after a collection changes. This number determines the
  // amount of additional memory Wren will use after a collection, as a
  // percentage of the current heap size.
  //
  // For example, say that this is 50. After a garbage collection, Wren there
  // are 400 bytes of memory still in use. That means the next collection will
  // be triggered after a total of 600 bytes are allocated (including the 400
  // already in use.
  //
  // Setting this to a smaller number wastes less memory, but triggers more
  // frequent garbage collections.
  //
  // If zero, defaults to 50.
  int heapGrowthPercent;
} WrenConfiguration;

typedef enum {
  WREN_RESULT_SUCCESS,
  WREN_RESULT_COMPILE_ERROR,
  WREN_RESULT_RUNTIME_ERROR
} WrenInterpretResult;

// Creates a new Wren virtual machine using the given [configuration]. Wren
// will copy the configuration data, so the argument passed to this can be
// freed after calling this. If [configuration] is `NULL`, uses a default
// configuration.
WrenVM* wrenNewVM(WrenConfiguration* configuration);

// Disposes of all resources is use by [vm], which was previously created by a
// call to [wrenNewVM].
void wrenFreeVM(WrenVM* vm);

// Runs [source], a string of Wren source code in a new fiber in [vm].
//
// [sourcePath] is a string describing where [source] was located, for use in
// debugging stack traces. It must not be `null`.
//
// If it's an empty string, then [source] is considered part of the "core"
// module. Any module-level names defined in it will be implicitly imported by
// another other modules. This also means runtime errors in its code will be
// omitted from stack traces (to avoid confusing users with core library
// implementation details).
WrenInterpretResult wrenInterpret(WrenVM* vm, const char* sourcePath,
                                  const char* source);

// TODO: Figure out how these interact with modules.

// Defines a foreign method implemented by the host application. Looks for a
// global class named [className] to bind the method to. If not found, it will
// be created automatically.
//
// Defines a method on that class named [methodName] accepting [arity]
// parameters. If a method already exists with that name and arity, it will be
// replaced. When invoked, the method will call [method].
void wrenDefineMethod(WrenVM* vm, const char* className,
                      const char* methodName, int arity,
                      WrenForeignMethodFn method);

// Defines a static foreign method implemented by the host application. Looks
// for a global class named [className] to bind the method to. If not found, it
// will be created automatically.
//
// Defines a static method on that class named [methodName] accepting
// [arity] parameters. If a method already exists with that name and arity,
// it will be replaced. When invoked, the method will call [method].
void wrenDefineStaticMethod(WrenVM* vm, const char* className,
                            const char* methodName, int arity,
                            WrenForeignMethodFn method);

// The following functions read one of the arguments passed to a foreign call.
// They may only be called while within a function provided to
// [wrenDefineMethod] or [wrenDefineStaticMethod] that Wren has invoked.
//
// They retreive the argument at a given index which ranges from 0 to the number
// of parameters the method expects. The zeroth parameter is used for the
// receiver of the method. For example, given a foreign method "foo" on String
// invoked like:
//
//     "receiver".foo("one", "two", "three")
//
// The foreign function will be able to access the arguments like so:
//
//     0: "receiver"
//     1: "one"
//     2: "two"
//     3: "three"
//
// It is an error to pass an invalid argument index.

// Reads a boolean argument for a foreign call. Returns false if the argument
// is not a boolean.
bool wrenGetArgumentBool(WrenVM* vm, int index);

// Reads a numeric argument for a foreign call. Returns 0 if the argument is not
// a number.
double wrenGetArgumentDouble(WrenVM* vm, int index);

// Reads an string argument for a foreign call. Returns NULL if the argument is
// not a string.
//
// The memory for the returned string is owned by Wren. You can inspect it
// while in your foreign function, but cannot keep a pointer to it after the
// function returns, since the garbage collector may reclaim it.
const char* wrenGetArgumentString(WrenVM* vm, int index);

// The following functions provide the return value for a foreign method back
// to Wren. Like above, they may only be called during a foreign call invoked
// by Wren.
//
// If none of these is called by the time the foreign function returns, the
// method implicitly returns `null`. Within a given foreign call, you may only
// call one of these once. It is an error to access any of the foreign calls
// arguments after one of these has been called.

// Provides a boolean return value for a foreign call.
void wrenReturnBool(WrenVM* vm, bool value);

// Provides a numeric return value for a foreign call.
void wrenReturnDouble(WrenVM* vm, double value);

// Provides a string return value for a foreign call.
//
// The [text] will be copied to a new string within Wren's heap, so you can
// free memory used by it after this is called. If [length] is non-zero, Wren
// will copy that many bytes from [text]. If it is -1, then the length of
// [text] will be calculated using `strlen()`.
void wrenReturnString(WrenVM* vm, const char* text, int length);

#endif
