// Copyright 2013 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// TODO(jochen): Remove this after the setting is turned on globally.
#define V8_IMMINENT_DEPRECATION_WARNINGS

#include <stdlib.h>
#include <iostream>  // NOLINT(readability/streams)

#include "src/base/utils/random-number-generator.h"
#include "src/macro-assembler.h"
#include "src/mips/macro-assembler-mips.h"
#include "src/mips/simulator-mips.h"
#include "src/v8.h"
#include "test/cctest/cctest.h"


using namespace v8::internal;

typedef void* (*F)(int x, int y, int p2, int p3, int p4);
typedef Object* (*F1)(int x, int p1, int p2, int p3, int p4);

#define __ masm->


static byte to_non_zero(int n) {
  return static_cast<unsigned>(n) % 255 + 1;
}


static bool all_zeroes(const byte* beg, const byte* end) {
  CHECK(beg);
  CHECK(beg <= end);
  while (beg < end) {
    if (*beg++ != 0)
      return false;
  }
  return true;
}


TEST(CopyBytes) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  const int data_size = 1 * KB;
  size_t act_size;

  // Allocate two blocks to copy data between.
  byte* src_buffer =
      static_cast<byte*>(v8::base::OS::Allocate(data_size, &act_size, 0));
  CHECK(src_buffer);
  CHECK(act_size >= static_cast<size_t>(data_size));
  byte* dest_buffer =
      static_cast<byte*>(v8::base::OS::Allocate(data_size, &act_size, 0));
  CHECK(dest_buffer);
  CHECK(act_size >= static_cast<size_t>(data_size));

  // Storage for a0 and a1.
  byte* a0_;
  byte* a1_;

  MacroAssembler assembler(isolate, NULL, 0,
                           v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;

  // Code to be generated: The stuff in CopyBytes followed by a store of a0 and
  // a1, respectively.
  __ CopyBytes(a0, a1, a2, a3);
  __ li(a2, Operand(reinterpret_cast<int>(&a0_)));
  __ li(a3, Operand(reinterpret_cast<int>(&a1_)));
  __ sw(a0, MemOperand(a2));
  __ jr(ra);
  __ sw(a1, MemOperand(a3));

  CodeDesc desc;
  masm->GetCode(&desc);
  Handle<Code> code = isolate->factory()->NewCode(
      desc, Code::ComputeFlags(Code::STUB), Handle<Code>());

  ::F f = FUNCTION_CAST< ::F>(code->entry());

  // Initialise source data with non-zero bytes.
  for (int i = 0; i < data_size; i++) {
    src_buffer[i] = to_non_zero(i);
  }

  const int fuzz = 11;

  for (int size = 0; size < 600; size++) {
    for (const byte* src = src_buffer; src < src_buffer + fuzz; src++) {
      for (byte* dest = dest_buffer; dest < dest_buffer + fuzz; dest++) {
        memset(dest_buffer, 0, data_size);
        CHECK(dest + size < dest_buffer + data_size);
        (void)CALL_GENERATED_CODE(isolate, f, reinterpret_cast<int>(src),
                                  reinterpret_cast<int>(dest), size, 0, 0);
        // a0 and a1 should point at the first byte after the copied data.
        CHECK_EQ(src + size, a0_);
        CHECK_EQ(dest + size, a1_);
        // Check that we haven't written outside the target area.
        CHECK(all_zeroes(dest_buffer, dest));
        CHECK(all_zeroes(dest + size, dest_buffer + data_size));
        // Check the target area.
        CHECK_EQ(0, memcmp(src, dest, size));
      }
    }
  }

  // Check that the source data hasn't been clobbered.
  for (int i = 0; i < data_size; i++) {
    CHECK(src_buffer[i] == to_non_zero(i));
  }
}


