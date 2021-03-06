/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "bytecodeStream.hpp"
#include "classFileParser.hpp"
#include "classLoader.hpp"
#include "ciEnv.hpp"
#include "collectedHeap.hpp"
#include "dictionary.hpp"
#include "fieldType.hpp"
#include "gcLocker.hpp"
#include "handles.hpp"
#include "instanceRefKlass.hpp"
#include "javaCalls.hpp"
#include "javaClasses.hpp"
#include "jvmtiExport.hpp"
#include "loaderConstraints.hpp"
#include "methodCodeOop.hpp"
#include "oopFactory.hpp"
#include "ostream.hpp"
#include "placeholders.hpp"
#include "reflectionUtils.hpp"
#include "refsHierarchy_pd.hpp"
#include "resolutionErrors.hpp"
#include "resourceArea.hpp"
#include "signature.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"
#include "thread.hpp"
#include "vmSymbols.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "oop.inline.hpp"
#include "thread_os.inline.hpp"

Dictionary*       SystemDictionary::_dictionary = NULL;
PlaceholderTable* SystemDictionary::_placeholders = NULL;
Dictionary*       SystemDictionary::_shared_dictionary = NULL;
LoaderConstraintTable* SystemDictionary::_loader_constraints = NULL;
ResolutionErrorTable* SystemDictionary::_resolution_errors = NULL;


intptr_t    SystemDictionary::_number_of_modifications = 0;

objectRef   SystemDictionary::_system_loader_lock_obj;

klassRef    SystemDictionary::_object_klass;
klassRef    SystemDictionary::_string_klass;
klassRef    SystemDictionary::_class_klass;
klassRef    SystemDictionary::_cloneable_klass;
klassRef    SystemDictionary::_classloader_klass;
klassRef    SystemDictionary::_serializable_klass;
klassRef    SystemDictionary::_system_klass;

klassRef    SystemDictionary::_throwable_klass;
klassRef    SystemDictionary::_error_klass;
klassRef    SystemDictionary::_threaddeath_klass;
klassRef    SystemDictionary::_exception_klass;
klassRef    SystemDictionary::_runtime_exception_klass;
klassRef    SystemDictionary::_classNotFoundException_klass;
klassRef    SystemDictionary::_noClassDefFoundError_klass;
klassRef    SystemDictionary::_linkageError_klass;
klassRef    SystemDictionary::_nullPointerException_klass;
klassRef    SystemDictionary::_arrayIndexOutOfBoundsException_klass;
klassRef    SystemDictionary::_classCastException_klass;
klassRef    SystemDictionary::_arrayStoreException_klass;
klassRef    SystemDictionary::_virtualMachineError_klass;
klassRef    SystemDictionary::_outOfMemoryError_klass;
klassRef    SystemDictionary::_StackOverflowError_klass;
klassRef    SystemDictionary::_illegalMonitorStateException_klass;
klassRef    SystemDictionary::_protectionDomain_klass;
klassRef    SystemDictionary::_AccessControlContext_klass;

klassRef    SystemDictionary::_reference_klass;
klassRef    SystemDictionary::_soft_reference_klass;
klassRef    SystemDictionary::_weak_reference_klass;
klassRef    SystemDictionary::_final_reference_klass;
klassRef    SystemDictionary::_phantom_reference_klass;
klassRef    SystemDictionary::_finalizer_klass;

klassRef    SystemDictionary::_thread_klass;
klassRef    SystemDictionary::_threadGroup_klass;
klassRef    SystemDictionary::_properties_klass;
klassRef    SystemDictionary::_reflect_accessible_object_klass;
klassRef    SystemDictionary::_reflect_field_klass;
klassRef    SystemDictionary::_reflect_method_klass;
klassRef    SystemDictionary::_reflect_constructor_klass;
klassRef    SystemDictionary::_reflect_magic_klass;
klassRef    SystemDictionary::_reflect_method_accessor_klass;
klassRef    SystemDictionary::_reflect_constructor_accessor_klass;
klassRef    SystemDictionary::_reflect_delegating_classloader_klass;
klassRef    SystemDictionary::_reflect_constant_pool_klass;
klassRef    SystemDictionary::_reflect_unsafe_static_field_accessor_impl_klass;

klassRef    SystemDictionary::_vector_klass;
klassRef    SystemDictionary::_hashtable_klass;
klassRef    SystemDictionary::_stringBuffer_klass;

klassRef    SystemDictionary::_stackTraceElement_klass;

klassRef    SystemDictionary::_java_nio_Buffer_klass;

klassRef    SystemDictionary::_sun_misc_AtomicLongCSImpl_klass;
klassRef    SystemDictionary::_com_azulsystems_util_Prefetch_klass;
klassRef    SystemDictionary::_com_azulsystems_misc_BlockingHint_klass;
klassRef    SystemDictionary::_abstract_ownable_synchronizer_klass;

klassRef    SystemDictionary::_boolean_klass;
klassRef    SystemDictionary::_char_klass;
klassRef    SystemDictionary::_float_klass;
klassRef    SystemDictionary::_double_klass;
klassRef    SystemDictionary::_byte_klass;
klassRef    SystemDictionary::_short_klass;
klassRef    SystemDictionary::_int_klass;
klassRef    SystemDictionary::_long_klass;
klassRef    SystemDictionary::_box_klasses[T_VOID+1];

objectRef   SystemDictionary::_java_system_loader;

bool        SystemDictionary::_has_loadClassInternal      =  false;
bool        SystemDictionary::_has_checkPackageAccess     =  false;

void systemdictionary_static_init() {
  SystemDictionary::_system_loader_lock_obj     = nullRef;

  SystemDictionary::_object_klass               = nullRef;
  SystemDictionary::_string_klass               = nullRef;
  SystemDictionary::_class_klass                = nullRef;
  SystemDictionary::_cloneable_klass            = nullRef;
  SystemDictionary::_classloader_klass          = nullRef;
  SystemDictionary::_serializable_klass         = nullRef;
  SystemDictionary::_system_klass               = nullRef;

  SystemDictionary::_throwable_klass            = nullRef;
  SystemDictionary::_error_klass                = nullRef;
  SystemDictionary::_threaddeath_klass          = nullRef;
  SystemDictionary::_exception_klass            = nullRef;
  SystemDictionary::_runtime_exception_klass    = nullRef;
  SystemDictionary::_classNotFoundException_klass=nullRef;
  SystemDictionary::_noClassDefFoundError_klass = nullRef;
  SystemDictionary::_linkageError_klass         = nullRef;
  SystemDictionary::_classCastException_klass   = nullRef;
  SystemDictionary::_arrayStoreException_klass  = nullRef;
  SystemDictionary::_virtualMachineError_klass  = nullRef;
  SystemDictionary::_outOfMemoryError_klass     = nullRef;
  SystemDictionary::_StackOverflowError_klass   = nullRef;
  SystemDictionary::_illegalMonitorStateException_klass = nullRef;
  SystemDictionary::_protectionDomain_klass     = nullRef;
  SystemDictionary::_AccessControlContext_klass = nullRef;

  SystemDictionary::_reference_klass            =  nullRef;
  SystemDictionary::_soft_reference_klass       =  nullRef;
  SystemDictionary::_weak_reference_klass       =  nullRef;
  SystemDictionary::_final_reference_klass      =  nullRef;
  SystemDictionary::_phantom_reference_klass    =  nullRef;
  SystemDictionary::_finalizer_klass            =  nullRef;

  SystemDictionary::_thread_klass               =  nullRef;
  SystemDictionary::_threadGroup_klass          =  nullRef;
  SystemDictionary::_properties_klass           =  nullRef;
  SystemDictionary::_reflect_accessible_object_klass =  nullRef;
  SystemDictionary::_reflect_field_klass        =  nullRef;
  SystemDictionary::_reflect_method_klass       =  nullRef;
  SystemDictionary::_reflect_constructor_klass  =  nullRef;
  SystemDictionary::_reflect_magic_klass        =  nullRef;
  SystemDictionary::_reflect_method_accessor_klass = nullRef;
  SystemDictionary::_reflect_constructor_accessor_klass = nullRef;
  SystemDictionary::_reflect_delegating_classloader_klass = nullRef;

  SystemDictionary::_vector_klass               =  nullRef;
  SystemDictionary::_hashtable_klass            =  nullRef;
  SystemDictionary::_stringBuffer_klass         =  nullRef;

  SystemDictionary::_java_nio_Buffer_klass      =  nullRef;

  SystemDictionary::_sun_misc_AtomicLongCSImpl_klass = nullRef;
  SystemDictionary::_com_azulsystems_util_Prefetch_klass = nullRef;
  SystemDictionary::_com_azulsystems_misc_BlockingHint_klass = nullRef;

  SystemDictionary::_abstract_ownable_synchronizer_klass = nullRef;

  SystemDictionary::_boolean_klass              =  nullRef;
  SystemDictionary::_char_klass                 =  nullRef;
  SystemDictionary::_float_klass                =  nullRef;
  SystemDictionary::_double_klass               =  nullRef;
  SystemDictionary::_byte_klass                 =  nullRef;
  SystemDictionary::_short_klass                =  nullRef;
  SystemDictionary::_int_klass                  =  nullRef;
  SystemDictionary::_long_klass                 =  nullRef;
  {
    for (int i = 0; i < T_VOID+1; i++) {
      SystemDictionary::_box_klasses[i] = nullRef;
    }
  }
  
  SystemDictionary::_java_system_loader         =  nullRef;;
}


// ----------------------------------------------------------------------------
// Java-level SystemLoader

oop SystemDictionary::java_system_loader() {
return lvb_ref(&_java_system_loader).as_oop();
}

void SystemDictionary::compute_java_system_loader(TRAPS) {
KlassHandle system_klass(THREAD,classloader_klass());
  JavaValue result(T_OBJECT);
  JavaCalls::call_static(&result, 
                         system_klass,
                         vmSymbolHandles::getSystemClassLoader_name(),
                         vmSymbolHandles::void_classloader_signature(),
                         CHECK);
    
  POISON_AND_STORE_REF(&_java_system_loader, *(objectRef*) result.get_value_addr());
}


// ----------------------------------------------------------------------------
// debugging

#ifdef ASSERT

// return true if class_name contains no '.' (internal format is '/')
bool SystemDictionary::is_internal_format(symbolHandle class_name) {
  if (class_name.not_null()) {
    ResourceMark rm;
    char* name = class_name->as_C_string();
    return strchr(name, '.') == NULL;
  } else {
    return true;
  }
}

#endif

// ----------------------------------------------------------------------------
// Resolving of classes

// Forwards to resolve_or_null

klassOop SystemDictionary::resolve_or_fail(symbolHandle class_name, Handle class_loader, Handle protection_domain, bool throw_error, TRAPS) {  
  klassOop klass = resolve_or_null(class_name, class_loader, protection_domain, THREAD);
  if (HAS_PENDING_EXCEPTION || klass == NULL) {
    KlassHandle k_h(THREAD, klass);
    // can return a null klass
    klass = handle_resolution_exception(class_name, class_loader, protection_domain, throw_error, k_h, THREAD);
  }
  return klass;
}

klassOop SystemDictionary::handle_resolution_exception(symbolHandle class_name, Handle class_loader, Handle protection_domain, bool throw_error, KlassHandle klass_h, TRAPS) {
  if (HAS_PENDING_EXCEPTION) {
    // If we have a pending exception we forward it to the caller, unless throw_error is true,
    // in which case we have to check whether the pending exception is a ClassNotFoundException,
    // and if so convert it to a NoClassDefFoundError
    // And chain the original ClassNotFoundException
    if (throw_error && PENDING_EXCEPTION->is_a(SystemDictionary::classNotFoundException_klass())) {
      ResourceMark rm(THREAD);
      assert(klass_h() == NULL, "Should not have result with exception pending");
      Handle e(THREAD, PENDING_EXCEPTION);
      CLEAR_PENDING_EXCEPTION;
      THROW_MSG_CAUSE_0(vmSymbols::java_lang_NoClassDefFoundError(), class_name->as_C_string(), e);
    } else {
      return NULL; 
    }
  }
  // Class not found, throw appropriate error or exception depending on value of throw_error
  if (klass_h() == NULL) {
    ResourceMark rm(THREAD);
    if (throw_error) {
      THROW_MSG_0(vmSymbols::java_lang_NoClassDefFoundError(), class_name->as_C_string());
    } else {      
      THROW_MSG_0(vmSymbols::java_lang_ClassNotFoundException(), class_name->as_C_string());      
    }
  }
  return (klassOop)klass_h(); 
}


klassOop SystemDictionary::resolve_or_fail(symbolHandle class_name,
                                           bool throw_error, TRAPS)
{
  return resolve_or_fail(class_name, Handle(), Handle(), throw_error, THREAD);
}


// Forwards to resolve_instance_class_or_null

klassOop SystemDictionary::resolve_or_null(symbolHandle class_name, Handle class_loader, Handle protection_domain, TRAPS) {  
  assert(!THREAD->is_Compiler_thread(), "Can not load classes with the Compiler thread");
  if (FieldType::is_array(class_name())) {
    return resolve_array_class_or_null(class_name, class_loader, protection_domain, CHECK_NULL);
  } else {
    return resolve_instance_class_or_null(class_name, class_loader, protection_domain, CHECK_NULL);
  }
}

klassOop SystemDictionary::resolve_or_null(symbolHandle class_name, TRAPS) {  
  return resolve_or_null(class_name, Handle(), Handle(), THREAD);
}

// Forwards to resolve_instance_class_or_null

