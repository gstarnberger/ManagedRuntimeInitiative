/*
 * Copyright 2005-2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.


#include "barrierSet.hpp"
#include "c1_CodeStubs.hpp"
#include "c1_FrameMap.hpp"
#include "c1_IR.hpp"
#include "c1_LIR.hpp"
#include "c1_LIRAssembler.hpp"
#include "c1_LIRGenerator.hpp"
#include "c1_ThreadLocals.hpp"
#include "c1_ValueStack.hpp"
#include "cardTableModRefBS.hpp"
#include "ciArrayKlass.hpp"
#include "ciInstance.hpp"
#include "collectedHeap.hpp"
#include "javaClasses.hpp"
#include "jvmtiExport.hpp"
#include "methodLiveness.hpp"
#include "sharedRuntime.hpp"
#include "stubRoutines.hpp"

#ifdef ASSERT
#define __ gen()->lir(__FILE__, __LINE__)->
#else
#define __ gen()->lir()->
#endif


void PhiResolverState::reset(int max_vregs) {
  // Initialize array sizes
  _virtual_operands.at_put_grow(max_vregs - 1, NULL, NULL);
  _virtual_operands.trunc_to(0);
  _other_operands.at_put_grow(max_vregs - 1, NULL, NULL);
  _other_operands.trunc_to(0);
  _vreg_table.at_put_grow(max_vregs - 1, NULL, NULL);
  _vreg_table.trunc_to(0);
}



//--------------------------------------------------------------
// PhiResolver

// Resolves cycles:
//
//  r1 := r2  becomes  temp := r1
//  r2 := r1           r1 := r2
//                     r2 := temp
// and orders moves:
//
//  r2 := r3  becomes  r1 := r2
//  r1 := r2           r2 := r3

PhiResolver::PhiResolver(LIRGenerator* gen, int max_vregs) 
 : _gen(gen)
 , _state(gen->resolver_state())
 , _temp(LIR_OprFact::illegalOpr)
{
  // reinitialize the shared state arrays
  _state.reset(max_vregs);
}


void PhiResolver::emit_move(LIR_Opr src, LIR_Opr dest) {
  assert(src->is_valid(), "");
  assert(dest->is_valid(), "");
  __ move(src, dest);
}


void PhiResolver::move_temp_to(LIR_Opr dest) {
  assert(_temp->is_valid(), "");
  emit_move(_temp, dest);
  NOT_PRODUCT(_temp = LIR_OprFact::illegalOpr);
}


void PhiResolver::move_to_temp(LIR_Opr src) {
  assert(_temp->is_illegal(), "");
  _temp = _gen->new_register(src->type());
  emit_move(src, _temp);
}


// Traverse assignment graph in depth first order and generate moves in post order
// ie. two assignments: b := c, a := b start with node c:
// Call graph: move(NULL, c) -> move(c, b) -> move(b, a)
// Generates moves in this order: move b to a and move c to b
// ie. cycle a := b, b := a start with node a
// Call graph: move(NULL, a) -> move(a, b) -> move(b, a)
// Generates moves in this order: move b to temp, move a to b, move temp to a
void PhiResolver::move(ResolveNode* src, ResolveNode* dest) {
  if (!dest->visited()) {
    dest->set_visited();
    for (int i = dest->no_of_destinations()-1; i >= 0; i --) {
      move(dest, dest->destination_at(i));
    }
  } else if (!dest->start_node()) {
    // cylce in graph detected 
    assert(_loop == NULL, "only one loop valid!");
    _loop = dest;
    move_to_temp(src->operand());
    return;
  } // else dest is a start node

  if (!dest->assigned()) {
    if (_loop == dest) {
      move_temp_to(dest->operand());
      dest->set_assigned();
    } else if (src != NULL) {
      emit_move(src->operand(), dest->operand());
      dest->set_assigned();
    }
  }
}


PhiResolver::~PhiResolver() {
  int i;
  // resolve any cycles in moves from and to virtual registers
  for (i = virtual_operands().length() - 1; i >= 0; i --) {
    ResolveNode* node = virtual_operands()[i];
    if (!node->visited()) {
      _loop = NULL;
      move(NULL, node);
      node->set_start_node();
      assert(_temp->is_illegal(), "move_temp_to() call missing");
    }
  }

  // generate move for move from non virtual register to abitrary destination
  for (i = other_operands().length() - 1; i >= 0; i --) {
    ResolveNode* node = other_operands()[i];
    for (int j = node->no_of_destinations() - 1; j >= 0; j --) {
      emit_move(node->operand(), node->destination_at(j)->operand());
    }
  }
}


ResolveNode* PhiResolver::create_node(LIR_Opr opr, bool source) {
  ResolveNode* node;
  if (opr->is_virtual()) {
    int vreg_num = opr->vreg_number();
    node = vreg_table().at_grow(vreg_num, NULL);
    assert(node == NULL || node->operand() == opr, "");
    if (node == NULL) {
      node = new ResolveNode(opr);
      vreg_table()[vreg_num] = node;
    }
    // Make sure that all virtual operands show up in the list when
    // they are used as the source of a move.
    if (source && !virtual_operands().contains(node)) {
      virtual_operands().append(node);
    }
  } else {
    assert(source, "");
    node = new ResolveNode(opr);
    other_operands().append(node);
  }
  return node;
}


void PhiResolver::move(LIR_Opr src, LIR_Opr dest) {
  assert(dest->is_virtual(), "");
  // tty->print("move "); src->print(); tty->print(" to "); dest->print(); tty->cr();
  assert(src->is_valid(), ""); 
  assert(dest->is_valid(), "");
  ResolveNode* source = source_node(src);
  source->append(destination_node(dest));
}


//--------------------------------------------------------------
// LIRItem

void LIRItem::set_result(LIR_Opr opr) {  
  assert(value()->operand()->is_illegal() || value()->operand()->is_constant(), "operand should never change");
  value()->set_operand(opr);

  if (opr->is_virtual()) {
    _gen->_instruction_for_operand.at_put_grow(opr->vreg_number(), value(), NULL);
  }

  _result = opr;
}

void LIRItem::load_item() {
  if (result()->is_illegal()) {
    // update the items result
    _result = value()->operand();
  }
  if (!result()->is_register()) {
    LIR_Opr reg = _gen->new_register(value()->type());
    __ move(result(), reg);
    if (result()->is_constant()) {
      _result = reg;
    } else {
      set_result(reg);
    }
  }
}


void LIRItem::load_for_store(BasicType type) {
  if (_gen->can_store_as_constant(value(), type)) {
    _result = value()->operand();
    if (!_result->is_constant()) {
      _result = LIR_OprFact::value_type(value()->type());
    }
  } else if (type == T_BYTE || type == T_BOOLEAN) {
    load_byte_item();
  } else {
    load_item();
  }
}

void LIRItem::load_item_force(LIR_Opr reg) {
  LIR_Opr r = result();
  if (r != reg) {
    if (r->type() != reg->type()) {
      // moves between different types need an intervening spill slot
      LIR_Opr tmp = _gen->force_to_spill(r, reg->type());
      __ move(tmp, reg);
    } else {
      __ move(r, reg);
    }
    _result = reg;
  }
}

ciObject* LIRItem::get_jobject_constant() const {
  ObjectType* oc = type()->as_ObjectType();
  if (oc) {
    return oc->constant_value();
  }
  return NULL;
}


jint LIRItem::get_jint_constant() const {
  assert(is_constant() && value() != NULL, "");
  assert(type()->as_IntConstant() != NULL, "type check");
  return type()->as_IntConstant()->value();
}


jint LIRItem::get_address_constant() const {
  assert(is_constant() && value() != NULL, "");
  assert(type()->as_AddressConstant() != NULL, "type check");
  return type()->as_AddressConstant()->value();
}


jfloat LIRItem::get_jfloat_constant() const {
  assert(is_constant() && value() != NULL, "");
  assert(type()->as_FloatConstant() != NULL, "type check");
  return type()->as_FloatConstant()->value();
}


jdouble LIRItem::get_jdouble_constant() const {
  assert(is_constant() && value() != NULL, "");
  assert(type()->as_DoubleConstant() != NULL, "type check");
  return type()->as_DoubleConstant()->value();
}


jlong LIRItem::get_jlong_constant() const {
  assert(is_constant() && value() != NULL, "");
  assert(type()->as_LongConstant() != NULL, "type check");
  return type()->as_LongConstant()->value();
}



//--------------------------------------------------------------


void LIRGenerator::init() {
  BarrierSet* bs = Universe::heap()->barrier_set();
  if (!UseGenPauselessGC) {
    CardTableModRefBS* ct = (CardTableModRefBS*)bs;
    assert(bs->kind() == BarrierSet::CardTableModRef, "Wrong barrier set kind");
    assert(sizeof(*ct->byte_map_base) == sizeof(jbyte), "adjust this code");
    _card_table_base = new LIR_Const((jlong)ct->byte_map_base);
  }
for(int i=0;i<FrameMap::pd_max_temp_vregs;i++){
    _temp_vregs[i] = new_register(FrameMap::type_for_temp_vreg(i));
  }
_cp_reg=LIR_OprFact::illegalOpr;
_thread_reg=LIR_OprFact::illegalOpr;
}


void LIRGenerator::block_do_prolog(BlockBegin* block) {
#ifndef PRODUCT
  if (PrintIRWithLIR) {
    block->print();
  }
#endif

  // set up the list of LIR instructions
  assert(block->lir() == NULL, "LIR list already computed for this block");
  _lir = new LIR_List(compilation(), block);
  block->set_lir(_lir);

  __ branch_destination(block->label());

  if (LIRTraceExecution &&
      Compilation::current_compilation()->hir()->start()->block_id() != block->block_id() &&
      !block->is_set(BlockBegin::exception_entry_flag)) {
    assert(block->lir()->instructions_list()->length() == 1, "should come right after br_dst");
    trace_block_entry(block);
  }
}


void LIRGenerator::block_do_epilog(BlockBegin* block) {
#ifndef PRODUCT
  if (PrintIRWithLIR) {
    tty->cr();
  }
#endif

  // LIR_Opr for unpinned constants shouldn't be referenced by other
  // blocks so clear them out after processing the block.
  for (int i = 0; i < _unpinned_constants.length(); i++) {
    _unpinned_constants.at(i)->clear_operand();
  }
  _unpinned_constants.trunc_to(0);

  // clear our any registers for other local constants
  _constants.trunc_to(0);
  _reg_for_constants.trunc_to(0);
}


void LIRGenerator::block_do(BlockBegin* block) {
  CHECK_BAILOUT();

  block_do_prolog(block);
  set_block(block);

  for (Instruction* instr = block; instr != NULL; instr = instr->next()) {
    if (instr->is_pinned()) do_root(instr);
  }

  set_block(NULL);
  block_do_epilog(block);
}


//-------------------------LIRGenerator-----------------------------

// This is where the tree-walk starts; instr must be root;
void LIRGenerator::do_root(Value instr) {
  CHECK_BAILOUT();

  InstructionMark im(compilation(), instr);

  assert(instr->is_pinned(), "use only with roots");
  assert(instr->subst() == instr, "shouldn't have missed substitution");

  instr->visit(this);

  if (ciEnv::current()->failing()) {
    compilation()->bailout(ciEnv::current()->failure_reason());
  }

  assert(!instr->has_uses() || instr->operand()->is_valid() ||
         instr->as_Constant() != NULL || bailed_out(), "invalid item set");
}

CodeEmitInfo* LIRGenerator::state_for(Instruction* x, ValueStack* state, bool ignore_xhandler) {
  int index;
  Value value;
  for_each_stack_value(state, index, value) {
    assert(value->subst() == value, "missed substition");
    if (!value->is_pinned() && value->as_Constant() == NULL && value->as_Local() == NULL) {
      walk(value);
      assert(value->operand()->is_valid(), "must be evaluated now");
    }
  }
  ValueStack* s = state;
  int bci = x->bci();
  for_each_state(s) {
    IRScope* scope = s->scope();
    ciMethod* method = scope->method();

    MethodLivenessResult liveness = method->liveness_at_bci(bci);
if(bci==InvocationEntryBci){
      if (x->as_ExceptionObject() || x->as_Throw()) {
        // all locals are dead on exit from the synthetic unlocker
        liveness.clear();
      } else {
        assert(x->as_MonitorEnter(), "only other case is MonitorEnter");
      }
    }
    if (!liveness.is_valid()) {
      // Degenerate or breakpointed method.
      bailout("Degenerate or breakpointed method");
    } else {
      assert((int)liveness.size() == s->locals_size(), "error in use of liveness");
      for_each_local_value(s, index, value) {
        assert(value->subst() == value, "missed substition");
        if (liveness.at(index) && !value->type()->is_illegal()) {
          if (!value->is_pinned() && value->as_Constant() == NULL && value->as_Local() == NULL) {
            walk(value);
            assert(value->operand()->is_valid(), "must be evaluated now");
          }
        } else {
          // NULL out this local so that linear scan can assume that all non-NULL values are live.
          s->invalidate_local(index);
        }
      }
    }
    bci = scope->caller_bci();
  }

  return new CodeEmitInfo(x->bci(), state, ignore_xhandler ? NULL : x->exception_handlers());
}


CodeEmitInfo* LIRGenerator::state_for(Instruction* x) {
  return state_for(x, x->lock_stack());
}


void LIRGenerator::jobject2reg_with_patching(LIR_Opr r, ciObject* obj, CodeEmitInfo* info) {
  if (!obj->is_loaded() || PatchALot) {
    assert(info != NULL, "info must be set if class is not loaded");
    __ oop2reg_patch(NULL, r, info);
  } else {
    // no patching needed
    __ oop2reg(obj->encoding(), r);
  }
}


void LIRGenerator::array_range_check(LIR_Opr array, LIR_Opr index,
                                    CodeEmitInfo* null_check_info, CodeEmitInfo* range_check_info) { 
#ifdef AZ_X86
  CodeStub* stub = new RangeCheckStub(range_check_info, index);
  if (index->is_constant()) {
    cmp_mem_int(lir_cond_belowEqual, array, arrayOopDesc::length_offset_in_bytes(),
                index->as_jint(), null_check_info);
    __ branch(lir_cond_belowEqual, T_INT, stub); // forward branch
  } else {
    cmp_reg_mem(lir_cond_aboveEqual, index, array,
                arrayOopDesc::length_offset_in_bytes(), T_INT, null_check_info);
    __ branch(lir_cond_aboveEqual, T_INT, stub); // forward branch
  }
#endif // AZ_X86
}


void LIRGenerator::nio_range_check(LIR_Opr buffer, LIR_Opr index, LIR_Opr result, CodeEmitInfo* info) {
  CodeStub* stub = new RangeCheckStub(info, index, true);
  Untested();
  if (index->is_constant()) {
    cmp_mem_int(lir_cond_belowEqual, buffer, java_nio_Buffer::limit_offset(), index->as_jint(), info);
    __ branch(lir_cond_belowEqual, T_INT, stub); // forward branch
  } else {
    cmp_reg_mem(lir_cond_aboveEqual, index, buffer,
                java_nio_Buffer::limit_offset(), T_INT, info);
    __ branch(lir_cond_aboveEqual, T_INT, stub); // forward branch
  }
  __ move(index, result);
}


// increment a counter returning the incremented value
LIR_Opr LIRGenerator::increment_and_return_counter(LIR_Opr base, int offset, int increment) {
  LIR_Address* counter = new LIR_Address(base, offset, T_INT);
  LIR_Opr result = new_register(T_INT);
  __ load(counter, result);
  __ add(result, LIR_OprFact::intConst(increment), result);
  __ store(result, counter);
  return result;
}


void LIRGenerator::arithmetic_op(Bytecodes::Code code, LIR_Opr result, LIR_Opr left, LIR_Opr right, bool is_strictfp, LIR_Opr tmp_op, CodeEmitInfo* info) {
  LIR_Opr result_op = result;
  LIR_Opr left_op   = left;
  LIR_Opr right_op  = right;

  if (TwoOperandLIRForm && left_op != result_op) {
    assert(right_op != result_op, "malformed");
    __ move(left_op, result_op);
    left_op = result_op;
  }

  switch(code) {
    case Bytecodes::_dadd:
    case Bytecodes::_fadd:
    case Bytecodes::_ladd:
    case Bytecodes::_iadd:  __ add(left_op, right_op, result_op); break;

    case Bytecodes::_fmul:
    case Bytecodes::_dmul:
      {
        if (is_strictfp) {
          __ mul_strictfp(left_op, right_op, result_op, tmp_op); break;
        } else {
          __ mul(left_op, right_op, result_op); break;
        }
      }
      break;

case Bytecodes::_lmul:
    case Bytecodes::_imul:
      {
        bool    did_strength_reduce = false;

        if (right->is_constant()) {
          intptr_t c = code == Bytecodes::_imul ? right->as_jint() : right->as_jlong();
          if (is_power_of_2(c)) {
            // do not need tmp here
            __ shift_left(left_op, exact_log2(c), result_op);
            did_strength_reduce = true;
          } else {
            did_strength_reduce = strength_reduce_multiply(left_op, c, result_op, tmp_op);
          }
        }
        // we couldn't strength reduce so just emit the multiply
        if (!did_strength_reduce) {
          __ mul(left_op, right_op, result_op);
        }
      }
      break;

    case Bytecodes::_dsub:
    case Bytecodes::_fsub:
    case Bytecodes::_lsub:
    case Bytecodes::_isub: __ sub(left_op, right_op, result_op); break;

    case Bytecodes::_fdiv: __ div (left_op, right_op, result_op); break;
    // ldiv and lrem are implemented with a direct runtime call

    case Bytecodes::_ddiv:
      {
        if (is_strictfp) {
          __ div_strictfp (left_op, right_op, result_op, tmp_op); break;
        } else {
          __ div (left_op, right_op, result_op); break;
        }
      }
      break;

    case Bytecodes::_drem:
    case Bytecodes::_frem: __ rem (left_op, right_op, result_op); break;

    default: ShouldNotReachHere();
  }
}


void LIRGenerator::arithmetic_op_cpu(Bytecodes::Code code, LIR_Opr result, LIR_Opr left, LIR_Opr right, LIR_Opr tmp, CodeEmitInfo* info) {
  arithmetic_op(code, result, left, right, false, tmp, info);
}


void LIRGenerator::arithmetic_op_fpu(Bytecodes::Code code, LIR_Opr result, LIR_Opr left, LIR_Opr right, bool is_strictfp, LIR_Opr tmp) {
  arithmetic_op(code, result, left, right, is_strictfp, tmp);
}


void LIRGenerator::shift_op(Bytecodes::Code code, LIR_Opr result_op, LIR_Opr value, LIR_Opr count, LIR_Opr tmp) {
  if (TwoOperandLIRForm && value != result_op) {
    assert(count != result_op, "malformed");
    __ move(value, result_op);
    value = result_op;
  }

  assert(count->is_constant() || count->is_register(), "must be");
  switch(code) {
  case Bytecodes::_ishl:
  case Bytecodes::_lshl: __ shift_left(value, count, result_op, tmp); break;
  case Bytecodes::_ishr:
  case Bytecodes::_lshr: __ shift_right(value, count, result_op, tmp); break;
  case Bytecodes::_iushr:
  case Bytecodes::_lushr: __ unsigned_shift_right(value, count, result_op, tmp); break;
  default: ShouldNotReachHere();
  }
}


void LIRGenerator::logic_op (Bytecodes::Code code, LIR_Opr result_op, LIR_Opr left_op, LIR_Opr right_op) {
  if (TwoOperandLIRForm && left_op != result_op) {
    assert(right_op != result_op, "malformed");
    __ move(left_op, result_op);
    left_op = result_op;
  }

  switch(code) {
    case Bytecodes::_iand:
    case Bytecodes::_land:  __ logical_and(left_op, right_op, result_op); break;

    case Bytecodes::_ior:
    case Bytecodes::_lor:   __ logical_or(left_op, right_op, result_op);  break;

    case Bytecodes::_ixor:
    case Bytecodes::_lxor:  __ logical_xor(left_op, right_op, result_op); break;

    default: ShouldNotReachHere();
  }
}


void LIRGenerator::monitor_enter(LIR_Opr object,LIR_Opr tid,LIR_Opr mark,LIR_Opr tmp,int mon_num,CodeEmitInfo*info_for_exception,CodeEmitInfo*info){
  if (!GenerateSynchronizationCode) return;
  // for slow path, use debug info for state after successful locking
CodeStub*slow_path=new MonitorEnterStub(object,info);
  // for handling NullPointerException, use debug info representing just the lock stack before this monitorenter
__ lock_object(object,tid,mark,tmp,mon_num,slow_path,info_for_exception);
}


void LIRGenerator::monitor_exit(LIR_Opr object,LIR_Opr mark,LIR_Opr tmp,int mon_num){
  if (!GenerateSynchronizationCode) return;
CodeStub*slow_path=new MonitorExitStub(object,mark);
__ unlock_object(object,mark,tmp,mon_num,slow_path);
}


void LIRGenerator::new_instance(LIR_Opr dst, ciInstanceKlass* klass, LIR_Opr size_in_bytes, LIR_Opr length_ekid, LIR_Opr akid, LIR_Opr tmp1, CodeEmitInfo* info) {
  // If klass is not loaded we do not know if the klass has finalizers:
bool always_slow_path=!UseFastNewInstance||!klass->is_loaded()||
Klass::layout_helper_needs_slow_path(klass->layout_helper());
LIR_Opr klass_reg;
if(always_slow_path){
klass_reg=new_register(T_OBJECT);
    jobject2reg_with_patching(klass_reg, klass, info);
  } else {
klass_reg=LIR_OprFact::illegalOpr;
  }
CodeStub*slow_path=new NewInstanceStub(dst,klass,info);

  assert(always_slow_path || klass->is_loaded(), "must be loaded");
  // allocate space for instance
  assert(always_slow_path || klass->size_helper() >= 0, "illegal instance size");
  __ allocate_object(klass, size_in_bytes, length_ekid, akid, tmp1, dst, klass_reg, !klass->is_initialized(), always_slow_path, slow_path);
}


static bool is_constant_zero(Instruction* inst) {
  IntConstant* c = inst->type()->as_IntConstant();
  if (c) {
    return (c->value() == 0);
  }
  return false;
}


static bool positive_constant(Instruction* inst) {
  IntConstant* c = inst->type()->as_IntConstant();
  if (c) {
    return (c->value() >= 0);
  }
  return false;
}

static bool can_overlap(Instruction* src_pos, Instruction* dst_pos, Instruction* length) {
IntConstant*s=src_pos->type()->as_IntConstant();
IntConstant*d=dst_pos->type()->as_IntConstant();
IntConstant*l=length->type()->as_IntConstant();
  if ((s == NULL) || (d == NULL) || (l == NULL)) {
    return true;
  }
  int sv = s->value();
  int dv = d->value();
  int lv = l->value();
  if ((sv+lv <= dv) && (sv < dv)) return false;
  if ((dv+lv <= sv) && (dv < sv)) return false;
  return true;
}

static ciArrayKlass* as_array_klass(ciType* type) {
  if (type != NULL && type->is_array_klass() && type->is_loaded()) {
    return (ciArrayKlass*)type;
  } else {
    return NULL;
  }
}

void LIRGenerator::arraycopy_helper(Intrinsic* x, int* flagsp, ciArrayKlass** expected_typep) {
  Instruction* src     = x->argument_at(0);
  Instruction* src_pos = x->argument_at(1);
  Instruction* dst     = x->argument_at(2);
  Instruction* dst_pos = x->argument_at(3);
  Instruction* length  = x->argument_at(4);

  // first try to identify the likely type of the arrays involved
  ciArrayKlass* expected_type = NULL;
  bool is_exact = false;
  {
    ciArrayKlass* src_exact_type    = as_array_klass(src->exact_type());
    ciArrayKlass* src_declared_type = as_array_klass(src->declared_type());
    ciArrayKlass* dst_exact_type    = as_array_klass(dst->exact_type());
    ciArrayKlass* dst_declared_type = as_array_klass(dst->declared_type());
    if (src_exact_type != NULL && src_exact_type == dst_exact_type) {
      // the types exactly match so the type is fully known
      is_exact = true;
      expected_type = src_exact_type;
    } else if (dst_exact_type != NULL && dst_exact_type->is_obj_array_klass()) {
      ciArrayKlass* dst_type = (ciArrayKlass*) dst_exact_type;
      ciArrayKlass* src_type = NULL;
      if (src_exact_type != NULL && src_exact_type->is_obj_array_klass()) {
        src_type = (ciArrayKlass*) src_exact_type;
      } else if (src_declared_type != NULL && src_declared_type->is_obj_array_klass()) {
        src_type = (ciArrayKlass*) src_declared_type;
      }
      if (src_type != NULL) {
        if (src_type->element_type()->is_subtype_of(dst_type->element_type())) {
          is_exact = true;
          expected_type = dst_type;
        }
      }
    }
    // at least pass along a good guess
    if (expected_type == NULL) expected_type = dst_exact_type;
    if (expected_type == NULL) expected_type = src_declared_type;
    if (expected_type == NULL) expected_type = dst_declared_type;
  }

  // if a probable array type has been identified, figure out if any
  // of the required checks for a fast case can be elided.
  int flags = LIR_OpArrayCopy::all_flags;
  if (expected_type != NULL) {
    // try to skip null checks
if(src->as_NewArray()!=NULL){
      flags &= ~LIR_OpArrayCopy::src_null_check;
    }
    if (dst->as_NewArray() != NULL) {
      flags &= ~LIR_OpArrayCopy::dst_null_check;
    }
    // check from incoming constant values
    if (positive_constant(src_pos))
      flags &= ~LIR_OpArrayCopy::src_pos_positive_check;
    if (positive_constant(dst_pos))
      flags &= ~LIR_OpArrayCopy::dst_pos_positive_check;
    if (positive_constant(length))
      flags &= ~LIR_OpArrayCopy::length_positive_check;
    if (!can_overlap(src_pos, dst_pos, length)) {
flags&=~LIR_OpArrayCopy::overlap_check;
    }
    // see if the range check can be elided, which might also imply
    // that src or dst is non-null.
    ArrayLength* al = length->as_ArrayLength();
    if (al != NULL) {
      if (al->array() == src) {
        // it's the length of the source array
        flags &= ~LIR_OpArrayCopy::length_positive_check;
        flags &= ~LIR_OpArrayCopy::src_null_check;
        if (is_constant_zero(src_pos))
          flags &= ~LIR_OpArrayCopy::src_range_check;
      }
      if (al->array() == dst) {
        // it's the length of the destination array
        flags &= ~LIR_OpArrayCopy::length_positive_check;
        flags &= ~LIR_OpArrayCopy::dst_null_check;
        if (is_constant_zero(dst_pos))
          flags &= ~LIR_OpArrayCopy::dst_range_check;
      }
    }
    if (is_exact) {
      flags &= ~LIR_OpArrayCopy::type_check;
    }
  }

  if (src == dst) {
    // moving within a single array so no type checks are needed
    if (flags & LIR_OpArrayCopy::type_check) {
      flags &= ~LIR_OpArrayCopy::type_check;
    }
  }
  *flagsp = flags;
  *expected_typep = (ciArrayKlass*)expected_type;
}


LIR_Opr LIRGenerator::force_to_spill(LIR_Opr value, BasicType t) {
  assert(type2size[t] == type2size[value->type()], "size mismatch");
  if (!value->is_register()) {
    // force into a register
    LIR_Opr r = new_register(value->type());
    __ move(value, r);
    value = r;
  }

  // create a spill location
  LIR_Opr tmp = new_register(t);
  set_vreg_flag(tmp, LIRGenerator::must_start_in_memory);

  // move from register to spill
  __ move(value, tmp);
  return tmp;
}


void LIRGenerator::profile_branch(If* if_instr, If::Condition cond) {
  if (if_instr->should_profile()) {
    Unimplemented();
    // ciMethod* method = if_instr->profiled_method();
    // assert(method != NULL, "method should be set if branch is profiled");
    // ciMethodData* md = method->method_data();
    // if (md == NULL) {
    //   bailout("out of memory building methodDataOop");
    //   return;
    // }
    // ciProfileData* data = md->bci_to_data(if_instr->profiled_bci());
    // assert(data != NULL, "must have profiling data");
    // assert(data->is_BranchData(), "need BranchData for two-way branches");
    // int taken_count_offset     = md->byte_offset_of_slot(data, BranchData::taken_offset());
    // int not_taken_count_offset = md->byte_offset_of_slot(data, BranchData::not_taken_offset());
    // LIR_Opr md_reg = new_register(T_OBJECT);
    // __ move(LIR_OprFact::oopConst(md->encoding()), md_reg);
    // LIR_Opr data_offset_reg = new_register(T_INT);
    // __ cmove(lir_cond(cond),
    //          LIR_OprFact::intConst(taken_count_offset),
    //          LIR_OprFact::intConst(not_taken_count_offset),
    //          data_offset_reg);
    // LIR_Opr data_reg = new_register(T_INT);
    // LIR_Address* data_addr = new LIR_Address(md_reg, data_offset_reg, T_INT);
    // __ move(LIR_OprFact::address(data_addr), data_reg);
    // LIR_Address* fake_incr_value = new LIR_Address(data_reg, DataLayout::counter_increment, T_INT);
    // // Use leal instead of add to avoid destroying condition codes on x86
    // __ leal(LIR_OprFact::address(fake_incr_value), data_reg);
    // __ move(data_reg, LIR_OprFact::address(data_addr));
  }
}


// Phi technique:
// This is about passing live values from one basic block to the other.
// In code generated with Java it is rather rare that more than one
// value is on the stack from one basic block to the other.
// We optimize our technique for efficient passing of one value
// (of type long, int, double..) but it can be extended.
// When entering or leaving a basic block, all registers and all spill
// slots are release and empty. We use the released registers
// and spill slots to pass the live values from one block
// to the other. The topmost value, i.e., the value on TOS of expression
// stack is passed in registers. All other values are stored in spilling
// area. Every Phi has an index which designates its spill slot
// At exit of a basic block, we fill the register(s) and spill slots.
// At entry of a basic block, the block_prolog sets up the content of phi nodes
// and locks necessary registers and spilling slots.


// move current value to referenced phi function
void LIRGenerator::move_to_phi(PhiResolver* resolver, Value cur_val, Value sux_val) {
  Phi* phi = sux_val->as_Phi();
  // cur_val can be null without phi being null in conjunction with inlining
  if (phi != NULL && cur_val != NULL && cur_val != phi && !phi->is_illegal()) {
    LIR_Opr operand = cur_val->operand();
    if (cur_val->operand()->is_illegal()) {
      assert(cur_val->as_Constant() != NULL || cur_val->as_Local() != NULL,
             "these can be produced lazily");
      operand = operand_for_instruction(cur_val);
    }
    resolver->move(operand, operand_for_instruction(phi));    
  }
}


// Moves all stack values into their PHI position
void LIRGenerator::move_to_phi(ValueStack* cur_state) {
  BlockBegin* bb = block();
  if (bb->number_of_sux() == 1) {
    BlockBegin* sux = bb->sux_at(0);
    assert(sux->number_of_preds() > 0, "invalid CFG");

    // a block with only one predecessor never has phi functions
    if (sux->number_of_preds() > 1) {
      int max_phis = cur_state->stack_size() + cur_state->locals_size();
      PhiResolver resolver(this, _virtual_register_number + max_phis * 2);

      ValueStack* sux_state = sux->state();
      Value sux_value;
      int index;

      for_each_stack_value(sux_state, index, sux_value) {
        move_to_phi(&resolver, cur_state->stack_at(index), sux_value);
      }

      // Inlining may cause the local state not to match up, so walk up
      // the caller state until we get to the same scope as the
      // successor and then start processing from there.
      while (cur_state->scope() != sux_state->scope()) {
        cur_state = cur_state->caller_state();
        assert(cur_state != NULL, "scopes don't match up");
      }

      for_each_local_value(sux_state, index, sux_value) {
        move_to_phi(&resolver, cur_state->local_at(index), sux_value);
      }

      assert(cur_state->caller_state() == sux_state->caller_state(), "caller states must be equal");
    }
  }
}


LIR_Opr LIRGenerator::new_register(BasicType type) {
  int vreg = _virtual_register_number;
  // add a little fudge factor for the bailout, since the bailout is
  // only checked periodically.  This gives a few extra registers to
  // hand out before we really run out, which helps us keep from
  // tripping over assertions.
  if (vreg + 20 >= LIR_OprDesc::vreg_max) {
    bailout("out of virtual registers");
    if (vreg + 2 >= LIR_OprDesc::vreg_max) {
      // wrap it around
      _virtual_register_number = LIR_OprDesc::vreg_base;
    }
  }
  _virtual_register_number += 1;
if(type==T_ADDRESS)type=T_LONG;
  return LIR_OprFact::virtual_register(vreg, type);
}


// Try to lock using register in hint
LIR_Opr LIRGenerator::rlock(Value instr) {
  return new_register(instr->type());
}


// does an rlock and sets result
LIR_Opr LIRGenerator::rlock_result(Value x) {
  LIR_Opr reg = rlock(x);
  set_result(x, reg);
  return reg;
}


// does an rlock and sets result
LIR_Opr LIRGenerator::rlock_result(Value x, BasicType type) {
  LIR_Opr reg;
  switch (type) {
  case T_BYTE:
  case T_BOOLEAN:
    reg = rlock_byte(type);
    break;
  default:
    reg = rlock(x);
    break;
  }

  set_result(x, reg);
  return reg;
}


//---------------------------------------------------------------------
ciObject* LIRGenerator::get_jobject_constant(Value value) {
  ObjectType* oc = value->type()->as_ObjectType();
  if (oc) {
    return oc->constant_value();
  }
  return NULL;
}

//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//                        visitor functions
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------

void LIRGenerator::do_Phi(Phi* x) {
  // phi functions are never visited directly
  ShouldNotReachHere();
}


// Code for a constant is generated lazily unless the constant is frequently used and can't be inlined.
void LIRGenerator::do_Constant(Constant* x) {
  if (x->state() != NULL) {
    // Any constant with a ValueStack requires patching so emit the patch here
    LIR_Opr reg = rlock_result(x);
    CodeEmitInfo* info = state_for(x, x->state());
    __ oop2reg_patch(NULL, reg, info);
  } else if (x->use_count() > 1 && !can_inline_as_constant(x)) {
    if (!x->is_pinned()) {
      // unpinned constants are handled specially so that they can be
      // put into registers when they are used multiple times within a
      // block.  After the block completes their operand will be
      // cleared so that other blocks can't refer to that register.
      set_result(x, load_constant(x));
    } else {
      LIR_Opr res = x->operand();
      if (!res->is_valid()) {
        res = LIR_OprFact::value_type(x->type());
      }
      if (res->is_constant()) {
        LIR_Opr reg = rlock_result(x);
        __ move(res, reg);
      } else {
        set_result(x, res);
      }
    }
  } else {
    set_result(x, LIR_OprFact::value_type(x->type()));
  }
}


void LIRGenerator::do_Local(Local* x) {
  // operand_for_instruction has the side effect of setting the result
  // so there's no need to do it here.
  operand_for_instruction(x);
}


void LIRGenerator::do_IfInstanceOf(IfInstanceOf* x) {
  // Disabled in both Azul and JavaSoft code base, see comments in c1_Canonicalizer
  Unimplemented();
}


void LIRGenerator::do_Return(Return* x) {
  if (x->type()->is_void()) {
    __ return_op(LIR_OprFact::illegalOpr);
  } else {
    LIR_Opr reg = result_register_for(x->type(), /*callee=*/true);
    LIRItem result(x->result(), this);

    result.load_item_force(reg);
    __ return_op(result.result());
  }
  set_no_result(x);
}