static void TestNaN(const char *code) {
  // NaN value is different on MIPS and x86 architectures, and TEST(NaNx)
  // tests checks the case where a x86 NaN value is serialized into the
  // snapshot on the simulator during cross compilation.
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> context = CcTest::NewContext(PRINT_EXTENSION);
  v8::Context::Scope context_scope(context);

  v8::Local<v8::Script> script =
      v8::Script::Compile(context, v8_str(code)).ToLocalChecked();
  v8::Local<v8::Object> result =
      v8::Local<v8::Object>::Cast(script->Run(context).ToLocalChecked());
  i::Handle<i::JSReceiver> o = v8::Utils::OpenHandle(*result);
  i::Handle<i::JSArray> array1(reinterpret_cast<i::JSArray*>(*o));
  i::FixedDoubleArray* a = i::FixedDoubleArray::cast(array1->elements());
  double value = a->get_scalar(0);
  CHECK(std::isnan(value) &&
        bit_cast<uint64_t>(value) ==
            bit_cast<uint64_t>(std::numeric_limits<double>::quiet_NaN()));
}


TEST(NaN0) {
  TestNaN(
          "var result;"
          "for (var i = 0; i < 2; i++) {"
          "  result = new Array(Number.NaN, Number.POSITIVE_INFINITY);"
          "}"
          "result;");
}


TEST(NaN1) {
  TestNaN(
          "var result;"
          "for (var i = 0; i < 2; i++) {"
          "  result = [NaN];"
          "}"
          "result;");
}


TEST(jump_tables4) {
  // Similar to test-assembler-mips jump_tables1, with extra test for branch
  // trampoline required before emission of the dd table (where trampolines are
  // blocked), and proper transition to long-branch mode.
  // Regression test for v8:4294.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assembler(isolate, NULL, 0,
                           v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;

  const int kNumCases = 512;
  int values[kNumCases];
  isolate->random_number_generator()->NextBytes(values, sizeof(values));
  Label labels[kNumCases];
  Label near_start, end;

  __ addiu(sp, sp, -4);
  __ sw(ra, MemOperand(sp));

  __ mov(v0, zero_reg);

  __ Branch(&end);
  __ bind(&near_start);

  // Generate slightly less than 32K instructions, which will soon require
  // trampoline for branch distance fixup.
  for (int i = 0; i < 32768 - 256; ++i) {
    __ addiu(v0, v0, 1);
  }

  Label done;
  {
    __ BlockTrampolinePoolFor(kNumCases + 6);
    PredictableCodeSizeScope predictable(
        masm, (kNumCases + 6) * Assembler::kInstrSize);
    Label here;

    __ bal(&here);
    __ sll(at, a0, 2);  // In delay slot.
    __ bind(&here);
    __ addu(at, at, ra);
    __ lw(at, MemOperand(at, 4 * Assembler::kInstrSize));
    __ jr(at);
    __ nop();  // Branch delay slot nop.
    for (int i = 0; i < kNumCases; ++i) {
      __ dd(&labels[i]);
    }
  }

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    __ lui(v0, (values[i] >> 16) & 0xffff);
    __ ori(v0, v0, values[i] & 0xffff);
    __ Branch(&done);
  }

  __ bind(&done);
  __ lw(ra, MemOperand(sp));
  __ addiu(sp, sp, 4);
  __ jr(ra);
  __ nop();

  __ bind(&end);
  __ Branch(&near_start);

  CodeDesc desc;
  masm->GetCode(&desc);
  Handle<Code> code = isolate->factory()->NewCode(
      desc, Code::ComputeFlags(Code::STUB), Handle<Code>());
#ifdef OBJECT_PRINT
  code->Print(std::cout);
#endif
  F1 f = FUNCTION_CAST<F1>(code->entry());
  for (int i = 0; i < kNumCases; ++i) {
    int res =
        reinterpret_cast<int>(CALL_GENERATED_CODE(isolate, f, i, 0, 0, 0, 0));
    ::printf("f(%d) = %d\n", i, res);
    CHECK_EQ(values[i], res);
  }
}


#undef __