klassOop SystemDictionary::resolve_array_class_or_null(symbolHandle class_name,
                                                       Handle class_loader, 
                                                       Handle protection_domain,
                                                       TRAPS) {  
  assert(FieldType::is_array(class_name()), "must be array");
  jint dimension;
  symbolOop object_key;
  klassOop k = NULL;  
  // dimension and object_key are assigned as a side-effect of this call
  BasicType t = FieldType::get_array_info(class_name(), 
                                          &dimension, 
                                          &object_key, 
                                          CHECK_NULL);

  if (t == T_OBJECT) {
    symbolHandle h_key(THREAD, object_key);
    // naked oop "k" is OK here -- we assign back into it
    k = SystemDictionary::resolve_instance_class_or_null(h_key, 
                                                         class_loader, 
                                                         protection_domain, 
                                                         CHECK_NULL);
    if (k != NULL) {
      klassRef kref = Klass::array_klass(klassRef(k), dimension, CHECK_NULL);
      k = kref.as_klassOop();
    }
  } else {
    k = Universe::typeArrayKlassObj(t);
    klassRef kref = Klass::array_klass(klassRef(k), dimension, CHECK_NULL);
    k = kref.as_klassOop();
  }
  return k;
}


// Must be called for any super-class or super-interface resolution
// during class definition to allow class circularity checking
// super-interface callers: 
//    parse_interfaces - for defineClass & jvmtiRedefineClasses
// super-class callers:
//   ClassFileParser - for defineClass & jvmtiRedefineClasses
//   load_shared_class - while loading a class from shared archive
//   resolve_instance_class_or_fail:
//      when resolving a class that has an existing placeholder with
//      a saved superclass [i.e. a defineClass is currently in progress]
//      if another thread is trying to resolve the class, it must do
//      super-class checks on its own thread to catch class circularity
// This last call is critical in class circularity checking for cases
// where classloading is delegated to different threads and the
// classloader lock is released.
// Take the case: Base->Super->Base
//   1. If thread T1 tries to do a defineClass of class Base
//    resolve_super_or_fail creates placeholder: T1, Base (super Super)
//   2. resolve_instance_class_or_null does not find SD or placeholder for Super
//    so it tries to load Super
//   3. If we load the class internally, or user classloader uses same thread
//      loadClassFromxxx or defineClass via parseClassFile Super ...
//      3.1 resolve_super_or_fail creates placeholder: T1, Super (super Base) 
//      3.3 resolve_instance_class_or_null Base, finds placeholder for Base
//      3.4 calls resolve_super_or_fail Base
//      3.5 finds T1,Base -> throws class circularity
//OR 4. If T2 tries to resolve Super via defineClass Super ...
//      4.1 resolve_super_or_fail creates placeholder: T2, Super (super Base) 
//      4.2 resolve_instance_class_or_null Base, finds placeholder for Base (super Super)
//      4.3 calls resolve_super_or_fail Super in parallel on own thread T2
//      4.4 finds T2, Super -> throws class circularity
// Must be called, even if superclass is null, since this is
// where the placeholder entry is created which claims this
// thread is loading this class/classloader.
klassOop SystemDictionary::resolve_super_or_fail(symbolHandle child_name,
                                                 symbolHandle class_name,
                                                 Handle class_loader,
                                                 Handle protection_domain,
                                                 bool is_superclass,
                                                 TRAPS) {

  // Double-check, if child class is already loaded, just return super-class,interface
  // Don't add a placedholder if already loaded, i.e. already in system dictionary
  // Make sure there's a placeholder for the *child* before resolving.
  // Used as a claim that this thread is currently loading superclass/classloader
  // Used here for ClassCircularity checks and also for heap verification
  // (every instanceKlass in the heap needs to be in the system dictionary
  // or have a placeholder).
  // Must check ClassCircularity before checking if super class is already loaded
  //
  // We might not already have a placeholder if this child_name was
  // first seen via resolve_from_stream (jni_DefineClass or JVM_DefineClass);
  // the name of the class might not be known until the stream is actually
  // parsed.
  // Bugs 4643874, 4715493
  // compute_hash can have a safepoint

  unsigned int d_hash = dictionary()->compute_hash(child_name, class_loader);
  int d_index = dictionary()->hash_to_index(d_hash);
  unsigned int p_hash = placeholders()->compute_hash(child_name, class_loader);
  int p_index = placeholders()->hash_to_index(p_hash);
  // can't throw error holding a lock
  bool child_already_loaded = false;
  bool throw_circularity_error = false;
  {
    
    assert0(Thread::current() == Thread::current()->sba_thread());
    StackBasedAllocation::ensure_in_heap(Thread::current()->sba_thread(), class_loader, "SystemDictionary"); 
    
    MutexLocker mu(SystemDictionary_lock, THREAD);
    klassOop childk = find_class(d_index, d_hash, child_name, class_loader);
    klassOop quicksuperk;
    // to support // loading: if child done loading, just return superclass
    // if class_name, & class_loader don't match:
    // if initial define, SD update will give LinkageError
    // if redefine: compare_class_versions will give HIERARCHY_CHANGED
    // so we don't throw an exception here.
    // see: nsk redefclass014 & java.lang.instrument Instrument032
    if ((childk != NULL ) && (is_superclass) &&
       ((quicksuperk = instanceKlass::cast(childk)->super()) != NULL) &&
      
         ((Klass::cast(quicksuperk)->name() == class_name()) && 
            (Klass::cast(quicksuperk)->class_loader()  == class_loader()))) {
           return quicksuperk;
    } else {
      PlaceholderEntry* probe = placeholders()->get_entry(p_index, p_hash, child_name, class_loader);
      if (probe && probe->check_seen_thread(THREAD, PlaceholderTable::LOAD_SUPER)) {
          throw_circularity_error = true;
      } 

      // add placeholder entry even if error - callers will remove on error
      PlaceholderEntry* newprobe = placeholders()->find_and_add(p_index, p_hash, child_name, class_loader, PlaceholderTable::LOAD_SUPER, class_name, THREAD); 
      if (throw_circularity_error) {
         newprobe->remove_seen_thread(THREAD, PlaceholderTable::LOAD_SUPER);
      }
    }
  }
  if (throw_circularity_error) {
      ResourceMark rm(THREAD);
      THROW_MSG_0(vmSymbols::java_lang_ClassCircularityError(), child_name->as_C_string());
  }

// java.lang.Object should have been found above
  assert(class_name() != NULL, "null super class for resolving");
  // Resolve the super class or interface, check results on return
  klassOop superk = NULL;
  superk = SystemDictionary::resolve_or_null(class_name,
                                                 class_loader,
                                                 protection_domain,
                                                 THREAD);
  
  KlassHandle superk_h(THREAD, superk);
  
  // Note: clean up of placeholders currently in callers of
  // resolve_super_or_fail - either at update_dictionary time
  // or on error 
  {
  MutexLocker mu(SystemDictionary_lock, THREAD);
   PlaceholderEntry* probe = placeholders()->get_entry(p_index, p_hash, child_name, class_loader);
   if (probe != NULL) {
      probe->remove_seen_thread(THREAD, PlaceholderTable::LOAD_SUPER);
   }
  }
  if (HAS_PENDING_EXCEPTION || superk_h() == NULL) {
    // can null superk
    superk_h = KlassHandle(THREAD, handle_resolution_exception(class_name, class_loader, protection_domain, true, superk_h, THREAD));
  }

  return superk_h();
}


void SystemDictionary::validate_protection_domain(instanceKlassHandle klass,
                                                  Handle class_loader,
                                                  Handle protection_domain,
                                                  TRAPS) {
  if(!has_checkPackageAccess()) return;

  // Now we have to call back to java to check if the initating class has access
  JavaValue result(T_VOID);
  if (TraceProtectionDomainVerification) {
    // Print out trace information
    tty->print_cr("Checking package access");
    tty->print(" - class loader:      "); class_loader()->print_value_on(tty);      tty->cr();
    tty->print(" - protection domain: "); protection_domain()->print_value_on(tty); tty->cr();
    tty->print(" - loading:           "); klass()->print_value_on(tty);             tty->cr();
  }
  
  assert(class_loader() != NULL, "should not have non-null protection domain for null classloader");

  KlassHandle system_loader(THREAD, SystemDictionary::classloader_klass());
  JavaCalls::call_special(&result,
                         class_loader,
                         system_loader,
                         vmSymbolHandles::checkPackageAccess_name(),
                         vmSymbolHandles::class_protectiondomain_signature(), 
                         Handle(THREAD, klass->java_mirror()),
                         protection_domain,
                         THREAD);

  if (TraceProtectionDomainVerification) {
    if (HAS_PENDING_EXCEPTION) {
      tty->print_cr(" -> DENIED !!!!!!!!!!!!!!!!!!!!!");
    } else {
     tty->print_cr(" -> granted");
    }
    tty->cr();
  }

  if (HAS_PENDING_EXCEPTION) return; 
    
  // If no exception has been thrown, we have validated the protection domain
  // Insert the protection domain of the initiating class into the set.
  {
    // We recalculate the entry here -- we've called out to java since
    // the last time it was calculated.
    symbolHandle kn(THREAD, klass->name());
    unsigned int d_hash = dictionary()->compute_hash(kn, class_loader);
    int d_index = dictionary()->hash_to_index(d_hash);

    MutexLocker mu(SystemDictionary_lock, THREAD);
    { 
      // Note that we have an entry, and entries can be deleted only during GC,
      // so we cannot allow GC to occur while we're holding this entry.

      // We're using a No_Safepoint_Verifier to catch any place where we
      // might potentially do a GC at all.
      // SystemDictionary::do_unloading() asserts that classes are only
      // unloaded at a safepoint.
      No_Safepoint_Verifier nosafepoint;
      dictionary()->add_protection_domain(d_index, d_hash, klass, class_loader,
                                          protection_domain, THREAD);
    }
  }
}

// We only get here if this thread finds that another thread
// has already claimed the placeholder token for the current operation,
// but that other thread either never owned or gave up the
// object lock
// Waits on SystemDictionary_lock to indicate placeholder table updated
// On return, caller must recheck placeholder table state
//
// We only get here if 
//  1) custom classLoader, i.e. not bootstrap classloader
//  2) UnsyncloadClass not set
//  3) custom classLoader has broken the class loader objectLock
//     so another thread got here in parallel
//
// lockObject must be held. 
// Complicated dance due to lock ordering:
// Must first release the classloader object lock to
// allow initial definer to complete the class definition
// and to avoid deadlock
// Reclaim classloader lock object with same original recursion count
// Must release SystemDictionary_lock after notify, since
// class loader lock must be claimed before SystemDictionary_lock
// to prevent deadlocks
//
// The notify allows applications that did an untimed wait() on
// the classloader object lock to not hang.
void SystemDictionary::double_lock_wait(Handle lockObject, TRAPS) {
  assert_lock_strong(SystemDictionary_lock);

  bool calledholdinglock = lockObject()->is_self_locked();
  assert(calledholdinglock,"must hold lock for notify");
  assert(!UnsyncloadClass, "unexpected double_lock_wait");
ObjectSynchronizer::notifyall(lockObject(),THREAD);
//At this point we hold 3 locks:
  // 1- JVM_lock; JVM_lock must be held to touch naked oops and prevents GC
  // 2- lockObject; an oop (class loader object lock).  Must be held before taking Sys Dict to avoid deadlock
  // 3- SystemDictionary_lock
  // Collect the tests that fail this; at this time, this is probably causing a hang in JCKs.
  ObjectSynchronizer::unlock_recursively(lockObject()); // unlock 2
  ((JavaThread*)THREAD)->jvm_unlock_self(); // unlock 1
  SystemDictionary_lock.wait(); // unlock 3, wait(), then relock it - then unlock again (hideous!)
  MutexUnlocker_GC_on_Relock mugcrelk(SystemDictionary_lock,true); // unlock 3 
  // No locks held
  ((JavaThread*)THREAD)->jvm_lock_self(); // lock 1
  // Reacquire the lockObject lock withOUT holding the SystemDictionary_lock
  // set recursion count, if applicable
  ObjectSynchronizer::lock_recursively(lockObject); // lock 2
  // lock 3: At this point we re-acquire the SystemDictionary_lock, possibly
  // allowing a GC.  
}