// Example: object.getClass ()
void LIRGenerator::do_getClass(Intrinsic* x) {
  assert(x->number_of_arguments() == 1, "wrong type");

  LIRItem rcvr(x->argument_at(0), this);
  rcvr.load_item();
rcvr.set_destroys_register();
  LIR_Opr result = rlock_result(x);

  // need to perform the null check on the rcvr
  CodeEmitInfo* info = NULL;
  if (x->needs_null_check()) {
    info = state_for(x, x->state()->copy_locks());
  }
LIR_Opr tmp1=new_register(T_LONG);
  __ ref2klass(result,rcvr.result(), tmp1);
LIR_Opr tmp2=new_register(T_OBJECT);
  if( !MultiMapMetaData ) tmp2 = tmp2->set_destroyed();
__ move(result,tmp2);
__ move(new LIR_Address(tmp2,Klass::java_mirror_offset_in_bytes()+
                          klassOopDesc::klass_part_offset_in_bytes(), T_OBJECT),
result,info);
}


// Example: Thread.currentThread()
void LIRGenerator::do_currentThread(Intrinsic* x) {
  assert(x->number_of_arguments() == 0, "wrong type");
  LIR_Opr reg = rlock_result(x);
  __ load(new LIR_Address(getThreadPointer(), in_bytes(JavaThread::threadObj_offset()), T_OBJECT), reg);
}


