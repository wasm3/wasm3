#include <stdio.h>
#include <catch2/catch_all.hpp>

#include "extensions/wasm3_ext.h"
#include "m3_bind.h"

typedef int32_t i32;
typedef int64_t s64;
typedef float f32;
typedef double f64;

namespace
{
  /*
    (module
        (func (result i32 f32 f64 i64)
            i32.const 1234
            f32.const 128.125
            f64.const 128.0125
            i64.const 0xAABBCCDD11223344
        )
        (export "main" (func 0))
    )
  */
  constexpr u8 WASM_MULTIVAL [] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x01, 0x60,
    0x00, 0x04, 0x7f, 0x7d, 0x7c, 0x7e, 0x03, 0x02, 0x01, 0x00, 0x07, 0x08,
    0x01, 0x04, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x0a, 0x20, 0x01, 0x1e,
    0x00, 0x41, 0xd2, 0x09, 0x43, 0x00, 0x20, 0x00, 0x43, 0x44, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x00, 0x60, 0x40, 0x42, 0xc4, 0xe6, 0x88, 0x89, 0xd1,
    0x9b, 0xf3, 0xdd, 0xaa, 0x7f, 0x0b
  };

  struct ResultStruct {
    i32 a = 0;
    f32 b = 0.0f;
    f64 c = 0.0;
    u64 d = 0;
  };
};

TEST_CASE( "m3_GetResultsBuffer", "[m3]" ) 
{
  IM3Environment env = m3_NewEnvironment ();
  IM3Runtime runtime = m3_NewRuntime (env, 1024, NULL);

  // WASM and function setup
  IM3Module module;
  M3Result result = m3_ParseModule  (env, & module, WASM_MULTIVAL, sizeof(WASM_MULTIVAL));
  REQUIRE (result == m3Err_none);

  result = m3_LoadModule(runtime, module);
  REQUIRE (result == m3Err_none);

  IM3Function function = nullptr;
  result = m3_FindFunction (& function, runtime, "main");
  REQUIRE (result == m3Err_none);
  REQUIRE (function != nullptr);

  // Actual tests:

  SECTION( "buffer before function call" ) 
  {
    ResultStruct buff;
    result = m3_GetResultsBuffer(function, sizeof(buff), &buff);
    REQUIRE (result == m3Err_functionNotCalled);
  }

  // Call function
  result = m3_Call(function, 0, nullptr);
  REQUIRE (result == m3Err_none);

  SECTION( "correct buffer" ) 
  {
    auto oldStackVal = module->runtime->stack;

    ResultStruct buff;
    result = m3_GetResultsBuffer(function, sizeof(buff), &buff);
    REQUIRE (result == m3Err_none);
    REQUIRE (buff.a == 1234);
    REQUIRE (buff.b == 128.125f);
    REQUIRE (buff.c == 128.0125);
    REQUIRE (buff.d == 0xAABB'CCDD'1122'3344);

    // reading data should not change the stack
    REQUIRE (oldStackVal == module->runtime->stack);
  }

  SECTION( "nullptr / zero-size combinations should fail" ) 
  {
    ResultStruct buff;

    result = m3_GetResultsBuffer(function, 0, nullptr);
    REQUIRE (result == m3Err_functionResultNullPtr);

    result = m3_GetResultsBuffer(function, 42, nullptr);
    REQUIRE (result == m3Err_functionResultNullPtr);

    result = m3_GetResultsBuffer(function, 0, &buff);
    REQUIRE (result == m3Err_argumentCountMismatch);
  }

  SECTION( "buffer too small" ) 
  {
    ResultStruct buff;
    result = m3_GetResultsBuffer(function, sizeof(i32), &buff); // set a size 12 bytes too small
    REQUIRE (result == m3Err_argumentCountMismatch);
    // "b" to "d" are OOB and should be unchanged
    REQUIRE (buff.b == 0.0f);
    REQUIRE (buff.c == 0.0);
    REQUIRE (buff.d == 0);
  }

  SECTION( "buffer too small (unaligned)" ) 
  {
    ResultStruct buff;
    result = m3_GetResultsBuffer(function, sizeof(buff) - 3, &buff); // size too small with an unaligned size
    REQUIRE (result == m3Err_argumentCountMismatch);
    // "d" is OOB and should be unchanged
    REQUIRE (buff.d == 0);
  }

  SECTION( "buffer too big" ) 
  {
    ResultStruct buff;
    result = m3_GetResultsBuffer(function, sizeof(buff) + sizeof(i32), &buff); // set a size 4 bytes too big
    REQUIRE (result == m3Err_argumentCountMismatch);
    // the actual values of "buff" don't matter, but they may be set
  }
}