// If the class in is in the placeholder table, class loading is in progress
// For cases where the application changes threads to load classes, it
// is critical to ClassCircularity detection that we try loading
// the superclass on the same thread internally, so we do parallel
// super class loading here.
// This also is critical in cases where the original thread gets stalled
// even in non-circularity situations.
// Note: only one thread can define the class, but multiple can resolve
// Note: must call resolve_super_or_fail even if null super -
// to force placeholder entry creation for this class
// Caller must check for pending exception
// Returns non-null klassOop if other thread has completed load
// and we are done, 
// If return null klassOop and no pending exception, the caller must load the class
instanceKlassHandle SystemDictionary::handle_parallel_super_load(
    symbolHandle name, symbolHandle superclassname, Handle class_loader, 
    Handle protection_domain, Handle lockObject, TRAPS) {

  instanceKlassHandle nh = instanceKlassHandle(); // null Handle
  unsigned int d_hash = dictionary()->compute_hash(name, class_loader);
  int d_index = dictionary()->hash_to_index(d_hash);
  unsigned int p_hash = placeholders()->compute_hash(name, class_loader);
  int p_index = placeholders()->hash_to_index(p_hash);

  // superk is not used, resolve_super called for circularity check only
  // This code is reached in two situations. One if this thread
  // is loading the same class twice (e.g. ClassCircularity, or 
  // java.lang.instrument).
  // The second is if another thread started the resolve_super first
  // and has not yet finished. 
  // In both cases the original caller will clean up the placeholder
  // entry on error.
  klassOop superk = SystemDictionary::resolve_super_or_fail(name,
                                                          superclassname,
                                                          class_loader,
                                                          protection_domain,
                                                          true,
                                                          CHECK_(nh));
  // We don't redefine the class, so we just need to clean up if there
  // was not an error (don't want to modify any system dictionary
  // data structures).
  {
    MutexLocker mu(SystemDictionary_lock, THREAD);
    placeholders()->find_and_remove(p_index, p_hash, name, class_loader, THREAD);
SystemDictionary_lock.notify_all();
  }

  // UnsyncloadClass does NOT wait for parallel superclass loads to complete
  // Bootstrap classloader does wait for parallel superclass loads
 if (UnsyncloadClass) {
    MutexLocker mu(SystemDictionary_lock, THREAD);
    // Check if classloading completed while we were loading superclass or waiting
    klassOop check = find_class(d_index, d_hash, name, class_loader);
    if (check != NULL) {
      // Klass is already loaded, so just return it
      return(instanceKlassHandle(THREAD, check));
    } else {
      return nh;
    }
  } 

  // must loop to both handle other placeholder updates
  // and spurious notifications
  bool super_load_in_progress = true;
  PlaceholderEntry* placeholder;
  while (super_load_in_progress) {
    MutexLocker mu(SystemDictionary_lock, THREAD);
    // Check if classloading completed while we were loading superclass or waiting
    klassOop check = find_class(d_index, d_hash, name, class_loader);
    if (check != NULL) {
      // Klass is already loaded, so just return it
      return(instanceKlassHandle(THREAD, check));
    } else {
      placeholder = placeholders()->get_entry(p_index, p_hash, name, class_loader);
      if (placeholder && placeholder->super_load_in_progress() ){
        // Before UnsyncloadClass:
        // We only get here if the application has released the
        // classloader lock when another thread was in the middle of loading a
        // superclass/superinterface for this class, and now
        // this thread is also trying to load this class.
        // To minimize surprises, the first thread that started to
        // load a class should be the one to complete the loading
        // with the classfile it initially expected.
        // This logic has the current thread wait once it has done
        // all the superclass/superinterface loading it can, until
        // the original thread completes the class loading or fails
        // If it completes we will use the resulting instanceKlass
        // which we will find below in the systemDictionary.
        // We also get here for parallel bootstrap classloader
        if (class_loader.is_null()) {
          // Azul - TODO - check whether this can be cleaned up
((JavaThread*)THREAD)->jvm_unlock_self();
          SystemDictionary_lock.wait();
          MutexUnlocker_GC_on_Relock mugcrelk(SystemDictionary_lock,true);
((JavaThread*)THREAD)->jvm_lock_self();
        } else {
          double_lock_wait(lockObject, THREAD);
        }
      } else {
        // If not in SD and not in PH, other thread's load must have failed
        super_load_in_progress = false;
      }
    }
  }
  return (nh);
}


klassOop SystemDictionary::resolve_instance_class_or_null(symbolHandle class_name, Handle class_loader, Handle protection_domain, TRAPS) {
  assert(class_name.not_null() && !FieldType::is_array(class_name()), "invalid class name");
  assert0( THREAD->is_Java_thread() );
  JavaThread *jt = (JavaThread*)THREAD;
  // First check to see if we should remove wrapping L and ;
  symbolHandle name;    
  if (FieldType::is_obj(class_name())) {
    ResourceMark rm(THREAD);
    // Ignore wrapping L and ;.
    name = oopFactory::new_symbol_handle(class_name()->as_C_string() + 1, class_name()->utf8_length() - 2, CHECK_NULL);    
  } else {
    name = class_name;
  }

  // UseNewReflection
  // Fix for 4474172; see evaluation for more details
  class_loader = Handle(THREAD, java_lang_ClassLoader::non_reflection_class_loader(class_loader()));

  // Do lookup to see if class already exist and the protection domain
  // has the right access
  unsigned int d_hash = dictionary()->compute_hash(name, class_loader);
  int d_index = dictionary()->hash_to_index(d_hash);
  klassOop probe = dictionary()->find(d_index, d_hash, name, class_loader,
                                      protection_domain, THREAD);
  if (probe != NULL) return probe;


  // Non-bootstrap class loaders will call out to class loader and
  // define via jvm/jni_DefineClass which will acquire the
  // class loader object lock to protect against multiple threads
  // defining the class in parallel by accident.
  // This lock must be acquired here so the waiter will find
  // any successful result in the SystemDictionary and not attempt
  // the define
  // Classloaders that support parallelism, e.g. bootstrap classloader,
  // or all classloaders with UnsyncloadClass do not acquire lock here
  bool DoObjectLock = true;
  if (UnsyncloadClass || (class_loader.is_null())) {
    DoObjectLock = false;
  }

  unsigned int p_hash = placeholders()->compute_hash(name, class_loader);
  int p_index = placeholders()->hash_to_index(p_hash);

  // Class is not in SystemDictionary so we have to do loading.
  // Make sure we are synchronized on the class loader before we proceed
objectRef lockObjref=compute_loader_lock_object(class_loader,THREAD);
check_loader_lock_contention(lockObjref,THREAD);
  ObjectLocker ol(DoObjectLock ? lockObjref : nullRef);

  // Check again (after locking) if class already exist in SystemDictionary
  bool class_has_been_loaded   = false;
  bool super_load_in_progress  = false;
  bool havesupername = false;
  instanceKlassHandle k;
  PlaceholderEntry* placeholder;
  symbolHandle superclassname;

  {           
    
    assert0(Thread::current() == Thread::current()->sba_thread());
    StackBasedAllocation::ensure_in_heap(Thread::current()->sba_thread(), class_loader, "SystemDictionary"); 
    
MutexLocker mu(SystemDictionary_lock,jt);
    klassOop check = find_class(d_index, d_hash, name, class_loader);
    if (check != NULL) {
      // Klass is already loaded, so just return it
      class_has_been_loaded = true;
      k = instanceKlassHandle(THREAD, check);
    } else {
      placeholder = placeholders()->get_entry(p_index, p_hash, name, class_loader);
      if (placeholder && placeholder->super_load_in_progress()) {
         super_load_in_progress = true;
         if (placeholder->havesupername() == true) {
           superclassname = symbolHandle(THREAD, placeholder->supername());
           havesupername = true;
         }
      } 
    }
  }

  // If the class in is in the placeholder table, class loading is in progress
  if (super_load_in_progress && havesupername==true) {
    k = SystemDictionary::handle_parallel_super_load(name, superclassname, 
class_loader,protection_domain,ol._h,THREAD);
    if (HAS_PENDING_EXCEPTION) {
      return NULL;
    }
    if (!k.is_null()) {
      class_has_been_loaded = true;
    }
  }

  if (!class_has_been_loaded) {
  
    // add placeholder entry to record loading instance class
    // Five cases:
    // All cases need to prevent modifying bootclasssearchpath
    // in parallel with a classload of same classname
    // case 1. traditional classloaders that rely on the classloader object lock
    //   - no other need for LOAD_INSTANCE
    // case 2. traditional classloaders that break the classloader object lock 
    //    as a deadlock workaround. Detection of this case requires that
    //    this check is done while holding the classloader object lock,
    //    and that lock is still held when calling classloader's loadClass.
    //    For these classloaders, we ensure that the first requestor
    //    completes the load and other requestors wait for completion.
    // case 3. UnsyncloadClass - don't use objectLocker
    //    With this flag, we allow parallel classloading of a
    //    class/classloader pair
    // case4. Bootstrap classloader - don't own objectLocker
    //    This classloader supports parallelism at the classloader level,
    //    but only allows a single load of a class/classloader pair.
    //    No performance benefit and no deadlock issues.
    // case 5. Future: parallel user level classloaders - without objectLocker
    symbolHandle nullsymbolHandle;
    bool throw_circularity_error = false;
    {
MutexLocker mu(SystemDictionary_lock,jt);
      if (!UnsyncloadClass) {
        PlaceholderEntry* oldprobe = placeholders()->get_entry(p_index, p_hash, name, class_loader);
        if (oldprobe) {
          // only need check_seen_thread once, not on each loop
          // 6341374 java/lang/Instrument with -Xcomp
          if (oldprobe->check_seen_thread(THREAD, PlaceholderTable::LOAD_INSTANCE)) {
            throw_circularity_error = true;
          } else {
            // case 1: traditional: should never see load_in_progress.
            while (!class_has_been_loaded && oldprobe && oldprobe->instance_load_in_progress()) {
     
              // case 4: bootstrap classloader: prevent futile classloading,
              // wait on first requestor
              if (class_loader.is_null()) {
                // Azul - TODO - check whether this can be cleaned up
jt->jvm_unlock_self();
                SystemDictionary_lock.wait();
                MutexUnlocker_GC_on_Relock mugcrelk(SystemDictionary_lock,true);
jt->jvm_lock_self();
              } else {
              // case 2: traditional with broken classloader lock. wait on first
              // requestor.
double_lock_wait(ol._h,THREAD);
              }
              // Check if classloading completed while we were waiting
              klassOop check = find_class(d_index, d_hash, name, class_loader);
              if (check != NULL) {
                // Klass is already loaded, so just return it
                k = instanceKlassHandle(THREAD, check);
                class_has_been_loaded = true;
              }
              // check if other thread failed to load and cleaned up
              oldprobe = placeholders()->get_entry(p_index, p_hash, name, class_loader);
            } 
          } 
        }
      }
      // All cases: add LOAD_INSTANCE 
      // case 3: UnsyncloadClass: allow competing threads to try
      // LOAD_INSTANCE in parallel
      // add placeholder entry even if error - callers will remove on error
      if (!class_has_been_loaded) {
        PlaceholderEntry* newprobe = placeholders()->find_and_add(p_index, p_hash, name, class_loader, PlaceholderTable::LOAD_INSTANCE, nullsymbolHandle, THREAD); 
        if (throw_circularity_error) {
          newprobe->remove_seen_thread(THREAD, PlaceholderTable::LOAD_INSTANCE);
        }
        // For class loaders that do not acquire the classloader object lock,
        // if they did not catch another thread holding LOAD_INSTANCE,
        // need a check analogous to the acquire ObjectLocker/find_class 
        // i.e. now that we hold the LOAD_INSTANCE token on loading this class/CL
        // one final check if the load has already completed
        klassOop check = find_class(d_index, d_hash, name, class_loader);
        if (check != NULL) {
        // Klass is already loaded, so just return it
          k = instanceKlassHandle(THREAD, check);
          class_has_been_loaded = true;
          newprobe->remove_seen_thread(THREAD, PlaceholderTable::LOAD_INSTANCE);
        }
      }
    }
    // must throw error outside of owning lock
    if (throw_circularity_error) {
      ResourceMark rm(THREAD);
      THROW_MSG_0(vmSymbols::java_lang_ClassCircularityError(), name->as_C_string());
    }

    if (!class_has_been_loaded) {

      // Do actual loading
      k = load_instance_class(name, class_loader, THREAD);

      // In custom class loaders, the usual findClass calls
      // findLoadedClass, which directly searches  the SystemDictionary, then
      // defineClass. If these are not atomic with respect to other threads,
      // the findLoadedClass can fail, but the defineClass can get a 
      // LinkageError:: duplicate class definition.
      // If they got a linkageError, check if a parallel class load succeeded.
      // If it did, then for bytecode resolution the specification requires
      // that we return the same result we did for the other thread, i.e. the
      // successfully loaded instanceKlass
      // Note: Class can not be unloaded as long as any classloader refs exist
      // Should not get here for classloaders that support parallelism
      // with the new cleaner mechanism, e.g. bootstrap classloader
      if (UnsyncloadClass || (class_loader.is_null())) {
        if (k.is_null() && HAS_PENDING_EXCEPTION 
          && PENDING_EXCEPTION->is_a(SystemDictionary::linkageError_klass())) {
MutexLocker mu(SystemDictionary_lock,jt);
          klassOop check = find_class(d_index, d_hash, name, class_loader);
          if (check != NULL) {
            // Klass is already loaded, so just use it
            k = instanceKlassHandle(THREAD, check);
            CLEAR_PENDING_EXCEPTION;
            guarantee((!class_loader.is_null()), "dup definition for bootstrap loader?");
          }
        }
      }

      // clean up placeholder entries for success or error
      // This cleans up LOAD_INSTANCE entries
      // It also cleans up LOAD_SUPER entries on errors from 
      // calling load_instance_class
      { 
MutexLocker mu(SystemDictionary_lock,jt);
        PlaceholderEntry* probe = placeholders()->get_entry(p_index, p_hash, name, class_loader);
        if (probe != NULL) {
          probe->remove_seen_thread(THREAD, PlaceholderTable::LOAD_INSTANCE);
          placeholders()->find_and_remove(p_index, p_hash, name, class_loader, THREAD);
SystemDictionary_lock.notify_all();
        }
      }

      // If everything was OK (no exceptions, no null return value), and
      // class_loader is NOT the defining loader, do a little more bookkeeping.
      if (!HAS_PENDING_EXCEPTION && !k.is_null() && 
        k->class_loader() != class_loader()) {

        check_constraints(d_index, d_hash, k, class_loader, false, THREAD);

        // Need to check for a PENDING_EXCEPTION again; check_constraints
        // can throw and doesn't use the CHECK macro.
        if (!HAS_PENDING_EXCEPTION) {
          { // Grabbing the Compile_lock prevents systemDictionary updates
            // during compilations. 
MutexLockerAllowGC mu(Compile_lock,jt);

            // Add to class hierarchy, initialize vtables, and do possible
            // deoptimizations.
if(!k->is_loaded())
add_to_hierarchy(k);

            update_dictionary(d_index, d_hash, p_index, p_hash,
                            k, class_loader, THREAD);
          }
          if (JvmtiExport::should_post_class_load()) {
            Thread *thread = THREAD;
            assert(thread->is_Java_thread(), "thread->is_Java_thread()");
            JvmtiExport::post_class_load((JavaThread *) thread, k());
          }
        }
      }
      if (HAS_PENDING_EXCEPTION || k.is_null()) {
        // On error, clean up placeholders
        {
MutexLocker mu(SystemDictionary_lock,jt);
          placeholders()->find_and_remove(p_index, p_hash, name, class_loader, THREAD);
SystemDictionary_lock.notify_all();
        }
        return NULL;
      }
    }
  }

#ifdef ASSERT
  {
    Handle loader (THREAD, k->class_loader());
MutexLocker mu(SystemDictionary_lock,jt);
    oop kk = find_class_or_placeholder(name, loader);
    assert(kk == k(), "should be present in dictionary");
  }
#endif

  // return if the protection domain in NULL
  if (protection_domain() == NULL) return k();

  // Check the protection domain has the right access 
  {
MutexLocker mu(SystemDictionary_lock,jt);
    // Note that we have an entry, and entries can be deleted only during GC,
    // so we cannot allow GC to occur while we're holding this entry.
    // We're using a No_Safepoint_Verifier to catch any place where we
    // might potentially do a GC at all.
    // SystemDictionary::do_unloading() asserts that classes are only
    // unloaded at a safepoint.
    No_Safepoint_Verifier nosafepoint;
    if (dictionary()->is_valid_protection_domain(d_index, d_hash, name,
                                                 class_loader,
                                                 protection_domain)) {
      return k();
    }
  }

  // Verify protection domain. If it fails an exception is thrown
  validate_protection_domain(k, class_loader, protection_domain, CHECK_(klassOop(NULL)));

  return k();
}