void LIRGenerator::do_RegisterFinalizer(Intrinsic* x) {
  assert(x->number_of_arguments() == 1, "wrong type");
  LIRItem receiver(x->argument_at(0), this);

  receiver.load_item();
  BasicTypeList signature;
  signature.append(T_OBJECT); // receiver
  LIR_OprList* args = new LIR_OprList();
  args->append(receiver.result());
  CodeEmitInfo* info = state_for(x, x->state());
  call_runtime(&signature, args,
               CAST_FROM_FN_PTR(address, Runtime1::entry_for(Runtime1::register_finalizer_id)),
ThreadLocals->_voidType,info);

  set_no_result(x);
}


//------------------------local access--------------------------------------

LIR_Opr LIRGenerator::operand_for_instruction(Instruction* x) {
  if (x->operand()->is_illegal()) {
    Constant* c = x->as_Constant();
    if (c != NULL) {
      x->set_operand(LIR_OprFact::value_type(c->type()));
    } else {
      assert(x->as_Phi() || x->as_Local() != NULL, "only for Phi and Local");
      // allocate a virtual register for this local or phi
      x->set_operand(rlock(x));
      _instruction_for_operand.at_put_grow(x->operand()->vreg_number(), x, NULL);
    }
  }
  return x->operand();
}


