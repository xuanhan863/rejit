// Copyright (C) 2013 Alexandre Rames <alexandre@coreperf.com>
// rejit is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "macro-assembler-x64.h"

namespace rejit {
namespace internal {

MacroAssembler::MacroAssembler()
  : MacroAssemblerBase(NULL, 4 * KB) { }


void MacroAssembler::PushRegisters(RegList regs) {
  int i;
  for (i = Register::kNumRegisters - 1; i >= 0; i--) {
    if (regs & (1 << i)) {
      push(Register::from_code(i));
    }
  }
}


void MacroAssembler::PopRegisters(RegList regs) {
  int i;
  for (i = 0; i < Register::kNumRegisters; i++) {
    if (regs & (1 << i)) {
      pop(Register::from_code(i));
    }
  }
}


// TODO: We generally don't care about all those registers and don't need to
// preserve them all.
void MacroAssembler::PushCallerSavedRegisters() {
  PushRegisters(kCallerSavedRegList);
}


void MacroAssembler::PopCallerSavedRegisters() {
  PopRegisters(kCallerSavedRegList);
}


// TODO: The saved registers are above the the frame pointer on the stack, which
// does not fit the ABI. This is because we mess up with lower the stack pointer
// while setting up the state ring (and other stuff), but don't restore it
// before returning.
void MacroAssembler::PushCalleeSavedRegisters() {
  push(rbp);
  movq(rbp, rsp);
  PushRegisters(kCalleeSavedRegList);
}


void MacroAssembler::PopCalleeSavedRegisters() {
  PopRegisters(kCalleeSavedRegList);
  pop(rbp);
}


void MacroAssembler::PushAllRegisters() {
  // All registers except rsp.
  PushRegisters(0xffff & ~rsp.bit());
}


void MacroAssembler::PopAllRegisters() {
  // All registers except rsp.
  PopRegisters(0xffff & ~rsp.bit());
}


// TODO(rames): Push and pop ah instread of rax.
void MacroAssembler::PushAllRegistersAndFlags() {
  PushAllRegisters();
  lahf();
  push(rax);
}


void MacroAssembler::PopAllRegistersAndFlags() {
  pop(rax);
  sahf();
  PopAllRegisters();
}


// The structure of this function is copied from
//  v8 x64 MacroAssembler::LoadSmiConstant().
void MacroAssembler::Move(Register dst, uint64_t value) {
  if (value == 0) {
    xorl(dst, dst);
    return;
  }
  // TODO(rames): Optimize this!
  if (value & 0xFFFFFFFF00000000ULL) {
    movq(dst, Immediate(value >> 32));
    shl(dst, Immediate(32));
    movl(mscratch, Immediate(value & 0xFFFFFFFF));
    or_(dst, mscratch);
  } else {
    movq(dst, Immediate(value & 0xFFFFFFFF));
  }
}


//      const int64_t c = *reinterpret_cast<const int64_t*>(next_chars);
//      __ Move(fixed_chars, c);
//      if (n_chars == 2) {
//        __ movb(fixed_chars, Immediate(next_chars[0]));
//      } else if (n_chars == 3) {
//        __ movw(fixed_chars, Immediate(*reinterpret_cast<const int16_t*>(next_chars)))
//      } else if (n_chars <= sizeof(uint32_t) + 1) {
//        int32_t c =
//          *reinterpret_cast<const int32_t*>(next_chars) & FirstCharsMask(n_chars);
//        __ movl(fixed_chars, Immediate(c))
//
//      } else if (n_chars <= sizeof(uint64_t)) + 1 {
//        int64_t c =
//          *reinterpret_cast<const int64_t*>(next_chars) & FirstCharsMask(n_chars);
//        __ Move(fixed_chars, Immediate(c))
//
//      } else {
//        int64_t c = *reinterpret_cast<const int64_t*>(next_chars);
//        __ Move(fixed_chars, Immediate(c))
//      }

// TODO: Clean all these methods
void MacroAssembler::MoveCharsFrom(Register dst, unsigned n, const char *location) {
  if (n == 0) return;
  int64_t chars = *reinterpret_cast<const int64_t*>(location);
  Move(dst, chars & FirstCharsMask(n));
}


void MacroAssembler::LoadCharsFrom(Register dst, unsigned n, const Operand& src) {
  ASSERT(0 < n && n <= (unsigned)kPointerSize);
  movq(dst, src);
  Move(scratch, FirstCharsMask(n));
  and_(dst, scratch);
}


void MacroAssembler::MaskFirstChars(unsigned n_chars, Register dst) {
  if (n_chars >= 8)
    return;
  if (n_chars == 0) {
    xor_(dst, dst);
  } else if (FitsImmediate(FirstCharsMask(n_chars))) {
    and_(dst, Immediate(FirstCharsMask(n_chars)));
  } else {
    // TODO: What is the fastest way to do that?
    shl(dst, Immediate((kCharsPerPointer - n_chars) * kBitsPerChar));
    shr(dst, Immediate((kCharsPerPointer - n_chars) * kBitsPerChar));
  }
}


void MacroAssembler::mov(unsigned width, Register dst, const Operand& src) {
  unsigned loaded_width = 0;
  if (width > 4) {
    movq(dst, src);
    loaded_width = 8;
  } else if (width > 2) {
    movl(dst, src);
    loaded_width = 4;
  } else if (width > 1) {
    movw(dst, src);
    loaded_width = 2;
  } else if (width == 1) {
    movb(dst, src);
    loaded_width = 1;
  }
  if (width < loaded_width) {
    MaskFirstChars(width, dst);
  }
}


void MacroAssembler::mov_truncated(unsigned width,
                                   Register dst,
                                   const Operand& src) {
  if (width == 0) return;
  if (width == 1) {
    movb(dst, src);
  } else if (width < 4) {
    movw(dst, src);
  } else if (width < 8) {
    movl(dst, src);
  } else {
    movq(dst, src);
  }
}


void MacroAssembler::cmp_truncated(unsigned width,
                                   const Operand& dst,
                                   int64_t src) {
  if (width == 0) return;
  if (width == 1) {
    cmpb(dst, Immediate(src & FirstCharsMask(1)));
  } else if (width < 4) {
    cmpw(dst, Immediate(src & FirstCharsMask(2)));
  } else if (width < 8) {
    cmpl(dst, Immediate(src & FirstCharsMask(4)));
  } else {
    Move(scratch, src);
    cmpq(dst, scratch);
  }
}


void MacroAssembler::cmp_safe(unsigned width, Condition cond,
                              Operand dst, int64_t src,
                              Label* on_no_match) {
  Label done;
  if (width == 0) return;
  if (width >= 8) {
    cmp(8, dst, src);
    return;
  }
  if (width >= 4) {
    cmp(4, dst, src);
    dst = Operand(dst, 4);
    src = src >> 4 * kBitsPerChar;
    width -= 4;
    if (width) {
      j(NegateCondition(cond), on_no_match ? on_no_match : &done);
    }
  }
  if (width >= 2) {
    cmp(2, dst, src);
    dst = Operand(dst, 2);
    src = src >> 2 * kBitsPerChar;
    width -= 2;
    if (width) {
      j(NegateCondition(cond), on_no_match ? on_no_match : &done);
    }
  }
  if (width >= 1) {
    cmp(1, dst, src);
  }
  bind(&done);
}


void MacroAssembler::cmp_safe(unsigned width, Condition cond,
                              Operand dst, Register src,
                              Label* on_no_match) {
  Label done;
  if (width == 0) return;
  if (width >= 8) {
    cmp(8, dst, src);
    return;
  }

  if (!IsPowerOf2(width) && !src.is(scratch3)) {
    movq(scratch3, src);
    src = scratch3;
  }

  if (width >= 4) {
    cmpl(dst, src);
    width -= 4;
    if (width) {
      j(NegateCondition(cond), on_no_match ? on_no_match : &done);
      dst = Operand(dst, 4);
      shr(src, Immediate(4 * kBitsPerChar));
    }
  }
  if (width >= 2) {
    cmpw(dst, src);
    width -= 2;
    if (width) {
      j(NegateCondition(cond), on_no_match ? on_no_match : &done);
      dst = Operand(dst, 2);
      shr(src, Immediate(2 * kBitsPerChar));
    }
  }
  if (width >= 1) {
    cmpb(dst, src);
  }
  bind(&done);
}


void MacroAssembler::cmp(unsigned width, const Operand& dst, int64_t src) {
  if (width == 1) {
    cmpb(dst, Immediate(src & FirstBytesMask(1)));
  } else if (width == 2) {
    cmpw(dst, Immediate(src & FirstBytesMask(2)));
  } else if (width == 3) {
    mov(width, scratch, dst);
    cmpl(scratch, Immediate(src & FirstBytesMask(3)));
  } else if (width == 4) {
    cmpl(dst, Immediate(src & FirstBytesMask(4)));
  } else if (width < 8) {
    mov(width, scratch1, dst);
    if (FitsImmediate(src & FirstBytesMask(width))) {
      cmpq(scratch1, Immediate(src & FirstBytesMask(width)));
    } else {
      Move(scratch2, src & FirstBytesMask(width));
      cmpq(scratch1, scratch2);
    }
  } else if (width >= 8) {
    if (FitsImmediate(src)) {
      cmpq(dst, Immediate(src));
    } else {
      Move(scratch, src);
      cmpq(dst, scratch);
    }
  }
}


void MacroAssembler::cmp(unsigned width, const Operand& dst, Register src) {
  if (width == 1) {
    cmpb(dst, src);
  } else if (width == 2) {
    cmpw(dst, src);
  } else if (width == 3) {
    mov(width, scratch, dst);
    cmpl(scratch, src);
  } else if (width == 4) {
    cmpl(dst, src);
  } else if (width < 8) {
    mov(width, scratch, dst);
    cmpq(scratch, src);
  } else if (width >= 8) {
    cmpq(dst, src);
  }
}


void MacroAssembler::movdq(XMMRegister dst, uint64_t high, uint64_t low) {
  // TODO(rames): rework this.
  // It is awful but it works.
  Move(scratch, high);
  push(scratch);
  Move(scratch, low);
  push(scratch);

  movdqu(dst, Operand(rsp, 0));

  pop(scratch);
  pop(scratch);
}


void MacroAssembler::movdqp(XMMRegister dst, const char* chars, size_t n_chars) {
  // TODO(rames): rework this.
  // It is awful but it works.
    if (n_chars > 8) {
      MoveCharsFrom(scratch, n_chars - 8, chars + 8);
    } else {
      Move(scratch, 0);
    }
    push(scratch);
    MoveCharsFrom(scratch, n_chars, chars);
    push(scratch);

    movdqu(dst, Operand(rsp, 0));

    pop(scratch);
    pop(scratch);
}


void MacroAssembler::ZeroMem(Register start, Register end) {
  // TODO: More efficient implementation?
  Label zero_loop;
  ASSERT(!start.is(rcx));

  Move(rcx, end);
  Move(scratch, 0);
  subq(rcx, start);
  shr(rcx, Immediate(kPointerSizeLog2));

  bind(&zero_loop);
  movq(Operand(start, rcx, times_8, -kPointerSize), scratch);
  loop(&zero_loop);
}


void MacroAssembler::CallCppPrepareStack() {
  movq(scratch, rsp);
  subq(rsp, Immediate(kPointerSize));
  // TODO(rames): introduce a platform stack alignment.
  and_(rsp, Immediate(~0xf));
  movq(Operand(rsp, 0), scratch);
}


void MacroAssembler::CallCpp(Address address) {
  PushCallerSavedRegisters();
  CallCppPrepareStack();
  Move(rax, (int64_t)address);
  call(rax);
  // Restore the stack pointer.
  movq(rsp, Operand(rsp, 0));
  PopCallerSavedRegisters();
}


void MacroAssembler::inc_c(Register dst) {
  if (char_size() == 1) {
    incq(dst);
  } else {
    addq(dst, Immediate(char_size()));
  }
}
void MacroAssembler::dec_c(Register dst) {
  if (char_size() == 1) {
    decq(dst);
  } else {
    subq(dst, Immediate(char_size()));
  }
}


void MacroAssembler::Advance(unsigned n_chars,
                             Direction direction,
                             Register reg) {
  if (direction == kForward) {
    if (kCharSize == 1) {
      incq(string_pointer);
    } else {
      addq(string_pointer, Immediate(kCharSize));
    }
  } else {
    if (kCharSize == 1) {
      decq(string_pointer);
    } else {
      subq(string_pointer, Immediate(kCharSize));
    }
  }
}


void MacroAssembler::AdvanceToEOS() {
  movq(string_pointer, string_end);
}


// Debug helpers -----------------------------------------------------

static void LocalPrint(const char* message) {
  cout << message << endl;
}


void MacroAssembler::asm_assert_(Condition cond, const char *file, int line, const char *description) {
  Label skip;
  j(cond, &skip);

  Move(rdi, (int64_t)file);
  Move(rsi, line);
  Move(rdx, (int64_t)description);
  CallCpp(FUNCTION_ADDR(rejit_fatal));

  bind(&skip);
}


void MacroAssembler::msg(const char *message) {
    PushAllRegistersAndFlags();
    Move(rdi, (int64_t)message);
    CallCpp(FUNCTION_ADDR(LocalPrint));
    PopAllRegistersAndFlags();
}


void MacroAssembler::debug_msg(const char *message) {
  if (FLAG_emit_debug_code) {
    msg(message);
  }
}


void MacroAssembler::debug_msg(Condition cond, const char *message) {
  if (FLAG_emit_debug_code) {
    Label skip;
    if (cond != always) {
      j(NegateCondition(cond), &skip);
    }
    debug_msg(message);
    bind(&skip);
  }
}


void MacroAssembler::stop(const char *message) {
  msg(message);
  int3();
}


void MacroAssembler::stop(Condition cond, const char *message) {
  Label skip;
  if (cond != always) {
    j(NegateCondition(cond), &skip);
  }
  stop(message);
  bind(&skip);
}

} }  // namespace rejit::internal