// This routine does not lock the system dictionary.
//
// Since readers don't hold a lock, we must make sure that system
// dictionary entries are only removed at a safepoint (when only one
// thread is running), and are added to in a safe way (all links must
// be updated in an MT-safe manner).
//
// Callers should be aware that an entry could be added just after
// _dictionary->bucket(index) is read here, so the caller will not see
// the new entry.

klassOop SystemDictionary::find(symbolHandle class_name,
                                Handle class_loader, 
                                Handle protection_domain,
                                TRAPS) {

  unsigned int d_hash = dictionary()->compute_hash(class_name, class_loader);
  int d_index = dictionary()->hash_to_index(d_hash);

  {
    // Note that we have an entry, and entries can be deleted only during GC,
    // so we cannot allow GC to occur while we're holding this entry.
    // We're using a No_Safepoint_Verifier to catch any place where we
    // might potentially do a GC at all.
    // SystemDictionary::do_unloading() asserts that classes are only
    // unloaded at a safepoint.
    No_Safepoint_Verifier nosafepoint;
    return dictionary()->find(d_index, d_hash, class_name, class_loader,
                              protection_domain, THREAD);
  }
}


// Look for a loaded instance or array klass by name.  Do not do any loading.
// return NULL in case of error.
klassOop SystemDictionary::find_instance_or_array_klass(symbolHandle class_name,
                                                        Handle class_loader,
							Handle protection_domain,
                                                        TRAPS) {
  klassOop k = NULL;
  assert(class_name() != NULL, "class name must be non NULL");
  if (FieldType::is_array(class_name())) {
    // The name refers to an array.  Parse the name.
    jint dimension;
    symbolOop object_key;

    // dimension and object_key are assigned as a side-effect of this call
    BasicType t = FieldType::get_array_info(class_name(), &dimension,
					    &object_key, CHECK_(NULL));
    if (t != T_OBJECT) {
      k = Universe::typeArrayKlassObj(t);
    } else {
      symbolHandle h_key(THREAD, object_key);
      k = SystemDictionary::find(h_key, class_loader, protection_domain, THREAD);
    }
    if (k != NULL) {
      k = Klass::cast(k)->array_klass_or_null(dimension);
    }
  } else {
    k = find(class_name, class_loader, protection_domain, THREAD);
  }
  return k;
}

// Note: this method is much like resolve_from_stream, but
// updates no supplemental data structures.
// TODO consolidate the two methods with a helper routine?
klassOop SystemDictionary::parse_stream(symbolHandle class_name,
                                        Handle class_loader,
                                        Handle protection_domain,
                                        ClassFileStream* st,
                                        TRAPS) {
  symbolHandle parsed_name;

  // Parse the stream. Note that we do this even though this klass might
  // already be present in the SystemDictionary, otherwise we would not
  // throw potential ClassFormatErrors.
  //
  // Note: "name" is updated.
  // Further note:  a placeholder will be added for this class when
  //   super classes are loaded (resolve_super_or_fail). We expect this
  //   to be called for all classes but java.lang.Object; and we preload
  //   java.lang.Object through resolve_or_fail, not this path.

  instanceKlassHandle k = ClassFileParser(st).parseClassFile(class_name,
                                                             class_loader,
                                                             protection_domain,
                                                             parsed_name,
                                                             THREAD);


  // We don't redefine the class, so we just need to clean up whether there
  // was an error or not (don't want to modify any system dictionary
  // data structures).
  // Parsed name could be null if we threw an error before we got far
  // enough along to parse it -- in that case, there is nothing to clean up.
  if (!parsed_name.is_null()) {
    unsigned int p_hash = placeholders()->compute_hash(parsed_name, 
                                                       class_loader);
    int p_index = placeholders()->hash_to_index(p_hash);
    {
    MutexLocker mu(SystemDictionary_lock, THREAD);
    placeholders()->find_and_remove(p_index, p_hash, parsed_name, class_loader, THREAD);
SystemDictionary_lock.notify_all();
    }
  }

  return k();
}

// Add a klass to the system from a stream (called by jni_DefineClass and
// JVM_DefineClass).
// Note: class_name can be NULL. In that case we do not know the name of 
// the class until we have parsed the stream.

klassOop SystemDictionary::resolve_from_stream(symbolHandle class_name, 
                                               Handle class_loader, 
                                               Handle protection_domain, 
                                               ClassFileStream* st, 
                                               TRAPS) {

  // Make sure we are synchronized on the class loader before we initiate 
  // loading.
  Handle lockObject = compute_loader_lock_object(class_loader, THREAD); 
check_loader_lock_contention(lockObject(),THREAD);
ObjectLocker ol(lockObject());

  symbolHandle parsed_name;

  // Parse the stream. Note that we do this even though this klass might 
  // already be present in the SystemDictionary, otherwise we would not 
  // throw potential ClassFormatErrors.
  //
  // Note: "name" is updated.
  // Further note:  a placeholder will be added for this class when
  //   super classes are loaded (resolve_super_or_fail). We expect this
  //   to be called for all classes but java.lang.Object; and we preload
  //   java.lang.Object through resolve_or_fail, not this path.

  instanceKlassHandle k = ClassFileParser(st).parseClassFile(class_name, 
                                                             class_loader, 
                                                             protection_domain,
                                                             parsed_name,
                                                             THREAD);

  const char* pkg = "java/";
  if (!HAS_PENDING_EXCEPTION && 
      !class_loader.is_null() && 
      !parsed_name.is_null() && 
      !strncmp((const char*)parsed_name->bytes(), pkg, strlen(pkg))) {
    // It is illegal to define classes in the "java." package from
    // JVM_DefineClass or jni_DefineClass unless you're the bootclassloader
    ResourceMark rm(THREAD);
    char* name = parsed_name->as_C_string();
    char* index = strrchr(name, '/');
    *index = '\0'; // chop to just the package name
    while ((index = strchr(name, '/')) != NULL) {
      *index = '.'; // replace '/' with '.' in package name
    }
    const char* fmt = "Prohibited package name: %s";
    size_t len = strlen(fmt) + strlen(name);
    char* message = NEW_RESOURCE_ARRAY(char, len);
    jio_snprintf(message, len, fmt, name);
    Exceptions::_throw_msg(THREAD_AND_LOCATION, 
      vmSymbols::java_lang_SecurityException(), message);
  }

  if (!HAS_PENDING_EXCEPTION) {
    assert(!parsed_name.is_null(), "Sanity");
    assert(class_name.is_null() || class_name() == parsed_name(), 
           "name mismatch");
    // Verification prevents us from creating names with dots in them, this
    // asserts that that's the case.
    assert(is_internal_format(parsed_name),
           "external class name format used internally");

    // Add class just loaded
    define_instance_class(k, THREAD);
  }

  // If parsing the class file or define_instance_class failed, we
  // need to remove the placeholder added on our behalf. But we
  // must make sure parsed_name is valid first (it won't be if we had
  // a format error before the class was parsed far enough to
  // find the name).
  if (HAS_PENDING_EXCEPTION && !parsed_name.is_null()) {
    unsigned int p_hash = placeholders()->compute_hash(parsed_name, 
                                                       class_loader);
    int p_index = placeholders()->hash_to_index(p_hash);
    {
    MutexLocker mu(SystemDictionary_lock, THREAD);
    placeholders()->find_and_remove(p_index, p_hash, parsed_name, class_loader, THREAD);
SystemDictionary_lock.notify_all();
    }
    return NULL;
  }

  // Make sure that we didn't leave a place holder in the
  // SystemDictionary; this is only done on success
  debug_only( {
    if (!HAS_PENDING_EXCEPTION) {
      assert(!parsed_name.is_null(), "parsed_name is still null?");
      symbolHandle h_name   (THREAD, k->name());
      Handle h_loader (THREAD, k->class_loader());

      MutexLocker mu(SystemDictionary_lock, THREAD);

      oop check = find_class_or_placeholder(parsed_name, class_loader);
      assert(check == k(), "should be present in the dictionary");

      oop check2 = find_class_or_placeholder(h_name, h_loader);
      assert(check == check2, "name inconsistancy in SystemDictionary");
    }
  } );

  return k();
}


void SystemDictionary::set_shared_dictionary(HashtableBucket* t, int length,
                                             int number_of_entries) {
  assert(length == _nof_buckets * sizeof(HashtableBucket),
         "bad shared dictionary size.");
  _shared_dictionary = new Dictionary(_nof_buckets, t, number_of_entries);
}


// If there is a shared dictionary, then find the entry for the
// given shared system class, if any.

klassOop SystemDictionary::find_shared_class(symbolHandle class_name) {
  if (shared_dictionary() != NULL) {
    unsigned int d_hash = dictionary()->compute_hash(class_name, Handle());
    int d_index = dictionary()->hash_to_index(d_hash);
    return shared_dictionary()->find_shared_class(d_index, d_hash, class_name);
  } else {
    return NULL;
  }
}



instanceKlassHandle SystemDictionary::load_instance_class(symbolHandle class_name, Handle class_loader, TRAPS) {
  instanceKlassHandle nh = instanceKlassHandle(); // null Handle
  if (class_loader.is_null()) {
    // Search the shared system dictionary for classes preloaded into the
    // shared spaces.
    instanceKlassHandle k;
    // Use VM class loader
    k = ClassLoader::load_classfile(class_name, CHECK_(nh));

    // find_or_define_instance_class may return a different k
    if (!k.is_null()) {
      k = find_or_define_instance_class(class_name, class_loader, k, CHECK_(nh));
    }
    return k;
  } else {
    // Use user specified class loader to load class. Call loadClass operation on class_loader.
    ResourceMark rm(THREAD);
      
    Handle s = java_lang_String::create_from_symbol(class_name, false/*SBA*/, CHECK_(nh));
    // Translate to external class name format, i.e., convert '/' chars to '.'
    Handle string = java_lang_String::externalize_classname(s, CHECK_(nh));

    JavaValue result(T_OBJECT);

    KlassHandle spec_klass (THREAD, SystemDictionary::classloader_klass());

    // UnsyncloadClass option means don't synchronize loadClass() calls.
    // loadClassInternal() is synchronized and public loadClass(String) is not.
    // This flag is for diagnostic purposes only. It is risky to call
    // custom class loaders without synchronization.
    // WARNING If a custom class loader does NOT synchronizer findClass, or callers of
    // findClass, this flag risks unexpected timing bugs in the field.
    // Do NOT assume this will be supported in future releases.
    if (!UnsyncloadClass && has_loadClassInternal()) {
      JavaCalls::call_special(&result, 
                              class_loader, 
                              spec_klass,
                              vmSymbolHandles::loadClassInternal_name(),
                              vmSymbolHandles::string_class_signature(), 
                              string,
                              CHECK_(nh));
    } else {
      JavaCalls::call_virtual(&result, 
                              class_loader, 
                              spec_klass,
                              vmSymbolHandles::loadClass_name(),
                              vmSymbolHandles::string_class_signature(), 
                              string,
                              CHECK_(nh));
    }

    assert(result.get_type() == T_OBJECT, "just checking");
oop obj=(*(objectRef*)result.get_value_addr()).as_oop();

    // Primitive classes return null since forName() can not be
    // used to obtain any of the Class objects representing primitives or void
    if ((obj != NULL) && !(java_lang_Class::is_primitive(obj))) {      
      instanceKlassHandle k = 
                instanceKlassHandle(THREAD, java_lang_Class::as_klassOop(obj));
      // For user defined Java class loaders, check that the name returned is
      // the same as that requested.  This check is done for the bootstrap
      // loader when parsing the class file.
      if (class_name() == k->name()) {
        return k;
      }
    }
    // Class is not found or has the wrong name, return NULL
    return nh;
  }
}