Instruction* LIRGenerator::instruction_for_opr(LIR_Opr opr) {
  if (opr->is_virtual()) {
    return instruction_for_vreg(opr->vreg_number());
  }
  return NULL;
}


Instruction* LIRGenerator::instruction_for_vreg(int reg_num) {
  if (reg_num < _instruction_for_operand.length()) {
    return _instruction_for_operand.at(reg_num);
  }
  return NULL;
}


void LIRGenerator::set_vreg_flag(int vreg_num, VregFlag f) {
  if (_vreg_flags.size_in_bits() == 0) {
    BitMap2D temp(100, num_vreg_flags);
    temp.clear();
    _vreg_flags = temp;
  }
  _vreg_flags.at_put_grow(vreg_num, f, true);
}

bool LIRGenerator::is_vreg_flag_set(int vreg_num, VregFlag f) {
  if (!_vreg_flags.is_valid_index(vreg_num, f)) {
    return false;
  }
  return _vreg_flags.at(vreg_num, f);
}


// Block local constant handling.  This code is useful for keeping
// unpinned constants and constants which aren't exposed in the IR in
// registers.  Unpinned Constant instructions have their operands
// cleared when the block is finished so that other blocks can't end
// up referring to their registers.

LIR_Opr LIRGenerator::load_constant(Constant* x) {
  assert(!x->is_pinned(), "only for unpinned constants");
  _unpinned_constants.append(x);
  return load_constant(LIR_OprFact::value_type(x->type())->as_constant_ptr());
}


LIR_Opr LIRGenerator::load_constant(LIR_Const* c) {
  BasicType t = c->type();
  for (int i = 0; i < _constants.length(); i++) {
    LIR_Const* other = _constants.at(i);
    if (t == other->type()) {
      switch (t) {
      case T_INT:
      case T_FLOAT:
        if (c->as_jint_bits() != other->as_jint_bits()) continue;
        break;
      case T_LONG:
      case T_DOUBLE:
        if (c->as_jint_hi_bits() != other->as_jint_hi_bits()) continue;
        if (c->as_jint_lo_bits() != other->as_jint_lo_bits()) continue;
        break;
      case T_OBJECT:
        if (c->as_jobject() != other->as_jobject()) continue;
        break;
      }
      return _reg_for_constants.at(i);
    }
  }

  LIR_Opr result = new_register(t);
  __ move((LIR_Opr)c, result);
  _constants.append(c);
  _reg_for_constants.append(result);
  return result;
}

// Various barriers

void LIRGenerator::post_barrier(LIR_OprDesc* addr, LIR_OprDesc* new_val) {
  switch (Universe::heap()->barrier_set()->kind()) {
    case BarrierSet::CardTableModRef:
    case BarrierSet::CardTableExtension:
      CardTableModRef_post_barrier(addr,  new_val);
      break;
    case BarrierSet::ModRef: 
    case BarrierSet::Other:
case BarrierSet::GenPauselessBarrier:
      // No post barriers
      break;
    default      : 
      ShouldNotReachHere();
    }
}

void LIRGenerator::CardTableModRef_post_barrier(LIR_OprDesc* addr, LIR_OprDesc* new_val) {

}


//------------------------field access--------------------------------------

// Comment copied form templateTable_i486.cpp
// ----------------------------------------------------------------------------
// Volatile variables demand their effects be made known to all CPU's in
// order.  Store buffers on most chips allow reads & writes to reorder; the
// JMM's ReadAfterWrite.java test fails in -Xint mode without some kind of
// memory barrier (i.e., it's not sufficient that the interpreter does not
// reorder volatile references, the hardware also must not reorder them).
// 
// According to the new Java Memory Model (JMM):
// (1) All volatiles are serialized wrt to each other.  
// ALSO reads & writes act as aquire & release, so:
// (2) A read cannot let unrelated NON-volatile memory refs that happen after
// the read float up to before the read.  It's OK for non-volatile memory refs
// that happen before the volatile read to float down below it.
// (3) Similar a volatile write cannot let unrelated NON-volatile memory refs
// that happen BEFORE the write float down to after the write.  It's OK for
// non-volatile memory refs that happen after the volatile write to float up
// before it.
//
// We only put in barriers around volatile refs (they are expensive), not
// _between_ memory refs (that would require us to track the flavor of the
// previous memory refs).  Requirements (2) and (3) require some barriers
// before volatile stores and after volatile loads.  These nearly cover
// requirement (1) but miss the volatile-store-volatile-load case.  This final
// case is placed after volatile-stores although it could just as well go
// before volatile-loads.


void LIRGenerator::do_StoreField(StoreField* x) {
  bool needs_patching = x->needs_patching();
  bool is_volatile = x->field()->is_volatile();
  BasicType field_type = x->field_type();
  bool is_oop = (field_type == T_ARRAY || field_type == T_OBJECT);

  CodeEmitInfo* info = NULL;
  if (needs_patching) {
    assert(x->explicit_null_check() == NULL, "can't fold null check into patching field access");
    info = state_for(x, x->state_before());
  } else if (x->needs_null_check()) {
    NullCheck* nc = x->explicit_null_check();
    if (nc == NULL) {
      info = state_for(x, x->lock_stack());
    } else {
      info = state_for(nc);
    }
  } else if (UseSBA && is_oop) {
    // Add info for oop maps that may be needed for SBA escapes
    info = state_for(x, x->lock_stack());
  }


LIRItem objitem(x->obj(),this);
  LIRItem value(x->value(),  this);

objitem.load_item();

  if (is_volatile || needs_patching) {
    // load item if field is volatile (fewer special cases for volatiles)
    // load item if field not initialized
    // load item if field not constant
    // because of code patching we cannot inline constants
    if (field_type == T_BYTE || field_type == T_BOOLEAN) { 
      value.load_byte_item();
    } else  {  
      value.load_item();
    }
  } else {
    value.load_for_store(field_type);
  }

  set_no_result(x);

  if (PrintNotLoaded && needs_patching) {
    tty->print_cr("   ###class not loaded at store_%s bci %d",
                  x->is_static() ?  "static" : "field", x->bci());
  }

  if (x->needs_null_check() && (is_oop || needs_patching || MacroAssembler::needs_explicit_null_check(x->offset()))) {
    // emit an explicit null check because the offset is too large, patching doesn't handle NPEs? and write barriers don't handle null oops
#ifdef AZ_X86
    if( !MultiMapMetaData ) {
      // AZUL x86-64 jumps to stub to throw exception to avoid stripping the ref
      CodeStub *stub = new SimpleExceptionStub(Runtime1::throw_null_pointer_exception_id,
                                               LIR_OprFact::illegalOpr, new CodeEmitInfo(info));
      __ cmp(lir_cond_equal, objitem.result(), LIR_OprFact::oopConst(NULL));
__ branch(lir_cond_equal,T_OBJECT,stub);
    } else {
__ null_check(objitem.result(),new CodeEmitInfo(info));
    }
#else
__ null_check(objitem.result(),new CodeEmitInfo(info));
#endif
  }

  LIR_Address* address;
  if( !MultiMapMetaData ) {
    // strip will destory contents of register holding the object ref
objitem.set_destroys_register();
  }
  if (needs_patching) {
    // we need to patch the offset in the instruction so don't allow
    // generate_address to try to be smart about emitting the -1.
    // Otherwise the patching code won't know how to find the
    // instruction to patch.
address=new LIR_Address(objitem.result(),max_jint,field_type);
  } else {
address=generate_address(objitem.result(),x->offset(),field_type);
  }

  if (is_volatile && os::is_MP()) {
    __ membar_release();
  }

  if (is_volatile) {
    assert(!needs_patching && x->is_loaded(),
           "how do we know it's volatile if it's not loaded");
    volatile_field_store(value.result(), address, info);
  } else {
    LIR_PatchCode patch_code = needs_patching ? lir_patch_normal : lir_patch_none;
    __ store(value.result(), address, info, patch_code);
  }

  if (is_oop) {
post_barrier(objitem.result(),value.result());
  }

  if (is_volatile && os::is_MP()) {
    __ membar();
  }
}


void LIRGenerator::do_LoadField(LoadField* x) {
  bool needs_patching = x->needs_patching();
  bool is_volatile = x->field()->is_volatile();
  BasicType field_type = x->field_type();

  CodeEmitInfo* info = NULL;
  if (needs_patching) {
    assert(x->explicit_null_check() == NULL, "can't fold null check into patching field access");
    info = state_for(x, x->state_before());
  } else if (x->needs_null_check()) {
    NullCheck* nc = x->explicit_null_check();
    if (nc == NULL) {
      info = state_for(x, x->lock_stack());
    } else {
      info = state_for(nc);
    }
  }

LIRItem objitem(x->obj(),this);

objitem.load_item();

  if (PrintNotLoaded && needs_patching) {
    tty->print_cr("   ###class not loaded at load_%s bci %d",
                  x->is_static() ?  "static" : "field", x->bci());
  }

  if (x->needs_null_check() && 
      (needs_patching || 
       MacroAssembler::needs_explicit_null_check(x->offset()))) {
    // emit an explicit null check because the offset is too large
#ifdef AZ_X86
    // AZUL x86-64 jumps to stub to throw exception to avoid stripping the ref
    CodeStub *stub = new SimpleExceptionStub(Runtime1::throw_null_pointer_exception_id,
                                             LIR_OprFact::illegalOpr, new CodeEmitInfo(info));
    __ cmp(lir_cond_equal, objitem.result(), LIR_OprFact::oopConst(NULL));
__ branch(lir_cond_equal,T_OBJECT,stub);
#else
__ null_check(objitem.result(),new CodeEmitInfo(info));
#endif
  }

  if( !MultiMapMetaData ) {
    // strip will destory contents of register holding the object ref
objitem.set_destroys_register();
  }

  LIR_Opr reg = rlock_result(x, field_type);
  LIR_Address* address;
  if (needs_patching) {
    // we need to patch the offset in the instruction so don't allow
    // generate_address to try to be smart about emitting the -1.
    // Otherwise the patching code won't know how to find the
    // instruction to patch.
address=new LIR_Address(objitem.result(),max_jint,field_type);
  } else {
address=generate_address(objitem.result(),x->offset(),field_type);
  }

  if (is_volatile) {
    assert(!needs_patching && x->is_loaded(),
           "how do we know it's volatile if it's not loaded");
    volatile_field_load(address, reg, info);
  } else {
    LIR_PatchCode patch_code = needs_patching ? lir_patch_normal : lir_patch_none;
    __ load(address, reg, info, patch_code);
  }

  if (is_volatile && os::is_MP()) {
    __ membar_acquire();
  }
}


//------------------------java.nio.Buffer.checkIndex------------------------

// int java.nio.Buffer.checkIndex(int)
void LIRGenerator::do_NIOCheckIndex(Intrinsic* x) {
  // NOTE: by the time we are in checkIndex() we are guaranteed that
  // the buffer is non-null (because checkIndex is package-private and
  // only called from within other methods in the buffer).
  assert(x->number_of_arguments() == 2, "wrong type");
  LIRItem buf  (x->argument_at(0), this);
  LIRItem index(x->argument_at(1), this);
  buf.load_item();
  index.load_item();

  LIR_Opr result = rlock_result(x);
  CodeEmitInfo* info = state_for(x);
  CodeStub* stub = new RangeCheckStub(info, index.result(), true);
  if( !MultiMapMetaData ) {
    // stripping destroys register
buf.set_destroys_register();
  }
  if (index.result()->is_constant()) {
    cmp_mem_int(lir_cond_belowEqual, buf.result(), java_nio_Buffer::limit_offset(), index.result()->as_jint(), info);
    __ branch(lir_cond_belowEqual, T_INT, stub);
  } else {
    cmp_reg_mem(lir_cond_aboveEqual, index.result(), buf.result(),
                java_nio_Buffer::limit_offset(), T_INT, info);
    __ branch(lir_cond_aboveEqual, T_INT, stub);
  }
  __ move(index.result(), result);
}


//------------------------array access--------------------------------------


void LIRGenerator::do_ArrayLength(ArrayLength* x) {
LIRItem arr(x->array(),this);
arr.load_item();
  if( !MultiMapMetaData ) {
    // strip will destory contents of register holding the object ref
arr.set_destroys_register();
  }
  LIR_Opr reg = rlock_result(x);

  CodeEmitInfo* info = NULL;
  if (x->needs_null_check()) {
    NullCheck* nc = x->explicit_null_check();
    if (nc == NULL) {
      info = state_for(x);
    } else {
      info = state_for(nc);
    }
  }
__ load(new LIR_Address(arr.result(),arrayOopDesc::length_offset_in_bytes(),T_INT),reg,info,lir_patch_none);
}