void SystemDictionary::define_instance_class(instanceKlassHandle k, TRAPS) {

  Handle class_loader_h(THREAD, k->class_loader());

  // for bootstrap classloader don't acquire lock
  if (!class_loader_h.is_null()) {
    assert(ObjectSynchronizer::current_thread_holds_lock((JavaThread*)THREAD, 
compute_loader_lock_object(class_loader_h,THREAD).as_oop()),
         "define called without lock");
  }


  // Check class-loading constraints. Throw exception if violation is detected.
  // Grabs and releases SystemDictionary_lock
  // The check_constraints/find_class call and update_dictionary sequence
  // must be "atomic" for a specific class/classloader pair so we never
  // define two different instanceKlasses for that class/classloader pair.
  // Existing classloaders will call define_instance_class with the
  // classloader lock held
  // Parallel classloaders will call find_or_define_instance_class
  // which will require a token to perform the define class
  symbolHandle name_h(THREAD, k->name());
  unsigned int d_hash = dictionary()->compute_hash(name_h, class_loader_h);
  int d_index = dictionary()->hash_to_index(d_hash);
  check_constraints(d_index, d_hash, k, class_loader_h, true, CHECK);

  // Register class just loaded with class loader (placed in Vector)
  // Note we do this before updating the dictionary, as this can
  // fail with an OutOfMemoryError (if it does, we will *not* put this
  // class in the dictionary and will not update the class hierarchy).
  if (k->class_loader() != NULL) {
    methodHandle m(THREAD, Universe::loader_addClass_method());
    JavaValue result(T_VOID);
    JavaCallArguments args(class_loader_h);
    args.push_oop(Handle(THREAD, k->java_mirror()));
    JavaCalls::call(&result, m, &args, CHECK);
  }

  // Add the new class. We need recompile lock during update of CHA.
  {
    unsigned int p_hash = placeholders()->compute_hash(name_h, class_loader_h);
    int p_index = placeholders()->hash_to_index(p_hash);

    assert0( THREAD->is_Java_thread() );
    MutexLockerAllowGC mu_r(Compile_lock, (JavaThread*)THREAD);

    // Link into hierachy. Make sure the vtables are initialized before linking into 
    k->append_to_sibling_list(); // add to superklass/sibling list

    // Add to class hierarchy, initialize vtables, and do possible
    // deoptimizations.
add_to_hierarchy(k);//No exception, but can block

    k->set_init_state(instanceKlass::loaded);

    // Add to systemDictionary - so other classes can see it.
    // Grabs and releases SystemDictionary_lock
    update_dictionary(d_index, d_hash, p_index, p_hash,
                      k, class_loader_h, THREAD);
  }
  k->eager_initialize(THREAD);

  // notify jvmti
  if (JvmtiExport::should_post_class_load()) {
      assert(THREAD->is_Java_thread(), "thread->is_Java_thread()");
      JvmtiExport::post_class_load((JavaThread *) THREAD, k());

  }
}

// Support parallel classloading
// Initial implementation for bootstrap classloader
// For future:
// For custom class loaders that support parallel classloading,
// in case they do not synchronize around
// FindLoadedClass/DefineClass calls, we check for parallel
// loading for them, wait if a defineClass is in progress
// and return the initial requestor's results
// For better performance, the class loaders should synchronize
// findClass(), i.e. FindLoadedClass/DefineClass or they
// potentially waste time reading and parsing the bytestream.
// Note: VM callers should ensure consistency of k/class_name,class_loader
instanceKlassHandle SystemDictionary::find_or_define_instance_class(symbolHandle class_name, Handle class_loader, instanceKlassHandle k, TRAPS) {

  instanceKlassHandle nh = instanceKlassHandle(); // null Handle

  unsigned int d_hash = dictionary()->compute_hash(class_name, class_loader);
  int d_index = dictionary()->hash_to_index(d_hash);

// Hold SD lock around find_class and placeholder creation for DEFINE_CLASS
  unsigned int p_hash = placeholders()->compute_hash(class_name, class_loader);
  int p_index = placeholders()->hash_to_index(p_hash);
  PlaceholderEntry* probe;

  { 
    MutexLocker mu(SystemDictionary_lock, THREAD);
    // First check if class already defined
    klassOop check = find_class(d_index, d_hash, class_name, class_loader);
    if (check != NULL) {
      return(instanceKlassHandle(THREAD, check));
    }

    // Acquire define token for this class/classloader
    symbolHandle nullsymbolHandle;
    probe = placeholders()->find_and_add(p_index, p_hash, class_name, class_loader, PlaceholderTable::DEFINE_CLASS, nullsymbolHandle, THREAD); 
    // Check if another thread defining in parallel
    if (probe->definer() == NULL) {
      // Thread will define the class
      probe->set_definer(THREAD);
    } else {
      // Wait for defining thread to finish and return results
      while (probe->definer() != NULL) {
        // Azul - TODO - check whether this can be cleaned up
((JavaThread*)THREAD)->jvm_unlock_self();
        SystemDictionary_lock.wait();
        MutexUnlocker_GC_on_Relock mugcrelk(SystemDictionary_lock,true);
((JavaThread*)THREAD)->jvm_lock_self();
      }
      if (probe->instanceKlass() != NULL) {
        probe->remove_seen_thread(THREAD, PlaceholderTable::DEFINE_CLASS);
        return(instanceKlassHandle(THREAD, probe->instanceKlass()));
      } else {
        // If definer had an error, try again as any new thread would
        probe->set_definer(THREAD);
#ifdef ASSERT
        klassOop check = find_class(d_index, d_hash, class_name, class_loader);
        assert(check == NULL, "definer missed recording success");
#endif
      }
    }
  }

  define_instance_class(k, THREAD);

  Handle linkage_exception = Handle(); // null handle

  // definer must notify any waiting threads
  {
    MutexLocker mu(SystemDictionary_lock, THREAD);
    PlaceholderEntry* probe = placeholders()->get_entry(p_index, p_hash, class_name, class_loader);
    assert(probe != NULL, "DEFINE_CLASS placeholder lost?");
    if (probe != NULL) {
      if (HAS_PENDING_EXCEPTION) {
        linkage_exception = Handle(THREAD,PENDING_EXCEPTION);
        CLEAR_PENDING_EXCEPTION;
      } else {
        probe->set_instanceKlass(POISON_KLASSREF(k.as_klassRef()));
      }
      probe->set_definer(NULL);
      probe->remove_seen_thread(THREAD, PlaceholderTable::DEFINE_CLASS);
SystemDictionary_lock.notify_all();
    }
  }

  // Can't throw exception while holding lock due to rank ordering
  if (linkage_exception() != NULL) {
    THROW_OOP_(linkage_exception(), nh); // throws exception and returns
  }

  return k;
}

objectRef SystemDictionary::compute_loader_lock_object(Handle class_loader,TRAPS){
  // If class_loader is NULL we synchronize on _system_loader_lock_obj
return class_loader.is_null()?lvb_ref(&_system_loader_lock_obj):class_loader();
}

// This method is added to check how often we have to wait to grab loader
// lock. The results are being recorded in the performance counters defined in
// ClassLoader::_sync_systemLoaderLockContentionRate and
// ClassLoader::_sync_nonSystemLoaderLockConteionRate. 
void SystemDictionary::check_loader_lock_contention(objectRef loader_lock,Thread*self){
if(!UsePerfData)return;
assert(loader_lock.not_null(),"NULL lock object");
  Thread *owner = loader_lock.as_oop()->lock_owner();
  if( !owner || owner == self ) return; // unlocked or self-locked
  // contention will likely happen, so increment the corresponding contention counter.
  ((loader_lock == _system_loader_lock_obj)
    ? ClassLoader::   sync_systemLoaderLockContentionRate()
    : ClassLoader::sync_nonSystemLoaderLockContentionRate()) ->inc();
} 
  
// ----------------------------------------------------------------------------
// Lookup

klassOop SystemDictionary::find_class(int index, unsigned int hash,
                                      symbolHandle class_name,
                                      Handle class_loader) {
  assert_locked_or_safepoint(SystemDictionary_lock);

  klassOop k = dictionary()->find_class(index, hash, class_name, class_loader);
  return k;
}


// Basic find on classes in the midst of being loaded
symbolOop SystemDictionary::find_placeholder(int index, unsigned int hash,
                                             symbolHandle class_name,
                                             Handle class_loader) {
  assert_locked_or_safepoint(SystemDictionary_lock);

  return placeholders()->find_entry(index, hash, class_name, class_loader);
}


// Used for assertions and verification only
oop SystemDictionary::find_class_or_placeholder(symbolHandle class_name, 
                                                Handle class_loader) {
  #ifndef ASSERT
  guarantee(VerifyBeforeGC   || 
            VerifyDuringGC   || 
            VerifyBeforeExit ||
            VerifyAfterGC, "too expensive"); 
  #endif
  assert_locked_or_safepoint(SystemDictionary_lock);
  symbolOop class_name_ = class_name();
  oop class_loader_ = class_loader();

  // First look in the loaded class array
  unsigned int d_hash = dictionary()->compute_hash(class_name, class_loader);
  int d_index = dictionary()->hash_to_index(d_hash);
  oop lookup = find_class(d_index, d_hash, class_name, class_loader);

  if (lookup == NULL) {
    // Next try the placeholders
    unsigned int p_hash = placeholders()->compute_hash(class_name,class_loader);
    int p_index = placeholders()->hash_to_index(p_hash);
    lookup = find_placeholder(p_index, p_hash, class_name, class_loader);
  }

  return lookup;
}


// Get the next class in the diictionary.
klassOop SystemDictionary::try_get_next_class() {
  return dictionary()->try_get_next_class();
}


// ----------------------------------------------------------------------------
// Update hierachy.  This is done before the new klass has been added to the
// SystemDictionary. The Compile_lock is held, to ensure that the compiler is
// not using the class hierachy, and that deoptimization will kick in before a
// new class is used.
void SystemDictionary::add_to_hierarchy(instanceKlassHandle k){
  assert(k.not_null(), "just checking");
  // Now flush all code that depended on old class hierarchy.
  // Note: must be done *after* linking k into the hierarchy (was bug 12/9/97)
  // Also, first reinitialize vtable because it may have gotten out of synch 
  // while the new class wasn't connected to the class hierarchy.
  { No_Safepoint_Verifier nsv;
  k->process_interface_and_abstract(); // handle all "implements" declarations  
instanceKlass::cast(k())->deoptimize_code_dependent_on();
  }

  {
    // this is a GC point, don't hold the compile_lock here
    // set dont_install_code flag here and give up compile_lock
    ciEnv::set_dont_install_code(true);
    MutexUnlocker_GC_on_Relock mc(Compile_lock);
    if( methodCodeOopDesc::_deopt_list_len > 0 ) {
      VM_Deoptimize vm_op;
VMThread::execute(&vm_op);
    }
  }
  // clear dont_install_code flag
assert_lock_strong(Compile_lock);
  ciEnv::set_dont_install_code(false);
#ifdef ASSERT
if(_object_klass.not_null()){
    instanceKlass::verify_dependencies(Klass::cast(object_klass()), 0, NULL);
  }
#endif // !ASSERT
}


// ----------------------------------------------------------------------------
// GC support

// Following roots during mark-sweep is separated in two phases. 
//
// The first phase follows preloaded classes and all other system 
// classes, since these will never get unloaded anyway.
//
// The second phase removes (unloads) unreachable classes from the
// system dictionary and follows the remaining classes' contents.

void SystemDictionary::always_strong_oops_do(OopClosure* blk) {
  // Follow preloaded classes/mirrors and system loader object
  blk->do_oop(&_java_system_loader);
  preloaded_oops_do(blk);
  always_strong_classes_do(blk);
}

void SystemDictionary::GC_always_strong_oops_do(OopClosure*blk){
  // Follow preloaded classes/mirrors and system loader object
  blk->do_oop(&_java_system_loader);
  preloaded_oops_do(blk);
GC_always_strong_classes_do(blk);
}

void SystemDictionary::GPGC_always_strong_oops_do_concurrent(OopClosure*blk){
  // Follow preloaded classes/mirrors and system loader object
  blk->do_oop(&_java_system_loader);
  preloaded_oops_do(blk);
lazily_loaded_oops_do(blk);
dictionary()->GC_always_strong_classes_do(blk);
MutexLocker ml(SystemDictionary_lock);
  placeholders_do(blk);  
  constraints()->always_strong_classes_do(blk);
  // Resolution errors keep the symbolOop for the error alive
  resolution_errors()->always_strong_classes_do(blk);
}


void SystemDictionary::always_strong_classes_do(OopClosure* blk) {
  // Follow all system classes and temporary placeholders in dictionary
  dictionary()->always_strong_classes_do(blk);
  
  // Placeholders. These are *always* strong roots, as they
  // represent classes we're actively loading.
  placeholders_do(blk);  

  // Loader constraints. We must keep the symbolOop used in the name alive.
  constraints()->always_strong_classes_do(blk);

  // Resolution errors keep the symbolOop for the error alive
  resolution_errors()->always_strong_classes_do(blk);
}
void SystemDictionary::GC_always_strong_classes_do(OopClosure*blk){
  // Follow all system classes and temporary placeholders in dictionary
dictionary()->GC_always_strong_classes_do(blk);
  
  // Placeholders. These are *always* strong roots, as they
  // represent classes we're actively loading.
  placeholders_do(blk);  

  // Loader constraints. We must keep the symbolOop used in the name alive.
  constraints()->always_strong_classes_do(blk);

  // Resolution errors keep the symbolOop for the error alive
  resolution_errors()->always_strong_classes_do(blk);
}


void SystemDictionary::placeholders_do(OopClosure* blk) {
  placeholders()->oops_do(blk);
}


bool SystemDictionary::do_unloading(BoolObjectClosure* is_alive) {
  bool result = dictionary()->do_unloading(is_alive);
  constraints()->purge_loader_constraints(is_alive);
  resolution_errors()->purge_resolution_errors(is_alive);
#ifdef ASSERT
if(_object_klass.not_null()){
    instanceKlass::verify_dependencies(Klass::cast(object_klass()), 1, is_alive);
  }
#endif
  if( result ) SystemDictionary::notice_modification();
  return result;
}