void LIRGenerator::do_LoadIndexed(LoadIndexed* x) {
  bool use_length = x->length() != NULL;
  LIRItem array(x->array(), this);
  LIRItem index(x->index(), this);
  LIRItem length(this);
  bool needs_range_check = true;

  if (use_length) {
    needs_range_check = x->compute_needs_range_check();
    if (needs_range_check) {
      length.set_instruction(x->length());
      length.load_item();
    }
  }

  array.load_item();

  if (index.is_constant() && can_inline_as_constant(x->index())) {
    // let it be a constant
    index.dont_load_item();
  } else {
    index.load_item();
  }

  CodeEmitInfo* range_check_info = state_for(x);
  CodeEmitInfo* null_check_info = NULL;
  if (x->needs_null_check()) {
    NullCheck* nc = x->explicit_null_check();
    if (nc != NULL) {
      null_check_info = state_for(nc);
    } else {
      null_check_info = range_check_info;
    }
  }

  LIR_Opr new_array1, new_array2;
  if( !MultiMapMetaData ) {
    // TODO:
    // this code is an ugly (having 2 copies of array) work around to give both
    // the array load and the array range check a fresh and destroyed register,
    // perhaps we can add smarts to the destroyed register code to detect multiple
    // uses of a result and insert the necessary copies (or blow up). See similar
    // issue in the PD LIR generator for store indexed.
    new_array1 = new_register(T_ARRAY)->set_destroyed();
    new_array2 = new_register(T_ARRAY)->set_destroyed();
    lir()->move(array.result(), new_array1);
    lir()->move(array.result(), new_array2);
  } else {
    new_array1 = array.result();
    new_array2 = array.result();
  }
  // emit array address setup early so it schedules better
LIR_Address*array_addr=emit_array_address(new_array1,index.result(),x->elt_type(),false);

if(needs_range_check){
    if (use_length) {
      __ cmp(lir_cond_belowEqual, length.result(), index.result());
      __ branch(lir_cond_belowEqual, T_INT, new RangeCheckStub(range_check_info, index.result()));
      // TODO: use a (modified) version of array_range_check that does not require a 
      //       constant length to be loaded to a register
    } else {
array_range_check(new_array2,index.result(),null_check_info,range_check_info);
      // The range check performs the null check, so clear it out for the load
      null_check_info = NULL;
    }
  }

  __ move(array_addr, rlock_result(x, x->elt_type()), null_check_info);
}


void LIRGenerator::do_NullCheck(NullCheck* x) {
  if (x->can_trap()) {
    LIRItem value(x->obj(), this);
    value.load_item();
    CodeEmitInfo* info = state_for(x);

#ifdef AZ_X86
    if( !MultiMapMetaData ) {
      // AZUL x86-64 jumps to stub to throw exception to avoid stripping the ref
      CodeStub *stub = new SimpleExceptionStub(Runtime1::throw_null_pointer_exception_id,
                                               LIR_OprFact::illegalOpr, info);
      __ cmp(lir_cond_equal, value.result(), LIR_OprFact::oopConst(NULL));
__ branch(lir_cond_equal,T_OBJECT,stub);
    } else {
      __ null_check(value.result(), info);
    }
#else
    __ null_check(value.result(), info);
#endif
  }
}


void LIRGenerator::do_Throw(Throw* x) {
  LIRItem exception(x->exception(), this);
  exception.load_item();
  set_no_result(x);
  LIR_Opr exception_opr = exception.result();
  CodeEmitInfo* info = state_for(x, x->state());

#ifndef PRODUCT
  if (PrintC1Statistics) {
    increment_counter(Runtime1::throw_count_address());
  }
#endif

  // check if the instruction has an xhandler in any of the nested scopes
  bool unwind = false;
  if (info->exception_handlers()->length() == 0) {
    // this throw is not inside an xhandler
    unwind = true;
  } else {
    // get some idea of the throw type
    bool type_is_exact = true;
    ciType* throw_type = x->exception()->exact_type();
    if (throw_type == NULL) {
      type_is_exact = false;
      throw_type = x->exception()->declared_type();
    }
    if (throw_type != NULL && throw_type->is_instance_klass()) {
      ciInstanceKlass* throw_klass = (ciInstanceKlass*)throw_type;
      unwind = !x->exception_handlers()->could_catch(throw_klass, type_is_exact);
    }
  }

  // do null check before moving exception oop into fixed register 
  // to avoid a fixed interval with an oop during the null check.  
  // Use a copy of the CodeEmitInfo because debug information is 
  // different for null_check and throw.
  if (GenerateCompilerNullChecks &&
      (x->exception()->as_NewInstance() == NULL && x->exception()->as_ExceptionObject() == NULL)) {
    // if the exception object wasn't created using new then it might be null.
#ifdef AZ_X86
    // AZUL x86-64 jumps to stub to throw exception to avoid stripping the ref
    CodeStub *stub = new SimpleExceptionStub(Runtime1::throw_null_pointer_exception_id,
                                             LIR_OprFact::illegalOpr, new CodeEmitInfo(info, true));
    __ cmp(lir_cond_equal, exception.result(), LIR_OprFact::oopConst(NULL));
__ branch(lir_cond_equal,T_OBJECT,stub);
#else
    __ null_check(exception.result(), new CodeEmitInfo(info, true));
#endif
  }

  if (JvmtiExport::can_post_exceptions() &&
      !block()->is_set(BlockBegin::default_exception_handler_flag)) {
    // we need to go through the exception lookup path to get JVMTI
    // notification done
    unwind = false;
  }

  assert(!block()->is_set(BlockBegin::default_exception_handler_flag) || unwind,
         "should be no more handlers to dispatch to");

  // move exception oop into TLS _pending_exception
  __ move(exception_opr, exceptionOopOpr());
    
  if (unwind) {
    __ unwind_exception(getThreadPointer(), exceptionOopOpr(), LIR_OprFact::illegalOpr, info);
  } else {
    __ throw_exception(getThreadPointer(),exceptionOopOpr(), exceptionPcOpr(), info);
  }
}


void LIRGenerator::do_UnsafeGetRaw(UnsafeGetRaw* x) {
  LIRItem base(x->base(), this);
  LIRItem idx(this);

  base.load_item();
  if (x->has_index()) {
    idx.set_instruction(x->index());
    idx.load_nonconstant();
  }

  LIR_Opr reg = rlock_result(x, x->basic_type());

  int   log2_scale = 0;
  if (x->has_index()) {
    assert(x->index()->type()->tag() == intTag, "should not find non-int index");
    log2_scale = x->log2_scale();
  }

  assert(!x->has_index() || idx.value() == x->index(), "should match");

  LIR_Opr base_op = base.result();

  BasicType dst_type = x->basic_type();
  LIR_Opr index_op = idx.result();
  
  LIR_Address* addr;
  if (index_op->is_constant()) {
    assert(log2_scale == 0, "must not have a scale");
    addr = new LIR_Address(base_op, index_op->as_jint(), dst_type);
  } else {
    addr = new LIR_Address(base_op, index_op, LIR_Address::Scale(log2_scale), 0, dst_type);
  }

  if (x->may_be_unaligned() && (dst_type == T_LONG || dst_type == T_DOUBLE)) {
    __ unaligned_move(addr, reg);
  } else {
    __ move(addr, reg);
  }
}


void LIRGenerator::do_UnsafePutRaw(UnsafePutRaw* x) {
  int  log2_scale = 0;
  BasicType type = x->basic_type();

  if (x->has_index()) {
    assert(x->index()->type()->tag() == intTag, "should not find non-int index");
    log2_scale = x->log2_scale();
  }

  LIRItem base(x->base(), this);
  LIRItem value(x->value(), this);
  LIRItem idx(this);
  
  base.load_item();
  if (x->has_index()) {
    idx.set_instruction(x->index());
    idx.load_item();
  }

  if (type == T_BYTE || type == T_BOOLEAN) {
    value.load_byte_item();
  } else {
    value.load_item();
  }

  set_no_result(x);

  LIR_Opr base_op = base.result();

  LIR_Opr index_op = idx.result();
  if (log2_scale != 0) {
    // temporary fix (platform dependent code without shift on Intel would be better)
    index_op = new_register(T_INT);
    __ move(idx.result(), index_op);
    __ shift_left(index_op, log2_scale, index_op);
  }

  LIR_Address* addr = new LIR_Address(base_op, index_op, x->basic_type());
  __ move(value.result(), addr);
}


void LIRGenerator::do_UnsafeGetObject(UnsafeGetObject* x) {
  BasicType type = x->basic_type();
  LIRItem src(x->object(), this);
  LIRItem off(x->offset(), this);

  off.load_item();
  src.load_item();
if(!MultiMapMetaData)src.set_destroys_register();

LIR_Opr reg=rlock_result(x,x->basic_type());

  if (x->is_volatile() && os::is_MP()) __ membar_acquire();
  get_Object_unsafe(reg, src.result(), off.result(), type, x->is_volatile());
  if (x->is_volatile() && os::is_MP()) __ membar();
}


void LIRGenerator::do_UnsafePutObject(UnsafePutObject* x) {
  BasicType type = x->basic_type();
  LIRItem src(x->object(), this);
  LIRItem off(x->offset(), this);
  LIRItem data(x->value(), this);

  src.load_item();
  if( !MultiMapMetaData ) src.set_destroys_register();
  if (type == T_BOOLEAN || type == T_BYTE) {
    data.load_byte_item();
  } else {
    data.load_item();
  }  
  off.load_item();

  set_no_result(x);

  if (x->is_volatile() && os::is_MP()) __ membar_release();
  put_Object_unsafe(src.result(), off.result(), data.result(), type, x->is_volatile());
}


void LIRGenerator::do_UnsafePrefetch(UnsafePrefetch* x, bool is_store) {
  LIRItem src(x->object(), this);
  LIRItem off(x->offset(), this);

  src.load_item();
  if (off.is_constant() && can_inline_as_constant(x->offset())) {
    // let it be a constant
    off.dont_load_item();
  } else {
    off.load_item();
  }

  set_no_result(x);

  LIR_Address* addr = generate_address(src.result(), off.result(), 0, 0, T_BYTE);
  __ prefetch(addr, is_store);
}


void LIRGenerator::do_UnsafePrefetchRead(UnsafePrefetchRead* x) {
  do_UnsafePrefetch(x, false);
}


void LIRGenerator::do_UnsafePrefetchWrite(UnsafePrefetchWrite* x) {
  do_UnsafePrefetch(x, true);
}


void LIRGenerator::do_SwitchRanges(SwitchRangeArray* x, LIR_Opr value, BlockBegin* default_sux) {
  int lng = x->length();

  for (int i = 0; i < lng; i++) {
    SwitchRange* one_range = x->at(i);
    int low_key = one_range->low_key();
    int high_key = one_range->high_key();
    BlockBegin* dest = one_range->sux();
    if (low_key == high_key) {
__ branch_cmp(lir_cond_equal,T_INT,value,low_key,dest);
    } else if (high_key - low_key == 1) {
__ branch_cmp(lir_cond_equal,T_INT,value,low_key,dest);
__ branch_cmp(lir_cond_equal,T_INT,value,high_key,dest);
    } else {
      LabelObj* L = new LabelObj();
__ branch_cmp(lir_cond_less,T_INT,value,low_key,L->label());
__ branch_cmp(lir_cond_lessEqual,T_INT,value,high_key,dest);
      __ branch_destination(L->label());
    }
  }
  __ jump(default_sux);
}


SwitchRangeArray* LIRGenerator::create_lookup_ranges(TableSwitch* x) {
  SwitchRangeList* res = new SwitchRangeList();
  int len = x->length();
  if (len > 0) {
    BlockBegin* sux = x->sux_at(0);
    int key = x->lo_key();
    BlockBegin* default_sux = x->default_sux();
    SwitchRange* range = new SwitchRange(key, sux);
    for (int i = 0; i < len; i++, key++) {
      BlockBegin* new_sux = x->sux_at(i);
      if (sux == new_sux) {
        // still in same range
        range->set_high_key(key);
      } else {
        // skip tests which explicitly dispatch to the default
        if (sux != default_sux) {
          res->append(range);
        }
        range = new SwitchRange(key, new_sux);
      }
      sux = new_sux;
    }
    if (res->length() == 0 || res->last() != range)  res->append(range);
  }
  return res;
}


// we expect the keys to be sorted by increasing value
SwitchRangeArray* LIRGenerator::create_lookup_ranges(LookupSwitch* x) {
  SwitchRangeList* res = new SwitchRangeList();
  int len = x->length();
  if (len > 0) {
    BlockBegin* default_sux = x->default_sux();
    int key = x->key_at(0);
    BlockBegin* sux = x->sux_at(0);
    SwitchRange* range = new SwitchRange(key, sux);
    for (int i = 1; i < len; i++) {
      int new_key = x->key_at(i);
      BlockBegin* new_sux = x->sux_at(i);
      if (key+1 == new_key && sux == new_sux) {
        // still in same range
        range->set_high_key(new_key);
      } else {
        // skip tests which explicitly dispatch to the default
        if (range->sux() != default_sux) {
          res->append(range);
        }
        range = new SwitchRange(new_key, new_sux);
      }
      key = new_key;
      sux = new_sux;
    }
    if (res->length() == 0 || res->last() != range)  res->append(range);
  }
  return res;
}


void LIRGenerator::do_TableSwitch(TableSwitch* x) {
  LIRItem tag(x->tag(), this);
  tag.load_item();
  set_no_result(x);

  if (x->is_safepoint()) {
    SafepointStub* slow_path = new SafepointStub(state_for(x, x->state_before()));
__ safepoint(safepoint_poll_register(),slow_path);
  }

  // move values into phi locations
  move_to_phi(x->state());

  int lo_key = x->lo_key();
  int hi_key = x->hi_key();
  int len = x->length();
  CodeEmitInfo* info = state_for(x, x->state());
  LIR_Opr value = tag.result();
  if (UseTableRanges) {
    do_SwitchRanges(create_lookup_ranges(x), value, x->default_sux());
  } else {
    Untested();
    for (int i = 0; i < len; i++) {
      __ cmp(lir_cond_equal, value, i + lo_key);
      __ branch(lir_cond_equal, T_INT, x->sux_at(i));
    }
    __ jump(x->default_sux());
  }
}


void LIRGenerator::do_LookupSwitch(LookupSwitch* x) {
  LIRItem tag(x->tag(), this);
  tag.load_item();
  set_no_result(x);

  if (x->is_safepoint()) {
    SafepointStub* slow_path = new SafepointStub(state_for(x, x->state_before()));
__ safepoint(safepoint_poll_register(),slow_path);
  }

  // move values into phi locations
  move_to_phi(x->state());

  LIR_Opr value = tag.result();
  if (UseTableRanges) {
    do_SwitchRanges(create_lookup_ranges(x), value, x->default_sux());
  } else {
    Untested();
    int len = x->length();
    for (int i = 0; i < len; i++) {
      __ cmp(lir_cond_equal, value, x->key_at(i));
      __ branch(lir_cond_equal, T_INT, x->sux_at(i));
    }
    __ jump(x->default_sux());
  }
}


void LIRGenerator::do_Goto(Goto* x) {
  set_no_result(x);

  if (block()->next()->as_OsrEntry()) {
    // need to free up storage used for OSR entry point
    LIR_Opr osrBuffer = block()->next()->operand();
    BasicTypeList signature;
    signature.append(T_INT);
    CallingConvention* cc = frame_map()->c_calling_convention(&signature);
    __ move(osrBuffer, cc->args()->at(0));
    __ call_runtime_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::OSR_migration_end),
LIR_OprFact::illegalOpr,cc->args());
  }

  if (x->is_safepoint()) {
    ValueStack* state = x->state_before() ? x->state_before() : x->state();

    // increment backedge counter if needed
    increment_backedge_counter(state_for(x, state));

    SafepointStub* slow_path = new SafepointStub(state_for(x, state));
__ safepoint(safepoint_poll_register(),slow_path);
  }

  // emit phi-instruction move after safepoint since this simplifies
  // describing the state as the safepoint.
  move_to_phi(x->state());

  __ jump(x->default_sux());
}


void LIRGenerator::do_OsrEntry(OsrEntry* x) {
  // construct our frame and model the production of incoming pointer
  // to the OSR buffer.
  __ osr_entry(LIR_Assembler::osrBufferPointer());
  LIR_Opr result = rlock_result(x);
  __ move(LIR_Assembler::osrBufferPointer(), result);
}


void LIRGenerator::invoke_load_arguments(Invoke* x, LIRItemList* args, const LIR_OprList* arg_list) {
  int i = x->has_receiver() ? 1 : 0;
  for (; i < args->length(); i++) {
    LIRItem* param = args->at(i);
    LIR_Opr loc = arg_list->at(i);
    if (loc->is_register()) {
      param->load_item_force(loc);
    } else {
      LIR_Address* addr = loc->as_address_ptr();
      param->load_for_store(addr->type());
      if (addr->type() == T_LONG || addr->type() == T_DOUBLE) {
        __ unaligned_move(param->result(), addr);
      } else {
        __ move(param->result(), addr);
      }
    }
  }

  if (x->has_receiver()) {
    LIRItem* receiver = args->at(0);
    LIR_Opr loc = arg_list->at(0);
    if (loc->is_register()) {
      receiver->load_item_force(loc);
    } else {
      assert(loc->is_address(), "just checking");
      receiver->load_for_store(T_OBJECT);
      __ move(receiver->result(), loc);
    }
  }
}


// Visits all arguments, returns appropriate items without loading them
LIRItemList* LIRGenerator::invoke_visit_arguments(Invoke* x) {
  LIRItemList* argument_items = new LIRItemList();
  if (x->has_receiver()) {
    LIRItem* receiver = new LIRItem(x->receiver(), this);
    argument_items->append(receiver);
  }
  int idx = x->has_receiver() ? 1 : 0;
  for (int i = 0; i < x->number_of_arguments(); i++) {
    LIRItem* param = new LIRItem(x->argument_at(i), this);
    argument_items->append(param);
    idx += (param->type()->is_double_word() ? 2 : 1);
  }
  return argument_items;
}


// The invoke with receiver has following phases:
//   a) traverse and load/lock receiver;
//   b) traverse all arguments -> item-array (invoke_visit_argument)
//   c) push receiver on stack
//   d) load each of the items and push on stack
//   e) unlock receiver
//   f) move receiver into receiver-register %o0
//   g) lock result registers and emit call operation
//
// Before issuing a call, we must spill-save all values on stack
// that are in caller-save register. "spill-save" moves thos registers
// either in a free callee-save register or spills them if no free
// callee save register is available.
//
// The problem is where to invoke spill-save.
// - if invoked between e) and f), we may lock callee save
//   register in "spill-save" that destroys the receiver register
//   before f) is executed
// - if we rearange the f) to be earlier, by loading %o0, it
//   may destroy a value on the stack that is currently in %o0
//   and is waiting to be spilled
// - if we keep the receiver locked while doing spill-save,
//   we cannot spill it as it is spill-locked
//
void LIRGenerator::do_Invoke(Invoke* x) {
  CallingConvention* cc = frame_map()->java_calling_convention(x->signature(), true);

  LIR_OprList* arg_list = cc->args();
  LIRItemList* args = invoke_visit_arguments(x);
  LIR_Opr receiver = LIR_OprFact::illegalOpr;

  // setup result register
  LIR_Opr result_register = LIR_OprFact::illegalOpr;
if(x->type()!=ThreadLocals->_voidType){
    result_register = result_register_for(x->type());
  }

  CodeEmitInfo* info = state_for(x, x->state());
  info->set_cpdoff(x->cpdoff());

  invoke_load_arguments(x, args, arg_list);

  if (x->has_receiver()) {
    args->at(0)->load_item_force(LIR_Assembler::receiverOpr());
    receiver = args->at(0)->result();
  }

  // emit invoke code
  bool optimized = x->target_is_loaded() && x->target_is_final();
  assert(receiver->is_illegal() || receiver->is_equal(LIR_Assembler::receiverOpr()), "must match");

  switch (x->code()) {
    case Bytecodes::_invokestatic:
       __ call_static(x->target(), result_register,
StubRoutines::resolve_and_patch_call_entry(),
                      arg_list, info);
      break;
    case Bytecodes::_invokespecial:
    case Bytecodes::_invokevirtual:
    case Bytecodes::_invokeinterface:
      // for final target we still produce an inline cache, in order
      // to be able to call mixed mode
      if (x->code() == Bytecodes::_invokespecial || optimized) {
        __ call_opt_virtual(x->target(), receiver, result_register,
StubRoutines::resolve_and_patch_call_entry(),
                            arg_list, info);
      } else if (x->vtable_index() < 0) {
        __ call_icvirtual(x->target(), receiver, result_register,
StubRoutines::resolve_and_patch_call_entry(),
                          arg_list, info);
      } else {
        int entry_offset = instanceKlass::vtable_start_offset() + x->vtable_index() * vtableEntry::size();
        int vtable_offset = entry_offset * wordSize + vtableEntry::method_offset_in_bytes();
        __ call_virtual(x->target(), receiver, result_register, vtable_offset, arg_list, info);
      }
      break;
    default:
      ShouldNotReachHere();
      break;
  }

  if (result_register->is_valid()) {
    LIR_Opr result = rlock_result(x);
    __ move(result_register, result);
  }
}