// This is called from the gc task threads to parallelize class unloading 
// during the second safepoint
void SystemDictionary::GPGC_unload_section(GPGC_GCManagerOldStrong* gcm, long section, long sections) {
  bool classes_were_unloaded = dictionary()->GPGC_unload_section(gcm, section, sections);
  if (classes_were_unloaded) {
SystemDictionary::notice_modification();
  }
}


void SystemDictionary::GPGC_unload_section_cleanup(long sections) {
for(int i=0;i<sections;i++){
    GPGC_GCManagerOldStrong* gcm = GPGC_GCManagerOldStrong::get_manager(i);
    while (gcm->dec_implementors_stack()->length() > 0) {
      instanceKlass* ik = gcm->dec_implementors_stack()->pop();
ik->GPGC_dec_implementor();
ik->release_C_heap_structures();
    }
  }
}


void SystemDictionary::GPGC_purge_loader_constraints_section(long section, long sections)
{
  constraints()->GPGC_purge_loader_constraints_section(section, sections);
  resolution_errors()->GPGC_purge_resolution_errors_section(section, sections);
}

void SystemDictionary::verify_dependencies(int gc_mode)
{
  /*
   * TODO : GPGC

  if ( UseC1 || UseC2 ) {
    Unimplemented();
    //  if( _object_klass.not_null() ) {
    //    instanceKlass::verify_dependencies(Klass::cast(object_klass()), gc_mode, NULL);
    //  }
  }
  */
}

// The mirrors are scanned by shared_oops_do() which is
// not called by oops_do().  In order to process oops in
// a necessary order, shared_oops_do() is call by
// Universe::oops_do().

void SystemDictionary::oops_do(OopClosure* f) {
  // Adjust preloaded classes and system loader object
  f->do_oop(&_java_system_loader);
  preloaded_oops_do(f);

  lazily_loaded_oops_do(f);

  // Adjust dictionary
  dictionary()->oops_do(f);

MutexLocker ml(SystemDictionary_lock);
  // Partially loaded classes
  placeholders()->oops_do(f);

  // Adjust constraint table
  constraints()->oops_do(f);

  // Adjust resolution error table
  resolution_errors()->oops_do(f);
}


void SystemDictionary::preloaded_oops_do(OopClosure* f) {
  f->do_oop((objectRef*) &_string_klass);
  f->do_oop((objectRef*) &_object_klass);
  f->do_oop((objectRef*) &_class_klass);
  f->do_oop((objectRef*) &_cloneable_klass);
  f->do_oop((objectRef*) &_classloader_klass);
  f->do_oop((objectRef*) &_serializable_klass);
  f->do_oop((objectRef*) &_system_klass);

  f->do_oop((objectRef*) &_throwable_klass);
  f->do_oop((objectRef*) &_error_klass);
  f->do_oop((objectRef*) &_threaddeath_klass);
  f->do_oop((objectRef*) &_exception_klass);
  f->do_oop((objectRef*) &_runtime_exception_klass);
  f->do_oop((objectRef*) &_classNotFoundException_klass);
  f->do_oop((objectRef*) &_noClassDefFoundError_klass);
  f->do_oop((objectRef*) &_linkageError_klass);
f->do_oop((objectRef*)&_nullPointerException_klass);
f->do_oop((objectRef*)&_arrayIndexOutOfBoundsException_klass);
  f->do_oop((objectRef*) &_classCastException_klass);
  f->do_oop((objectRef*) &_arrayStoreException_klass);
  f->do_oop((objectRef*) &_virtualMachineError_klass);
  f->do_oop((objectRef*) &_outOfMemoryError_klass);
  f->do_oop((objectRef*) &_StackOverflowError_klass);
  f->do_oop((objectRef*) &_illegalMonitorStateException_klass);
  f->do_oop((objectRef*) &_protectionDomain_klass);
  f->do_oop((objectRef*) &_AccessControlContext_klass);

  f->do_oop((objectRef*) &_reference_klass);
  f->do_oop((objectRef*) &_soft_reference_klass);
  f->do_oop((objectRef*) &_weak_reference_klass);
  f->do_oop((objectRef*) &_final_reference_klass);
  f->do_oop((objectRef*) &_phantom_reference_klass);
  f->do_oop((objectRef*) &_finalizer_klass);
  
  f->do_oop((objectRef*) &_thread_klass);
  f->do_oop((objectRef*) &_threadGroup_klass);
  f->do_oop((objectRef*) &_properties_klass);      
  f->do_oop((objectRef*) &_reflect_accessible_object_klass);      
  f->do_oop((objectRef*) &_reflect_field_klass);      
  f->do_oop((objectRef*) &_reflect_method_klass);      
  f->do_oop((objectRef*) &_reflect_constructor_klass);      
  f->do_oop((objectRef*) &_reflect_magic_klass);
  f->do_oop((objectRef*) &_reflect_method_accessor_klass);
  f->do_oop((objectRef*) &_reflect_constructor_accessor_klass);
  f->do_oop((objectRef*) &_reflect_delegating_classloader_klass);
  f->do_oop((objectRef*) &_reflect_constant_pool_klass);
  f->do_oop((objectRef*) &_reflect_unsafe_static_field_accessor_impl_klass);

  f->do_oop((objectRef*) &_stringBuffer_klass);
  f->do_oop((objectRef*) &_vector_klass);
  f->do_oop((objectRef*) &_hashtable_klass);

  f->do_oop((objectRef*) &_stackTraceElement_klass);

  f->do_oop((objectRef*) &_java_nio_Buffer_klass);

  f->do_oop((objectRef*) &_sun_misc_AtomicLongCSImpl_klass);
f->do_oop((objectRef*)&_com_azulsystems_util_Prefetch_klass);
f->do_oop((objectRef*)&_com_azulsystems_misc_BlockingHint_klass);

  f->do_oop((objectRef*) &_boolean_klass);
  f->do_oop((objectRef*) &_char_klass);
  f->do_oop((objectRef*) &_float_klass);
  f->do_oop((objectRef*) &_double_klass);
  f->do_oop((objectRef*) &_byte_klass);
  f->do_oop((objectRef*) &_short_klass);
  f->do_oop((objectRef*) &_int_klass);
  f->do_oop((objectRef*) &_long_klass);
  {
    for (int i = 0; i < T_VOID+1; i++) {
if(_box_klasses[i].not_null()){
	assert(i >= T_BOOLEAN, "checking");
	f->do_oop((objectRef*) &_box_klasses[i]);
      }
    }
  }

  // The basic type mirrors would have already been processed in
  // Universe::oops_do(), via a call to shared_oops_do(), so should
  // not be processed again.

  f->do_oop((objectRef*) &_system_loader_lock_obj); 
  FilteredFieldsMap::klasses_oops_do(f); 
}

// Just the classes from defining class loaders
// Don't iterate over placeholders
void SystemDictionary::lazily_loaded_oops_do(OopClosure* f) {
  f->do_oop((objectRef*) &_abstract_ownable_synchronizer_klass);
}

void SystemDictionary::classes_do(void f(klassOop)) {
  dictionary()->classes_do(f);
}

// clone of the above function for GC purposes: at this point it is used only by the allocation-profiler
// it uses the non-lvb accessor for the loader
void SystemDictionary::GC_classes_do(void f(klassOop)){
dictionary()->GC_classes_do(f);
}

// Added for initialize_itable_for_klass
//   Just the classes from defining class loaders
// Don't iterate over placeholders
void SystemDictionary::classes_do(void f(klassOop, TRAPS), TRAPS) {
  dictionary()->classes_do(f, CHECK);
}

//   All classes, and their class loaders
// Don't iterate over placeholders
void SystemDictionary::classes_do(void f(klassOop, oop)) {
  dictionary()->classes_do(f);
}

//   All classes, and their class loaders
//   (added for helpers that use HandleMarks and ResourceMarks)
// Don't iterate over placeholders
void SystemDictionary::classes_do(void f(klassOop, oop, TRAPS), TRAPS) {
  dictionary()->classes_do(f, CHECK);
}

void SystemDictionary::placeholders_do(void f(symbolOop, oop)) {
  placeholders()->entries_do(f);
}

void SystemDictionary::methods_do(void f(methodOop)) {
  dictionary()->methods_do(f);
}

// ----------------------------------------------------------------------------
// Lazily load klasses

void SystemDictionary::load_abstract_ownable_synchronizer_klass(TRAPS) {
  assert(JDK_Version::is_gte_jdk16x_version(), "Must be JDK 1.6 or later");

  // if multiple threads calling this function, only one thread will load
  // the class.  The other threads will find the loaded version once the
  // class is loaded.
klassRef aos=abstract_ownable_synchronizer_klassref();
if(aos.is_null()){
    klassOop k = resolve_or_fail(vmSymbolHandles::java_util_concurrent_locks_AbstractOwnableSynchronizer(), true, CHECK);
    // Force a fence to prevent any read before the write completes
    OrderAccess::fence();
POISON_AND_STORE_REF(&_abstract_ownable_synchronizer_klass,klassRef(k));
  }
}

// ----------------------------------------------------------------------------
// Initialization

void SystemDictionary::initialize(TRAPS) {
  // Allocate arrays
  assert(dictionary() == NULL,
         "SystemDictionary should only be initialized once");
  _dictionary = new Dictionary(_nof_buckets);
  _placeholders = new PlaceholderTable(_nof_buckets);
  _number_of_modifications = 0;
  _loader_constraints = new LoaderConstraintTable(_loader_constraint_size);
  _resolution_errors = new ResolutionErrorTable(_resolution_error_size);

  // Allocate private object used as system class loader lock
oop o=oopFactory::new_system_objArray(0,CHECK);
  POISON_AND_STORE_REF(&_system_loader_lock_obj, objectRef(o));
  // Initialize basic classes
  initialize_preloaded_classes(CHECK);
}