void LIRGenerator::do_FPIntrinsics(Intrinsic* x) {
vmIntrinsics::ID id=x->id();
  assert(id == vmIntrinsics::_intBitsToFloat ||
         id == vmIntrinsics::_doubleToRawLongBits ||
         id == vmIntrinsics::_longBitsToDouble ||
         id == vmIntrinsics::_floatToRawIntBits, "unexpected intrinsic");

  LIRItem value (x->argument_at(0), this);
  LIR_Opr reg = rlock_result(x);
  value.load_item();
  assert(x->number_of_arguments() == 1, "wrong type");
#ifdef AZ_X86
  // Register-to-register moves are ok in AZ_X86
LIR_Opr tmp=value.result();
#else
  // Force memory-to-register move
  LIR_Opr tmp = force_to_spill(value.result(), as_BasicType(x->type()));
#endif
  __ move(tmp, reg);
}



// Code for  :  x->x() {x->cond()} x->y() ? x->tval() : x->fval()
void LIRGenerator::do_IfOp(IfOp* x) {
#ifdef ASSERT
  {
    ValueTag xtag = x->x()->type()->tag();
    ValueTag ttag = x->tval()->type()->tag();
    assert(xtag == intTag || xtag == objectTag, "cannot handle others");
    assert(ttag == addressTag || ttag == intTag || ttag == objectTag || ttag == longTag, "cannot handle others");
    assert(ttag == x->fval()->type()->tag(), "cannot handle others");
  }
#endif

  LIRItem left(x->x(), this);
  LIRItem right(x->y(), this);
  left.load_item();
  if (can_inline_as_constant(right.value())) {
    right.dont_load_item();
  } else {
    right.load_item();
  }

  LIRItem t_val(x->tval(), this);
  LIRItem f_val(x->fval(), this);
  t_val.dont_load_item();
  f_val.dont_load_item();
  LIR_Opr reg = rlock_result(x);

LIR_Opr tmp=LIR_OprFact::illegalOpr;
#ifdef AZ_X86
if(x->tval()->type()->tag()==objectTag){
t_val.load_item();
tmp=new_register(T_LONG);
  }
#endif

  __ cmp(lir_cond(x->cond()), left.result(), right.result());
__ cmove(lir_cond(x->cond()),t_val.result(),f_val.result(),reg,tmp);
}


void LIRGenerator::do_Intrinsic(Intrinsic* x) {
  switch (x->id()) {
  case vmIntrinsics::_intBitsToFloat      :
  case vmIntrinsics::_doubleToRawLongBits :
  case vmIntrinsics::_longBitsToDouble    :
  case vmIntrinsics::_floatToRawIntBits   : {
    do_FPIntrinsics(x);
    break;
  }

  case vmIntrinsics::_currentTimeMillis: {
    assert(x->number_of_arguments() == 0, "wrong type");
    LIR_Opr reg = result_register_for(x->type());
__ call_runtime_leaf(CAST_FROM_FN_PTR(address,os::javaTimeMillis),
                         reg, new LIR_OprList());
    LIR_Opr result = rlock_result(x);
    __ move(reg, result);
    break;
  }

  case vmIntrinsics::_nanoTime: {
    assert(x->number_of_arguments() == 0, "wrong type");
    LIR_Opr reg = result_register_for(x->type());
__ call_runtime_leaf(CAST_FROM_FN_PTR(address,os::javaTimeNanos),
                         reg, new LIR_OprList());
    LIR_Opr result = rlock_result(x);
    __ move(reg, result);
    break;
  }

  case vmIntrinsics::_Object_init:    do_RegisterFinalizer(x); break;
  case vmIntrinsics::_getClass:       do_getClass(x);      break;
  case vmIntrinsics::_currentThread:  do_currentThread(x); break;

  case vmIntrinsics::_dlog:           // fall through
  case vmIntrinsics::_dlog10:         // fall through
  case vmIntrinsics::_dabs:           // fall through
  case vmIntrinsics::_dsqrt:          // fall through
  case vmIntrinsics::_dtan:           // fall through
  case vmIntrinsics::_dsin :          // fall through
  case vmIntrinsics::_dcos :          do_MathIntrinsic(x); break;
  case vmIntrinsics::_arraycopy:      do_ArrayCopy(x);     break;
case vmIntrinsics::_stringEquals:do_StringEquals(x);break;

  // java.nio.Buffer.checkIndex
  case vmIntrinsics::_checkIndex:     do_NIOCheckIndex(x); break;

  case vmIntrinsics::_compareAndSwapObject: 
do_CompareAndSwap(x,ThreadLocals->_objectType);
    break;
  case vmIntrinsics::_compareAndSwapInt: 
do_CompareAndSwap(x,ThreadLocals->_intType);
    break;
  case vmIntrinsics::_compareAndSwapLong: 
do_CompareAndSwap(x,ThreadLocals->_longType);
    break;

    // sun.misc.AtomicLongCSImpl.attemptUpdate
  case vmIntrinsics::_attemptUpdate:
    do_AttemptUpdate(x);
    break;

  default: ShouldNotReachHere(); break;
  }
}


void LIRGenerator::do_ProfileCall(ProfileCall* x) {
  // Need recv in a temporary register so it interferes with the other temporaries
  LIR_Opr recv = LIR_OprFact::illegalOpr;
  LIR_Opr mdo = new_register(T_OBJECT);
  LIR_Opr tmp = new_register(T_INT);
  if (x->recv() != NULL) {
    LIRItem value(x->recv(), this);
    value.load_item();
    recv = new_register(T_OBJECT);
    __ move(value.result(), recv);
  }
  __ profile_call(x->method(), x->bci_of_invoke(), mdo, recv, tmp, x->known_holder());
}


void LIRGenerator::do_ProfileCounter(ProfileCounter* x) {
  LIRItem mdo(x->mdo(), this);
  mdo.load_item();
  
  increment_counter(new LIR_Address(mdo.result(), x->offset(), T_INT), x->increment());
}


LIR_Opr LIRGenerator::call_runtime(Value arg1, address entry, ValueType* result_type, CodeEmitInfo* info) {
  LIRItemList args(1);
  LIRItem value(arg1, this);
  args.append(&value);
  BasicTypeList signature;
  signature.append(as_BasicType(arg1->type()));

  return call_runtime(&signature, &args, entry, result_type, info);
}


LIR_Opr LIRGenerator::call_runtime(Value arg1, Value arg2, address entry, ValueType* result_type, CodeEmitInfo* info) {
  LIRItemList args(2);
  LIRItem value1(arg1, this);
  LIRItem value2(arg2, this);
  args.append(&value1);
  args.append(&value2);
  BasicTypeList signature;
  signature.append(as_BasicType(arg1->type()));
  signature.append(as_BasicType(arg2->type()));

  return call_runtime(&signature, &args, entry, result_type, info);
}


LIR_Opr LIRGenerator::call_runtime(BasicTypeArray* signature, LIR_OprList* args,
                                   address entry, ValueType* result_type, CodeEmitInfo* info) {
  // get a result register
  LIR_Opr phys_reg = LIR_OprFact::illegalOpr;
  LIR_Opr result = LIR_OprFact::illegalOpr;
  if (result_type->tag() != voidTag) {
    result = new_register(result_type);
    phys_reg = result_register_for(result_type);
  }
  
  // move the arguments into the correct location
  CallingConvention* cc = frame_map()->c_calling_convention(signature);
  assert(cc->length() == args->length(), "argument mismatch");
  for (int i = 0; i < args->length(); i++) {
    LIR_Opr arg = args->at(i);
    LIR_Opr loc = cc->at(i);
    if (loc->is_register()) {
      __ move(arg, loc);
    } else {
      LIR_Address* addr = loc->as_address_ptr();
//           if (!can_store_as_constant(arg)) {
//             LIR_Opr tmp = new_register(arg->type());
//             __ move(arg, tmp);
//             arg = tmp;
//           }
      if (addr->type() == T_LONG || addr->type() == T_DOUBLE) {
        __ unaligned_move(arg, addr);
      } else {
        __ move(arg, addr);
      }
    }
  }
      
  if (info) {
__ call_runtime(entry,phys_reg,cc->args(),info);
  } else {
__ call_runtime_leaf(entry,phys_reg,cc->args());
  }
  if (result->is_valid()) {
    __ move(phys_reg, result);
  }
  return result;
}


LIR_Opr LIRGenerator::call_runtime(BasicTypeArray* signature, LIRItemList* args,
                                   address entry, ValueType* result_type, CodeEmitInfo* info) {
  // get a result register
  LIR_Opr phys_reg = LIR_OprFact::illegalOpr;
  LIR_Opr result = LIR_OprFact::illegalOpr;
  if (result_type->tag() != voidTag) {
    result = new_register(result_type);
    phys_reg = result_register_for(result_type);
  }
  
  // move the arguments into the correct location
  CallingConvention* cc = frame_map()->c_calling_convention(signature);

  assert(cc->length() == args->length(), "argument mismatch");
  for (int i = 0; i < args->length(); i++) {
    LIRItem* arg = args->at(i);
    LIR_Opr loc = cc->at(i);
    if (loc->is_register()) {
      arg->load_item_force(loc);
    } else {
      LIR_Address* addr = loc->as_address_ptr();
      arg->load_for_store(addr->type());
      if (addr->type() == T_LONG || addr->type() == T_DOUBLE) {
        __ unaligned_move(arg->result(), addr);
      } else {
        __ move(arg->result(), addr);
      }
    }
  }
  
  if (info) {
__ call_runtime(entry,phys_reg,cc->args(),info);
  } else {
__ call_runtime_leaf(entry,phys_reg,cc->args());
  }
  if (result->is_valid()) {
    __ move(phys_reg, result);
  }
  return result;
}



void LIRGenerator::increment_invocation_counter(CodeEmitInfo* info, bool backedge) {
#ifdef TIERED
  if (_compilation->env()->comp_level() == CompLevel_fast_compile &&
      (method()->code_size() >= Tier1BytecodeLimit || backedge)) {
    int limit = InvocationCounter::Tier1InvocationLimit;
    int offset = in_bytes(methodOopDesc::invocation_counter_offset() +
                          InvocationCounter::counter_offset());
    if (backedge) {
      limit = InvocationCounter::Tier1BackEdgeLimit;
      offset = in_bytes(methodOopDesc::backedge_counter_offset() +
                        InvocationCounter::counter_offset());
    }

    LIR_Opr meth = new_register(T_OBJECT);
    __ oop2reg(method()->encoding(), meth);
    LIR_Opr result = increment_and_return_counter(meth, offset, InvocationCounter::count_increment);
    __ cmp(lir_cond_aboveEqual, result, LIR_OprFact::intConst(limit));
    CodeStub* overflow = new CounterOverflowStub(info, info->bci());
    __ branch(lir_cond_aboveEqual, T_INT, overflow);
    __ branch_destination(overflow->continuation());
  }
#endif
}