void SystemDictionary::initialize_preloaded_classes(TRAPS) {
assert(_object_klass.is_null(),"preloaded classes should only be initialized once");
  // Preload commonly used klasses
klassOop k=resolve_or_fail(vmSymbolHandles::java_lang_Object(),true,CHECK);
  POISON_AND_STORE_REF(&_object_klass, klassRef(k));
  assert0(k->klass_part()->klassId() == java_lang_Object_kid);
k=resolve_or_fail(vmSymbolHandles::java_lang_String(),true,CHECK);
  POISON_AND_STORE_REF(&_string_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_Class(),true,CHECK);
  POISON_AND_STORE_REF(&_class_klass, klassRef(k));
  debug_only(instanceKlass::verify_class_klass_nonstatic_oop_maps(class_klass()));

  // Fixup mirrors for classes loaded before java.lang.Class.
  // These calls iterate over the objects currently in the perm gen
  // so calling them at this point is matters (not before when there
  // are fewer objects and not later after there are more objects
  // in the perm gen.
  Universe::initialize_basic_type_mirrors(CHECK);
  Universe::fixup_mirrors(CHECK);

k=resolve_or_fail(vmSymbolHandles::java_lang_Cloneable(),true,CHECK);
  POISON_AND_STORE_REF(&_cloneable_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_ClassLoader(),true,CHECK);
  POISON_AND_STORE_REF(&_classloader_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_io_Serializable(),true,CHECK);
  POISON_AND_STORE_REF(&_serializable_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_System(),true,CHECK);
  POISON_AND_STORE_REF(&_system_klass, klassRef(k));

k=resolve_or_fail(vmSymbolHandles::java_lang_Throwable(),true,CHECK);
  POISON_AND_STORE_REF(&_throwable_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_Error(),true,CHECK);
  POISON_AND_STORE_REF(&_error_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_ThreadDeath(),true,CHECK);
  POISON_AND_STORE_REF(&_threaddeath_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_Exception(),true,CHECK);
  POISON_AND_STORE_REF(&_exception_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_RuntimeException(),true,CHECK);
  POISON_AND_STORE_REF(&_runtime_exception_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_security_ProtectionDomain(),true,CHECK);
  POISON_AND_STORE_REF(&_protectionDomain_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_security_AccessControlContext(),true,CHECK);
  POISON_AND_STORE_REF(&_AccessControlContext_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_ClassNotFoundException(),true,CHECK);
  POISON_AND_STORE_REF(&_classNotFoundException_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_NoClassDefFoundError(),true,CHECK);
  POISON_AND_STORE_REF(&_noClassDefFoundError_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_LinkageError(),true,CHECK);
  POISON_AND_STORE_REF(&_linkageError_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_NullPointerException(),true,CHECK);
  POISON_AND_STORE_REF(&_nullPointerException_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_ArrayIndexOutOfBoundsException(),true,CHECK);
  POISON_AND_STORE_REF(&_arrayIndexOutOfBoundsException_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_ClassCastException(),true,CHECK);
  POISON_AND_STORE_REF(&_classCastException_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_ArrayStoreException(),true,CHECK);
  POISON_AND_STORE_REF(&_arrayStoreException_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_VirtualMachineError(),true,CHECK);
  POISON_AND_STORE_REF(&_virtualMachineError_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_OutOfMemoryError(),true,CHECK);
  POISON_AND_STORE_REF(&_outOfMemoryError_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_StackOverflowError(),true,CHECK);
  POISON_AND_STORE_REF(&_StackOverflowError_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_IllegalMonitorStateException(),true,CHECK);
  POISON_AND_STORE_REF(&_illegalMonitorStateException_klass, klassRef(k));

  // Preload ref klasses and set reference types
k=resolve_or_fail(vmSymbolHandles::java_lang_ref_Reference(),true,CHECK);
  POISON_AND_STORE_REF(&_reference_klass, klassRef(k));
instanceKlass::cast(k)->set_reference_type(REF_OTHER);
instanceRefKlass::update_nonstatic_oop_maps(k);

k=resolve_or_fail(vmSymbolHandles::java_lang_ref_SoftReference(),true,CHECK);
instanceKlass::cast(k)->set_reference_type(REF_SOFT);
  POISON_AND_STORE_REF(&_soft_reference_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_ref_WeakReference(),true,CHECK);
instanceKlass::cast(k)->set_reference_type(REF_WEAK);
  POISON_AND_STORE_REF(&_weak_reference_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_ref_FinalReference(),true,CHECK);
instanceKlass::cast(k)->set_reference_type(REF_FINAL);
  POISON_AND_STORE_REF(&_final_reference_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_ref_PhantomReference(),true,CHECK);
instanceKlass::cast(k)->set_reference_type(REF_PHANTOM);
  POISON_AND_STORE_REF(&_phantom_reference_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_ref_Finalizer(),true,CHECK);
  POISON_AND_STORE_REF(&_finalizer_klass, klassRef(k));

k=resolve_or_fail(vmSymbolHandles::java_lang_Thread(),true,CHECK);
  POISON_AND_STORE_REF(&_thread_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_ThreadGroup(),true,CHECK);
  POISON_AND_STORE_REF(&_threadGroup_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_util_Properties(),true,CHECK);
  POISON_AND_STORE_REF(&_properties_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_reflect_AccessibleObject(),true,CHECK);
  POISON_AND_STORE_REF(&_reflect_accessible_object_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_reflect_Field(),true,CHECK);
  POISON_AND_STORE_REF(&_reflect_field_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_reflect_Method(),true,CHECK);
  POISON_AND_STORE_REF(&_reflect_method_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_reflect_Constructor(),true,CHECK);
  POISON_AND_STORE_REF(&_reflect_constructor_klass, klassRef(k));
  // JDK_Version::is_gte_jdk14x_version() is not set up by this point.
  // It's okay if these turn out to be NULL in non-1.4 JDKs.
k=resolve_or_null(vmSymbolHandles::sun_reflect_MagicAccessorImpl(),CHECK);
  POISON_AND_STORE_REF(&_reflect_magic_klass, klassRef(k));
k=resolve_or_null(vmSymbolHandles::sun_reflect_MethodAccessorImpl(),CHECK);
  POISON_AND_STORE_REF(&_reflect_method_accessor_klass, klassRef(k));
k=resolve_or_null(vmSymbolHandles::sun_reflect_ConstructorAccessorImpl(),CHECK);
  POISON_AND_STORE_REF(&_reflect_constructor_accessor_klass, klassRef(k));
k=resolve_or_null(vmSymbolHandles::sun_reflect_DelegatingClassLoader(),CHECK);
  POISON_AND_STORE_REF(&_reflect_delegating_classloader_klass, klassRef(k));
k=resolve_or_null(vmSymbolHandles::sun_reflect_ConstantPool(),CHECK);
  POISON_AND_STORE_REF(&_reflect_constant_pool_klass, klassRef(k));
k=resolve_or_null(vmSymbolHandles::sun_reflect_UnsafeStaticFieldAccessorImpl(),CHECK);
  POISON_AND_STORE_REF(&_reflect_unsafe_static_field_accessor_impl_klass, klassRef(k));

k=resolve_or_fail(vmSymbolHandles::java_util_Vector(),true,CHECK);
  POISON_AND_STORE_REF(&_vector_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_util_Hashtable(),true,CHECK);
  POISON_AND_STORE_REF(&_hashtable_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_StringBuffer(),true,CHECK);
  POISON_AND_STORE_REF(&_stringBuffer_klass, klassRef(k));

  // It's NULL in non-1.4 JDKs.
k=resolve_or_null(vmSymbolHandles::java_lang_StackTraceElement(),CHECK);
  POISON_AND_STORE_REF(&_stackTraceElement_klass, klassRef(k));

  // JDK_Version::is_gte_jdk14x_version() is not set up by this point.
  // It's okay if this turns out to be NULL in non-1.4 JDKs.
k=resolve_or_null(vmSymbolHandles::java_nio_Buffer(),CHECK);
  POISON_AND_STORE_REF(&_java_nio_Buffer_klass, klassRef(k));

  // If this class isn't present, it won't be referenced.
k=resolve_or_null(vmSymbolHandles::sun_misc_AtomicLongCSImpl(),CHECK);
  POISON_AND_STORE_REF(&_sun_misc_AtomicLongCSImpl_klass, klassRef(k));
k=resolve_or_null(vmSymbolHandles::com_azulsystems_util_Prefetch(),CHECK);
  POISON_AND_STORE_REF(&_com_azulsystems_util_Prefetch_klass, klassRef(k));

  // Preload boxing klasses
k=resolve_or_fail(vmSymbolHandles::java_lang_Boolean(),true,CHECK);
  POISON_AND_STORE_REF(&_boolean_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_Character(),true,CHECK);
  POISON_AND_STORE_REF(&_char_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_Float(),true,CHECK);
  POISON_AND_STORE_REF(&_float_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_Double(),true,CHECK);
  POISON_AND_STORE_REF(&_double_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_Byte(),true,CHECK);
  POISON_AND_STORE_REF(&_byte_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_Short(),true,CHECK);
  POISON_AND_STORE_REF(&_short_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_Integer(),true,CHECK);
  POISON_AND_STORE_REF(&_int_klass, klassRef(k));
k=resolve_or_fail(vmSymbolHandles::java_lang_Long(),true,CHECK);
  POISON_AND_STORE_REF(&_long_klass, klassRef(k));

  // Thread blocking hint.
k=resolve_or_fail(vmSymbolHandles::com_azulsystems_misc_BlockingHint(),true,CHECK);
  POISON_AND_STORE_REF(&_com_azulsystems_misc_BlockingHint_klass, klassRef(k));

  _box_klasses[T_BOOLEAN]  = _boolean_klass;
  _box_klasses[T_CHAR]     = _char_klass;
  _box_klasses[T_FLOAT]    = _float_klass;
  _box_klasses[T_DOUBLE]   = _double_klass;
  _box_klasses[T_BYTE]     = _byte_klass;
  _box_klasses[T_SHORT]    = _short_klass;
  _box_klasses[T_INT]      = _int_klass;
  _box_klasses[T_LONG]     = _long_klass;
  //_box_klasses[T_OBJECT]  = _object_klass;
  //_box_klasses[T_ARRAY]   = _object_klass;

  { // Compute whether we should use loadClass or loadClassInternal when loading classes.
    methodOop method = instanceKlass::cast(classloader_klass())->find_method(vmSymbols::loadClassInternal_name(), vmSymbols::string_class_signature());
    _has_loadClassInternal = (method != NULL);
  }

  { // Compute whether we should use checkPackageAccess or NOT
    methodOop method = instanceKlass::cast(classloader_klass())->find_method(vmSymbols::checkPackageAccess_name(), vmSymbols::class_protectiondomain_signature());
    _has_checkPackageAccess = (method != NULL); 
  }
}

// Tells if a given klass is a box (wrapper class, such as java.lang.Integer).
// If so, returns the basic type it holds.  If not, returns T_OBJECT.
BasicType SystemDictionary::box_klass_type(klassOop k) {
  assert(k != NULL, "");
  for (int i = T_BOOLEAN; i < T_VOID+1; i++) {
if(lvb_klassRef(&_box_klasses[i]).as_klassOop()==k)
      return (BasicType)i;
  }
  return T_OBJECT;
}

// Constraints on class loaders. The details of the algorithm can be
// found in the OOPSLA'98 paper "Dynamic Class Loading in the Java
// Virtual Machine" by Sheng Liang and Gilad Bracha.  The basic idea is
// that the system dictionary needs to maintain a set of contraints that
// must be satisfied by all classes in the dictionary.
// if defining is true, then LinkageError if already in systemDictionary
// if initiating loader, then ok if instanceKlass matches existing entry

void SystemDictionary::check_constraints(int d_index, unsigned int d_hash,
                                         instanceKlassHandle k,
                                         Handle class_loader, bool defining, 
                                         TRAPS) {
  const char *linkage_error = NULL;
  {
    symbolHandle name (THREAD, k->name());
    MutexLocker mu(SystemDictionary_lock, THREAD);         

    klassOop check = find_class(d_index, d_hash, name, class_loader);
    if (check != (klassOop)NULL) { 
      // if different instanceKlass - duplicate class definition,
      // else - ok, class loaded by a different thread in parallel,
      // we should only have found it if it was done loading and ok to use 
      // system dictionary only holds instance classes, placeholders
      // also holds array classes
      
      assert(check->klass_part()->oop_is_instance(), "noninstance in systemdictionary");
      if ((defining == true) || (k() != check)) {
        linkage_error = "loader (instance of  %s): attempted  duplicate class "
	  "definition for name: \"%s\"";
      } else {
        return;
      }
    }

#ifdef ASSERT
    unsigned int p_hash = placeholders()->compute_hash(name, class_loader);
    int p_index = placeholders()->hash_to_index(p_hash);
    symbolOop ph_check = find_placeholder(p_index, p_hash, name, class_loader);
    assert(ph_check == NULL || ph_check == name(), "invalid symbol");
#endif

    if (linkage_error == NULL) {
      if (constraints()->check_or_update(k, class_loader, name) == false) {
	linkage_error = "loader constraint violation: loader (instance of %s)"
	  " previously initiated loading for a different type with name \"%s\"";
      }
    }
  }

  // Throw error now if needed (cannot throw while holding 
  // SystemDictionary_lock because of rank ordering)

  if (linkage_error) {
    ResourceMark rm(THREAD);
    const char* class_loader_name = loader_name(class_loader());
    char* type_name = k->name()->as_C_string();
    size_t buflen = strlen(linkage_error) + strlen(class_loader_name) +
      strlen(type_name);
    char* buf = NEW_RESOURCE_ARRAY_IN_THREAD(THREAD, char, buflen);
    jio_snprintf(buf, buflen, linkage_error, class_loader_name, type_name);
    THROW_MSG(vmSymbols::java_lang_LinkageError(), buf);
  }
}


// Update system dictionary - done after check_constraint and add_to_hierachy 
// have been called.
void SystemDictionary::update_dictionary(int d_index, unsigned int d_hash,
                                         int p_index, unsigned int p_hash,
                                         instanceKlassHandle k, 
                                         Handle class_loader, 
                                         TRAPS) {
  // Compile_lock prevents systemDictionary updates during compilations
  assert_locked_or_safepoint(Compile_lock);
  symbolHandle name (THREAD, k->name());

  {
  MutexLocker mu1(SystemDictionary_lock, THREAD);           

  // Check for a placeholder. If there, remove it and make a
  // new system dictionary entry.
  placeholders()->find_and_remove(p_index, p_hash, name, class_loader, THREAD);
  klassOop sd_check = find_class(d_index, d_hash, name, class_loader);
  if (sd_check == NULL) {
    dictionary()->add_klass(name, class_loader, k);
    notice_modification();
  }
#ifdef ASSERT
  sd_check = find_class(d_index, d_hash, name, class_loader);
  assert (sd_check != NULL, "should have entry in system dictionary");
// Changed to allow PH to remain to complete class circularity checking
// while only one thread can define a class at one time, multiple
// classes can resolve the superclass for a class at one time, 
// and the placeholder is used to track that
//  symbolOop ph_check = find_placeholder(p_index, p_hash, name, class_loader);
//  assert (ph_check == NULL, "should not have a placeholder entry");
#endif
SystemDictionary_lock.notify_all();
  }
}


klassOop SystemDictionary::find_constrained_instance_or_array_klass(
                    symbolHandle class_name, Handle class_loader, TRAPS) {

  // First see if it has been loaded directly.
  // Force the protection domain to be null.  (This removes protection checks.)
  Handle no_protection_domain;
  klassOop klass = find_instance_or_array_klass(class_name, class_loader,
                                                no_protection_domain, CHECK_NULL);
  if (klass != NULL)
    return klass;

  // Now look to see if it has been loaded elsewhere, and is subject to
  // a loader constraint that would require this loader to return the
  // klass that is already loaded.
  MutexLocker mu(SystemDictionary_lock, THREAD);
  if (FieldType::is_array(class_name())) {
    // Array classes are hard because their klassOops are not kept in the
    // constraint table. The array klass may be constrained, but the elem class
    // may not be. 
    jint dimension;
    symbolOop object_key;
    BasicType t = FieldType::get_array_info(class_name(), &dimension,
                                            &object_key, CHECK_(NULL));
    if (t != T_OBJECT) {
      klass = Universe::typeArrayKlassObj(t);
    } else {
      symbolHandle elem_name(THREAD, object_key);
      klass = constraints()->find_constrained_elem_klass(class_name, elem_name, class_loader, THREAD);
    }
    if (klass != NULL) {
      klass = Klass::cast(klass)->array_klass_or_null(dimension);
    }
  } else {
    // Non-array classes are easy: simply check the constraint table.
    klass = constraints()->find_constrained_klass(class_name, class_loader);
  }
      
  return klass;
}


bool SystemDictionary::add_loader_constraint(symbolHandle class_name,
                                             Handle class_loader1,
                                             Handle class_loader2, 
                                             Thread* THREAD) {
if(class_name->byte_at(0)=='['&&
(class_name->byte_at(1)=='['||class_name->byte_at(1)=='L')){
    ResourceMark rm;
    const char *p = class_name->as_utf8()+1;
    int len = class_name->utf8_length()-1;
    if( p[0] == 'L' && p[len-1] == ';' ) {
      p++; len-=2;
    }
class_name=oopFactory::new_symbol(p,len,THREAD);
    return add_loader_constraint(class_name,class_loader1,class_loader2,THREAD);
  } 

  unsigned int d_hash1 = dictionary()->compute_hash(class_name, class_loader1);
  int d_index1 = dictionary()->hash_to_index(d_hash1);

  unsigned int d_hash2 = dictionary()->compute_hash(class_name, class_loader2);
  int d_index2 = dictionary()->hash_to_index(d_hash2);

  {
    MutexLocker mu_s(SystemDictionary_lock, THREAD);

    // Better never do a GC while we're holding these oops
    No_Safepoint_Verifier nosafepoint;

    klassOop klass1 = find_class(d_index1, d_hash1, class_name, class_loader1);
    klassOop klass2 = find_class(d_index2, d_hash2, class_name, class_loader2);
    return constraints()->add_entry(class_name, klass1, class_loader1,
				    klass2, class_loader2);
  }
}

// Add entry to resolution error table to record the error when the first
// attempt to resolve a reference to a class has failed.
void SystemDictionary::add_resolution_error(constantPoolHandle pool, int which, symbolHandle error) {
  unsigned int hash = resolution_errors()->compute_hash(pool, which);
  int index = resolution_errors()->hash_to_index(hash);
  { 
    MutexLocker ml(SystemDictionary_lock, Thread::current());
    resolution_errors()->add_entry(index, hash, pool, which, error);
  }
}

// Lookup resolution error table. Returns error if found, otherwise NULL.
symbolOop SystemDictionary::find_resolution_error(constantPoolHandle pool, int which) {
  unsigned int hash = resolution_errors()->compute_hash(pool, which);
  int index = resolution_errors()->hash_to_index(hash);
  { 
    MutexLocker ml(SystemDictionary_lock, Thread::current());
    ResolutionErrorEntry* entry = resolution_errors()->find_entry(index, hash, pool, which);
return(entry!=NULL)?entry->error().as_symbolOop():(symbolOop)NULL;
  }
}


// Make sure all class components (including arrays) in the given
// signature will be resolved to the same class in both loaders.
// Returns the name of the type that failed a loader constraint check, or
// NULL if no constraint failed. The returned C string needs cleaning up
// with a ResourceMark in the caller
char* SystemDictionary::check_signature_loaders(symbolHandle signature,
                                               Handle loader1, Handle loader2,
                                               bool is_method, TRAPS)  {
  // Nothing to do if loaders are the same. 
  if (loader1() == loader2()) {
    return NULL;
  }
  
  SignatureStream sig_strm(signature, is_method);
  while (!sig_strm.is_done()) {
    if (sig_strm.is_object()) {
      symbolOop s = sig_strm.as_symbol(CHECK_NULL);
      symbolHandle sig (THREAD, s);
      if (!add_loader_constraint(sig, loader1, loader2, THREAD)) {
	return sig()->as_C_string();
      }
    }
    sig_strm.next();
  }
  return NULL;
}


// Since the identity hash code for symbols changes when the symbols are
// moved from the regular perm gen (hash in the mark word) to the shared
// spaces (hash is the address), the classes loaded into the dictionary
// may be in the wrong buckets.

void SystemDictionary::reorder_dictionary() {
  dictionary()->reorder_dictionary();
}


void SystemDictionary::copy_buckets(char** top, char* end) {
  dictionary()->copy_buckets(top, end);
}


void SystemDictionary::copy_table(char** top, char* end) {
  dictionary()->copy_table(top, end);
}


void SystemDictionary::reverse() {
  dictionary()->reverse();
}

int SystemDictionary::number_of_classes() {
  return dictionary()->number_of_entries();
}

void SystemDictionary::reset_unloaded_classes(){
dictionary()->reset_unloaded_classes();
}

int SystemDictionary::number_of_unloaded_classes(){
return dictionary()->number_of_unloaded_classes();
}

static GrowableArray<klassOop>* global_print_xml_array;

static void add_klass_to_array(klassOop k){
global_print_xml_array->append(k);
}

static int compare_klass(klassOop* k1, klassOop* k2) {
  symbolOop k1name = (*k1)->klass_part()->name();
  symbolOop k2name = (*k2)->klass_part()->name();
  
if(k1name==NULL)return-1;
if(k2name==NULL)return 1;
  
  return k1name->fast_compare(k2name);
}

void SystemDictionary::print_xml_on(xmlBuffer *xb) {
  GrowableArray<klassOop> print_xml_array(number_of_classes(), false);
  { // We need to access a static as a "closure" parameter.
    // This is protected by the SystemDictionary_lock to also get a consistent view of the classlist.
    MutexLocker sd(SystemDictionary_lock);
    global_print_xml_array = &print_xml_array;
    classes_do(&add_klass_to_array);
global_print_xml_array=NULL;
  }
  print_xml_array.sort(&compare_klass);
  
  { xmlElement xe(xb, "class_list");
for(int i=0;i<print_xml_array.length();i++){
      xmlElement xe(xb, "class_list_entry");
      print_xml_array.at(i)->print_xml_on(xb, true);
    }
  }
}

// ----------------------------------------------------------------------------
#ifndef PRODUCT

void SystemDictionary::print() {
  dictionary()->print();

  // Placeholders
  GCMutexLocker mu(SystemDictionary_lock);
  placeholders()->print();

  // loader constraints - print under SD_lock
constraints()->print(tty);
}

#endif

void SystemDictionary::verify() {
  guarantee(dictionary() != NULL, "Verify of system dictionary failed");
  guarantee(constraints() != NULL,
            "Verify of loader constraints failed");
  guarantee(dictionary()->number_of_entries() >= 0 &&
            placeholders()->number_of_entries() >= 0,
            "Verify of system dictionary failed");

  // Verify dictionary
  dictionary()->verify();

  GCMutexLocker mu(SystemDictionary_lock);
  placeholders()->verify();

  // Verify constraint table
  guarantee(constraints() != NULL, "Verify of loader constraints failed");
  constraints()->verify(dictionary());
}


void SystemDictionary::verify_obj_klass_present(Handle obj,
                                                symbolHandle class_name,
                                                Handle class_loader) {
  GCMutexLocker mu(SystemDictionary_lock);
  oop probe = find_class_or_placeholder(class_name, class_loader);
  if (probe == NULL) {
    probe = SystemDictionary::find_shared_class(class_name);
  }
  guarantee(probe != NULL && 
            (!probe->is_klass() || probe == obj()), 
                     "Loaded klasses should be in SystemDictionary");
}

#ifndef PRODUCT

// statistics code
class ClassStatistics: AllStatic {
 private:
  static int nclasses;        // number of classes
static int num_methods;//number of methods
  static int nbci2cpd;        // number of bci to cpd maps
  static int ncodeprofile;    // number of code profiles
  static int class_size;      // size of class objects in words
  static int method_size;     // size of method objects in words
  static int debug_size;      // size of debug info in methods
  static int bci2cpd_size;    // size of codeprofiles in bytes
  static int codeprofile_size;// size of codeprofiles in bytes

  static void do_class(klassOop k) {
    nclasses++;
    class_size += k->size();
    if (k->klass_part()->oop_is_instance()) {
      instanceKlass* ik = (instanceKlass*)k->klass_part();
      class_size += ik->methods()->size();
      class_size += ik->constants()->size();
      class_size += ik->local_interfaces()->size();
      class_size += ik->transitive_interfaces()->size();
      // We do not have to count implementors, since we only store one!      
      class_size += ik->fields()->size();
    }
  }

  static void do_method(methodOop m) {
    num_methods++;
    method_size += m->size();
    // class loader uses same objArray for empty vectors, so don't count these
    if (m->exception_table()->length() != 0)   method_size += m->exception_table()->size();
    if (m->has_stackmap_table()) {
      method_size += m->stackmap_data()->size();
    }

    Unimplemented();
//    BCI2CPD_mapping *bci2cpd = m->has_bci2cpd_map();
//    if( bci2cpd ) {
//      nbci2cpd++;
//      bci2cpd_size += sizeof(BCI2CPD_mapping) + bci2cpd->codesize();
//      int size = m->get_codeprofile_size();
//      if (size) {
//        ncodeprofile++;
//        codeprofile_size += size;
//      }
//    }
  }

 public:
  static void print() {
    SystemDictionary::classes_do(do_class);
    SystemDictionary::methods_do(do_method);
    tty->print_cr("Class statistics:");
    tty->print_cr("%d classes (%d bytes)", nclasses, class_size * oopSize);
tty->print_cr("%d methods (%d bytes = %d base + %d debug info)",num_methods,
                  (method_size + debug_size) * oopSize, method_size * oopSize, debug_size * oopSize);
tty->print_cr("%d BCI2CPD maps (%d bytes) and %d CodeProfiles (%d bytes)",
                  nbci2cpd, bci2cpd_size, ncodeprofile, codeprofile_size);
  }
};


int ClassStatistics::nclasses        = 0;  
int ClassStatistics::num_methods=0;
int ClassStatistics::nbci2cpd=0;
int ClassStatistics::ncodeprofile=0;
int ClassStatistics::class_size      = 0;
int ClassStatistics::method_size     = 0; 
int ClassStatistics::debug_size      = 0;
int ClassStatistics::bci2cpd_size=0;
int ClassStatistics::codeprofile_size=0;

void SystemDictionary::print_class_statistics() {
  ResourceMark rm;
  ClassStatistics::print();
}


class MethodStatistics: AllStatic {
 public:
  enum {
    max_parameter_size = 10
  };
 private:

  static int _number_of_methods;
  static int _number_of_final_methods;
  static int _number_of_static_methods;
  static int _number_of_native_methods;
  static int _number_of_synchronized_methods;
  static int _number_of_profiled_methods;
  static int _number_of_bytecodes;
  static int _parameter_size_profile[max_parameter_size];
  static int _bytecodes_profile[Bytecodes::number_of_java_codes];

  static void initialize() {
    _number_of_methods        = 0;
    _number_of_final_methods  = 0;
    _number_of_static_methods = 0;
    _number_of_native_methods = 0;
    _number_of_synchronized_methods = 0;
    _number_of_profiled_methods = 0;
    _number_of_bytecodes      = 0;
    for (int i = 0; i < max_parameter_size             ; i++) _parameter_size_profile[i] = 0;
    for (int j = 0; j < Bytecodes::number_of_java_codes; j++) _bytecodes_profile     [j] = 0;
  };

  static void do_method(methodOop m) {
    _number_of_methods++;
    // collect flag info
    if (m->is_final()       ) _number_of_final_methods++;
    if (m->is_static()      ) _number_of_static_methods++;
    if (m->is_native()      ) _number_of_native_methods++;
    if (m->is_synchronized()) _number_of_synchronized_methods++;
if(m->get_codeprofile_size())_number_of_profiled_methods++;
    // collect parameter size info (add one for receiver, if any)
    _parameter_size_profile[MIN2(m->size_of_parameters() + (m->is_static() ? 0 : 1), max_parameter_size - 1)]++;
    // collect bytecodes info
    { 
      Thread *thread = Thread::current();
      HandleMark hm(thread);
      BytecodeStream s(methodHandle(thread, m));
      Bytecodes::Code c;
      while ((c = s.next()) >= 0) {
        _number_of_bytecodes++;
        _bytecodes_profile[c]++;
      }
    }
  }

 public:
  static void print() {
    initialize();
    SystemDictionary::methods_do(do_method);
    // generate output
    tty->cr();
    tty->print_cr("Method statistics (static):");
    // flag distribution
    tty->cr();
    tty->print_cr("%6d final        methods  %6.1f%%", _number_of_final_methods       , _number_of_final_methods        * 100.0F / _number_of_methods);
    tty->print_cr("%6d static       methods  %6.1f%%", _number_of_static_methods      , _number_of_static_methods       * 100.0F / _number_of_methods);
    tty->print_cr("%6d native       methods  %6.1f%%", _number_of_native_methods      , _number_of_native_methods       * 100.0F / _number_of_methods);
    tty->print_cr("%6d synchronized methods  %6.1f%%", _number_of_synchronized_methods, _number_of_synchronized_methods * 100.0F / _number_of_methods);
    tty->print_cr("%6d profiled     methods  %6.1f%%", _number_of_profiled_methods    , _number_of_profiled_methods     * 100.0F / _number_of_methods);
    // parameter size profile
    tty->cr();
    { int tot = 0;
      int avg = 0;
      for (int i = 0; i < max_parameter_size; i++) {
        int n = _parameter_size_profile[i];
        tot += n;
        avg += n*i;
        tty->print_cr("parameter size = %1d: %6d methods  %5.1f%%", i, n, n * 100.0F / _number_of_methods);
      }
      assert(tot == _number_of_methods, "should be the same");
      tty->print_cr("                    %6d methods  100.0%%", _number_of_methods);
      tty->print_cr("(average parameter size = %3.1f including receiver, if any)", (float)avg / _number_of_methods);
    }
    // bytecodes profile
    tty->cr();
    { int tot = 0;
      for (int i = 0; i < Bytecodes::number_of_java_codes; i++) {
        if (Bytecodes::is_defined(i)) {
          Bytecodes::Code c = Bytecodes::cast(i);
          int n = _bytecodes_profile[c];
          tot += n;
          tty->print_cr("%9d  %7.3f%%  %s", n, n * 100.0F / _number_of_bytecodes, Bytecodes::name(c));
        }
      }
      assert(tot == _number_of_bytecodes, "should be the same");
      tty->print_cr("%9d  100.000%%", _number_of_bytecodes);
    }
    tty->cr();
  }
};

int MethodStatistics::_number_of_methods;
int MethodStatistics::_number_of_final_methods;
int MethodStatistics::_number_of_static_methods;
int MethodStatistics::_number_of_native_methods;
int MethodStatistics::_number_of_synchronized_methods;
int MethodStatistics::_number_of_profiled_methods;
int MethodStatistics::_number_of_bytecodes;
int MethodStatistics::_parameter_size_profile[MethodStatistics::max_parameter_size];
int MethodStatistics::_bytecodes_profile[Bytecodes::number_of_java_codes];


void SystemDictionary::print_method_statistics() {
  MethodStatistics::print();
}

#endif // PRODUCT
