#include "cap.h"
#include "trace.h"

//#include "../framework/analysis_callback.h"

#include "../tinyformat.h" // for testing purpose

#include <limits>
#include <cassert>
#include <bitset>
#include <functional>

#include <boost/type_traits.hpp>
#include <typeinfo>

enum tracing_state_t
{
  NOT_STARTED         = 0,
  FULL_SUSPENDED      = 1,
  SELECTIVE_SUSPENDED = 2,
  ENABLED             = 3,
  DISABLED            = 4
};

using syscall_info_t = std::tuple<
  ADDRINT, // syscall number
  ADDRINT, // return value
  ADDRINT, // 1st argument
  ADDRINT, // 2nd
  ADDRINT, // 3rd
  ADDRINT  // 4th
  >;

enum
  {
    SYSCALL_ID    = 0,
    SYSCALL_RET   = 1,
    SYSCALL_ARG_0 = 2,
    SYSCALL_ARG_1 = 3,
    SYSCALL_ARG_2 = 4,
    SYSCALL_ARG_3 = 5
  };

// for patching
using exec_point_t                  = std::pair<ADDRINT, UINT32>;
using patch_point_t                 = std::pair<exec_point_t, bool>;

using register_patch_value_t        = std::tuple<REG,     // patched register
                                                 UINT8,   // low bit position
                                                 UINT8,   // high bit position
                                                 ADDRINT  // value need to be set
                                                 >;

using memory_patch_value_t          = std::tuple<ADDRINT, // patched memory address
                                                 UINT8,   // patched size
                                                 ADDRINT  // value need to be set
                                                 >;

using indirect_memory_patch_value_t = std::tuple<REG,     // register containinng patched memory address
                                                 UINT8,   // patched size
                                                 ADDRINT  // value need to be set
                                                 >;

using patch_point_register_t        = std::pair<patch_point_t, register_patch_value_t>;
using patch_point_memory_t          = std::pair<patch_point_t, memory_patch_value_t>;
using patch_point_indirect_memory_t = std::pair<patch_point_t, indirect_memory_patch_value_t>;

// using auto here is not supported by C++11 standard (why?)
dyn_inss_t trace                             = dyn_inss_t();
map_address_instruction_t cached_ins_at_addr = map_address_instruction_t();

static auto state_of_thread                    = std::map<THREADID, tracing_state_t>();
static auto ins_at_thread                      = std::map<THREADID, dyn_ins_t>();
static auto resume_address_of_thread           = std::map<THREADID, ADDRINT>();

static auto start_address                      = ADDRINT{0};
static auto stop_address                       = ADDRINT{0};
static auto full_skip_call_addresses           = std::vector<ADDRINT>();
static auto selective_skip_call_addresses      = std::vector<ADDRINT>();
static auto auto_skip_call_addresses           = std::vector<ADDRINT>();
static auto max_trace_length                   = uint32_t{0};
static auto loop_count                         = uint32_t{0};

static auto patched_register_at_address        = std::vector<patch_point_register_t>();
static auto patched_memory_at_address          = std::vector<patch_point_memory_t>();
static auto patched_indirect_memory_at_address = std::vector<patch_point_indirect_memory_t>();
static auto execution_order_of_address         = std::map<ADDRINT, UINT32>();

static auto current_syscall_info               = syscall_info_t();

static auto some_thread_is_started             = false;
static auto some_thread_is_not_suspended       = true;
static auto some_thread_is_selective_suspended = false;

/*====================================================================================================================*/
/*                                       callback analysis and support functions                                      */
/*====================================================================================================================*/


enum event_t
{
  NEW_THREAD          = 0,
  ENABLE_TO_SUSPEND   = 1,
  ANY_TO_DISABLE      = 2,
  ANY_TO_TERMINATE    = 3,
  NOT_START_TO_ENABLE = 4,
  SUSPEND_TO_ENABLE   = 5
};


auto normalize_hex_string (const std::string& input) -> std::string
{
  assert(input.find("0x") == 0);
  auto first_non_zero_iter = std::find_if(std::begin(input) + 2, std::end(input), [](char c) { return (c != '0');});
  auto output = first_non_zero_iter != std::end(input) ? std::string(first_non_zero_iter, std::end(input)) : std::string("0");
  return ("0x" + output);
}

static auto reinstrument_if_some_thread_started (ADDRINT current_addr,
                                                 ADDRINT next_addr, const CONTEXT* p_ctxt) -> void
{
//  assert(current_addr != start_address);
  ASSERTX(!some_thread_is_started);

  if (cached_ins_at_addr[current_addr]->is_ret) {
//    ASSERTX(PIN_GetContextReg(p_ctxt, REG_STACK_PTR) == next_addr);

    auto return_addr = next_addr;
    PIN_SafeCopy(&return_addr, reinterpret_cast<ADDRINT*>(next_addr), sizeof(ADDRINT));
    next_addr = return_addr;
  }

  if (next_addr == start_address) {
    some_thread_is_started = true;

    tfm::format(std::cerr, "the next executed instruction is at %s (current %s), restart instrumentation...\n",
                StringFromAddrint(next_addr), StringFromAddrint(current_addr));

    PIN_RemoveInstrumentation();
//    CODECACHE_InvalidateTraceAtProgramAddress(start_address);
//    CODECACHE_InvalidateRange(start_address, stop_address);
//    CODECACHE_FlushCache();
    PIN_ExecuteAt(p_ctxt);
  }
  return;
}


static auto reinstrument_because_of_suspended_state (const CONTEXT* p_ctxt, ADDRINT ins_addr) -> void
{
  auto new_state = std::any_of(std::begin(state_of_thread), std::end(state_of_thread),
                               [](decltype(state_of_thread)::const_reference thread_state) {
      static_assert(std::is_same<decltype(std::get<1>(thread_state)), const tracing_state_t&>::value, "type conflict");

      return (std::get<1>(thread_state) != FULL_SUSPENDED) && (std::get<1>(thread_state) != SELECTIVE_SUSPENDED);
  });

  some_thread_is_selective_suspended = std::any_of(std::begin(state_of_thread), std::end(state_of_thread),
                                                   [](decltype(state_of_thread)::const_reference thread_state) {

      static_assert(std::is_same<decltype(std::get<1>(thread_state)), const tracing_state_t&>::value, "type conflict");

      return (std::get<1>(thread_state) == SELECTIVE_SUSPENDED);
  });

  if (new_state != some_thread_is_not_suspended) {
    some_thread_is_not_suspended = new_state;

    tfm::format(std::cerr, "state changed to %s, restart instrumentation...\n",
                !some_thread_is_not_suspended ? "suspend" : "enable");

    PIN_RemoveInstrumentation();
    PIN_ExecuteAt(p_ctxt);
//    CODECACHE_FlushCache();
  }

  return;
}


template <event_t event>
static auto update_condition (ADDRINT ins_addr, THREADID thread_id) -> void
{
  static_assert((event == NEW_THREAD) || (event == ENABLE_TO_SUSPEND) ||
                (event == ANY_TO_DISABLE) || (event == ANY_TO_TERMINATE) ||
                (event == NOT_START_TO_ENABLE) || (event == SUSPEND_TO_ENABLE), "unknow event");

  switch (event) {
  case NEW_THREAD:
    if (state_of_thread.find(thread_id) == state_of_thread.end()) {
      state_of_thread[thread_id] = NOT_STARTED;
    }
    break;

  case ENABLE_TO_SUSPEND:
    if (ins_at_thread.find(thread_id) != ins_at_thread.end()) {
      if (state_of_thread[thread_id] == ENABLED) {

        auto thread_ins_addr = std::get<INS_ADDRESS>(ins_at_thread[thread_id]);

        if (std::find(
              std::begin(full_skip_call_addresses), std::end(full_skip_call_addresses), thread_ins_addr)
            != std::end(full_skip_call_addresses)) {

          tfm::format(std::cerr, "suspend thread %d...\n", thread_id);

          state_of_thread[thread_id] = FULL_SUSPENDED;
        }

        if (std::find(
              std::begin(selective_skip_call_addresses), std::end(selective_skip_call_addresses), thread_ins_addr)
            != std::end(selective_skip_call_addresses)) {

          tfm::format(std::cerr, "suspend (selective) thread %d...\n", thread_id);
          state_of_thread[thread_id] = SELECTIVE_SUSPENDED;
        }

        if ((std::get<INS_ADDRESS>(ins_at_thread[thread_id]) == stop_address) && (stop_address != 0x0)) {
          state_of_thread[thread_id] = FULL_SUSPENDED;
        }
      }
    }
    break;

  case ANY_TO_DISABLE:
    if (ins_at_thread.find(thread_id) != ins_at_thread.end()) {
      if ((state_of_thread[thread_id] != NOT_STARTED) && (state_of_thread[thread_id] != DISABLED)) {

        if ((std::get<INS_ADDRESS>(ins_at_thread[thread_id]) == stop_address) && (stop_address != 0x0)) {
          loop_count--;
          if (loop_count <= 0) state_of_thread[thread_id] = DISABLED;
        }
      }
    }
    break;

  case ANY_TO_TERMINATE:
    if (std::all_of(std::begin(state_of_thread), std::end(state_of_thread),
                    [](decltype(state_of_thread)::const_reference thread_state)
                    { return (std::get<1>(thread_state) == DISABLED); })) {
      tfm::format(std::cerr, "all execution threads are terminated, exit application...\n");
      PIN_ExitApplication(1);
    }
    break;

  case NOT_START_TO_ENABLE:
    if (((ins_addr == start_address) || (start_address == 0x0)) && (state_of_thread[thread_id] == NOT_STARTED)) {
      state_of_thread[thread_id] = ENABLED;
    }
    break;

  case SUSPEND_TO_ENABLE:
    if ((state_of_thread[thread_id] == FULL_SUSPENDED) || (state_of_thread[thread_id] == SELECTIVE_SUSPENDED)) {

      if (ins_addr == resume_address_of_thread[thread_id]) {
        tfm::format(std::cerr, "enable thread %d...\n", thread_id);

        state_of_thread[thread_id] = ENABLED;
      }

      if (ins_addr == start_address) {
        tfm::format(std::cerr, "enable thread %d...\n", thread_id);

        state_of_thread[thread_id] = ENABLED;
      }
    }
    break;
  }

  return;
}


static auto initialize_instruction (ADDRINT ins_addr, THREADID thread_id) -> void
{
  if ((state_of_thread[thread_id] == ENABLED) ||
      ((state_of_thread[thread_id] == SELECTIVE_SUSPENDED) && (cached_ins_at_addr[ins_addr]->is_syscall))) {
    ins_at_thread[thread_id] = dyn_ins_t(ins_addr,          // instruction address
                                         thread_id,         // thread id
                                         dyn_regs_t(),      // read registers
                                         dyn_regs_t(),      // written registers
                                         dyn_mems_t(),      // read memory addresses
                                         dyn_mems_t(),      // write memory addresses
                                         concrete_info_t{}, // other concrete information
                                         0x0                // next instruction address
                                         );
  }
  return;
}


static auto update_resume_address (ADDRINT resume_addr, THREADID thread_id) -> void
{
  ASSERTX(state_of_thread.find(thread_id) != state_of_thread.end());

  if (state_of_thread[thread_id] == ENABLED) {
    resume_address_of_thread[thread_id] = resume_addr;

//    tfm::printfln("resume address = 0x%x", resume_addr);
  }
  return;
}


template <bool read_or_write>
static auto save_register (const CONTEXT* p_context, THREADID thread_id) -> void
{
  if (ins_at_thread.find(thread_id) != ins_at_thread.end()) {

    auto ins_addr = std::get<INS_ADDRESS>(ins_at_thread[thread_id]);
    const auto & current_ins = cached_ins_at_addr[ins_addr];

    if (((state_of_thread[thread_id] == ENABLED) && !current_ins->is_special) ||
        ((state_of_thread[thread_id] == SELECTIVE_SUSPENDED) && current_ins->is_syscall)) {

      const auto & regs = !read_or_write ? current_ins->src_registers : current_ins->dst_registers;

      auto & reg_map = !read_or_write ? std::get<INS_READ_REGS>(ins_at_thread[thread_id]) :
                                        std::get<INS_WRITE_REGS>(ins_at_thread[thread_id]);

      for (auto const& reg : regs) {
        static PIN_REGISTER reg_value;
        PIN_GetContextRegval(p_context, reg, reinterpret_cast<uint8_t*>(&reg_value));
        reg_map[reg] = reg_value;
      }
    }
  }

  return;
}

template <typename T>
auto assign_value (dyn_mems_t& mem_map, ADDRINT addr) -> void
{
  mem_map[dyn_mem_t(addr, sizeof(T))] = *(reinterpret_cast<T*>(addr));
  return;
}

enum rw_t { READ = 0, WRITE = 1 };

template <rw_t read_or_write>
static auto save_memory (ADDRINT mem_addr, UINT32 mem_size, THREADID thread_id) -> void
{
  static_assert((read_or_write == READ) || (read_or_write == WRITE), "unknown action");

  static auto save_memory_size = std::map<
      uint8_t, std::function<void (dyn_mems_t&, ADDRINT)>
      > {
    {1, assign_value<uint8_t>}, {2, assign_value<uint16_t>},
    {4, assign_value<uint32_t>}, {8, assign_value<uint64_t>}
  };

  if (ins_at_thread.find(thread_id) != ins_at_thread.end()) {

    if ((state_of_thread[thread_id] == ENABLED) ||
        ((state_of_thread[thread_id] == SELECTIVE_SUSPENDED) &&
         cached_ins_at_addr[std::get<INS_ADDRESS>(ins_at_thread[thread_id])]->is_syscall)) {

      // any chance for compile time evaluation !?
      auto& mem_map = (read_or_write == READ) ? std::get<INS_READ_MEMS>(ins_at_thread[thread_id]) :
                                                std::get<INS_WRITE_MEMS>(ins_at_thread[thread_id]);

      if (mem_size != 0) {
        ASSERTX((mem_size == 1) || (mem_size == 2) || (mem_size == 4) || (mem_size == 8));

        // some obscure programs use accessing to zero exception to exit
        if (mem_addr != 0) {
          if (read_or_write == READ) save_memory_size[mem_size](mem_map, mem_addr);
          else mem_map[dyn_mem_t(mem_addr, mem_size)] = 0;
        }
      }
      else { // save_memory is called with mem_size == 0
        ASSERTX((mem_map.size() == 1) || (mem_map.size() == 0));
        ASSERTX(mem_addr == 0);

        if (mem_map.size() == 1) {
          ASSERTX(cached_ins_at_addr[std::get<INS_ADDRESS>(ins_at_thread[thread_id])]->is_memory_write);

          auto stored_mem      = std::begin(mem_map);
          auto mem_addr_size   = std::get<0>(*stored_mem);
          auto mem_addr        = std::get<0>(mem_addr_size);
          auto stored_mem_size = std::get<1>(mem_addr_size);

          ASSERTX((stored_mem_size == 1) || (stored_mem_size == 2) ||
                 (stored_mem_size == 4) || (stored_mem_size == 8));

          ASSERTX(mem_map[dyn_mem_t(mem_addr, stored_mem_size)] == 0);

          save_memory_size[stored_mem_size](mem_map, mem_addr);
        }
      }
    }
  }

  return;
}

//static auto save_not_retrieved_concrete_info (THREADID thread_id) noexcept -> void
//{
//  if (ins_at_thread.find(thread_id) != ins_at_thread.end()) {
//    assert(cached_ins_at_addr[std::get<INS_ADDRESS>(ins_at_thread[thread_id])]->is_special);

//    if (state_of_thread[thread_id] == ENABLED) {

//    }
//  }
//  return;
//}

static auto save_call_concrete_info (ADDRINT called_addr, THREADID thread_id) -> void
{
  auto get_called_func_name = [](ADDRINT called_addr) -> std::string {
    PIN_LockClient();
    auto routine = RTN_FindByAddress(called_addr);
    PIN_UnlockClient();

    if (RTN_Valid(routine)) {
      auto routine_mangled_name = RTN_Name(routine);
      return PIN_UndecorateSymbolName(routine_mangled_name, UNDECORATION_NAME_ONLY);
    }
    else return "";
  };

  if (ins_at_thread.find(thread_id) != ins_at_thread.end()) {
    ASSERTX(cached_ins_at_addr[std::get<INS_ADDRESS>(ins_at_thread[thread_id])]->is_call);

    if (state_of_thread[thread_id] == ENABLED) {
      auto call_info = call_info_t{};
      call_info.called_fun_addr = called_addr;

      if (cached_ins_at_addr.find(called_addr) != cached_ins_at_addr.end()) {
        call_info.called_fun_name = cached_ins_at_addr[called_addr]->including_routine_name;
      }
      else {
        call_info.called_fun_name = get_called_func_name(called_addr);
      }

      auto ins_addr = std::get<INS_ADDRESS>(ins_at_thread[thread_id]);
      if ((std::find(
             std::begin(full_skip_call_addresses), std::end(full_skip_call_addresses), ins_addr
             ) != std::end(full_skip_call_addresses)) ||
          (std::find(
             std::begin(selective_skip_call_addresses), std::end(selective_skip_call_addresses), ins_addr
             ) != std::end(selective_skip_call_addresses))) {
        call_info.is_traced = false;
      }
      else {
        call_info.is_traced = true;
      }

      std::get<INS_CONCRETE_INFO>(ins_at_thread[thread_id]) = call_info;
    }
  }
  return;
}


static auto add_to_trace (ADDRINT ins_addr, THREADID thread_id) -> void
{
  if (ins_at_thread.find(thread_id) != ins_at_thread.end()) {

    if ((state_of_thread[thread_id] == ENABLED) ||
        ((state_of_thread[thread_id] == SELECTIVE_SUSPENDED) &&
         cached_ins_at_addr[std::get<INS_ADDRESS>(ins_at_thread[thread_id])]->is_syscall)) {

//      tfm::printfln("0x%-12x %s", ins_addr, cached_ins_at_addr[ins_addr]->disassemble);

      std::get<INS_NEXT_ADDRESS>(ins_at_thread[thread_id]) = ins_addr;
      trace.push_back(ins_at_thread[thread_id]);

      if (trace.size() >= 5000) cap_flush_trace();
    }
  }

  return;
}


static auto remove_previous_instruction (THREADID thread_id) -> void
{
  if (ins_at_thread.find(thread_id) != ins_at_thread.end()) {
    ins_at_thread.erase(thread_id);
  }
  return;
}


static auto update_execution_order (ADDRINT ins_addr, THREADID thread_id) -> void
{
  (void)thread_id;

  static auto last_updated_address = ADDRINT{0x0};

  // this verification is erronous in case of rep instructions (so we should not patch at them)
  if (last_updated_address != ins_addr) {
    execution_order_of_address[ins_addr]++;
  }
  last_updated_address = ins_addr;
  return;
}


template<typename reg_value_type>
static auto patch_register_of_type (ADDRINT org_patch_val,
                                    uint8_t val_lo_pos, uint8_t val_hi_pos,
                                    PIN_REGISTER* p_pin_reg) -> void
{
  auto reg_patch_val = org_patch_val << val_lo_pos;
  auto patch_val_bitsec = std::bitset<std::numeric_limits<reg_value_type>::digits>(reg_patch_val);

  auto pin_current_val = *(reinterpret_cast<reg_value_type*>(p_pin_reg));
  auto current_val_bitset = std::bitset<numeric_limits<reg_value_type>::digits>(pin_current_val);

  for (auto i = 0; i < std::numeric_limits<reg_value_type>::digits; ++i) {
    if ((i < val_lo_pos) || (i > val_hi_pos)) patch_val_bitsec[i] = current_val_bitset[i];
  }

  tfm::format(std::cerr, "value before patching = 0x%x\n", pin_current_val);
  *(reinterpret_cast<reg_value_type*>(p_pin_reg)) = patch_val_bitsec.to_ulong();
  tfm::format(std::cerr, "value after patching = 0x%x\n", *(reinterpret_cast<reg_value_type*>(p_pin_reg)));
  return;
}


static auto patch_register (ADDRINT ins_addr, bool patch_point,
                            UINT32 patch_reg, PIN_REGISTER* p_register,
                            THREADID thread_id) -> void
{
  (void)thread_id;
  ASSERTX(REG_valid(static_cast<REG>(patch_reg)) && "the needed to patch register is invalid");

  static auto patch_reg_funs = std::map<
      uint8_t, std::function<void(ADDRINT, uint8_t, uint8_t, PIN_REGISTER*)>
      > {
    {1, patch_register_of_type<uint8_t>}, {2, patch_register_of_type<uint16_t>},
    {4, patch_register_of_type<uint32_t>}, {8, patch_register_of_type<uint64_t>}
  };

  for (auto const& patch_reg_info : patched_register_at_address) {

    auto patch_exec_point  = std::get<0>(patch_reg_info);
    auto patch_reg_value = std::get<1>(patch_reg_info);

    auto exec_point        = std::get<0>(patch_exec_point);
    auto exec_addr         = std::get<0>(exec_point);
    auto exec_order        = std::get<1>(exec_point);
    auto found_patch_point = std::get<1>(patch_exec_point);
    auto found_patch_reg   = std::get<0>(patch_reg_value);

    ASSERTX(REG_valid(found_patch_reg) && "the patched register is invalid");

    if ((exec_addr == ins_addr) && (exec_order == execution_order_of_address[ins_addr]) &&
        (found_patch_point == patch_point) && (found_patch_reg == patch_reg)) {

      auto reg_info        = std::get<1>(patch_reg_info);
      auto reg_size        = static_cast<uint8_t>(REG_Size(std::get<0>(reg_info)));
      auto reg_lo_pos      = std::get<1>(reg_info);
      auto reg_hi_pos      = std::get<2>(reg_info);
      auto reg_patch_val   = std::get<3>(reg_info);

      patch_reg_funs[reg_size](reg_patch_val, reg_lo_pos, reg_hi_pos, p_register);
    }
  }

  return;
}


/*
 * Because the thread_id is not used in this function, the memory patching is realized actually by any thread.
 */
static auto patch_memory (ADDRINT ins_addr, bool patch_point,
                          ADDRINT patch_mem_addr, THREADID thread_id) -> void
{
  (void)thread_id;

  for (auto const& patch_mem_info : patched_memory_at_address) {

    auto patch_exec_point  = std::get<0>(patch_mem_info);
    auto exec_point        = std::get<0>(patch_exec_point);
    auto exec_addr         = std::get<0>(exec_point);
    auto exec_order        = std::get<1>(exec_point);
    auto exec_patch_point  = std::get<1>(patch_exec_point);

    auto patch_mem_val = std::get<1>(patch_mem_info);
    auto found_mem_addr = std::get<0>(patch_mem_val);

    if ((exec_addr == ins_addr) && (exec_order == execution_order_of_address[ins_addr]) &&
        (exec_patch_point == patch_point) && (found_mem_addr == patch_mem_addr)) {

      auto mem_size  = std::get<1>(patch_mem_val);
      auto mem_value = std::get<2>(patch_mem_val);

      tfm::format(std::cerr, "at 0x%x: will patch %d bytes at 0x%x by value 0x%x\n", exec_addr,
                    mem_size, found_mem_addr, mem_value);

      auto excp_info = EXCEPTION_INFO();
      auto patched_mem_size = PIN_SafeCopyEx(reinterpret_cast<uint8_t*>(found_mem_addr),
                                             reinterpret_cast<uint8_t*>(&mem_value), mem_size, &excp_info);
      if (patched_mem_size != mem_size) {
//        auto excp_code = PIN_GetExceptionCode(&excp_info);

//        if (PIN_GetExceptionClass(excp_code) == EXCEPTCLASS_ACCESS_FAULT) {
//          tfm::printfln("accessed to some invalid (i.e. unmapped, protected, etc) address");
//        }
        tfm::format(std::cerr, "error: %s\n", PIN_ExceptionToString(&excp_info));
      }
      else {
        tfm::format(std::cerr, "after patching: ADDRINT value at 0x%x is 0x%x\n", found_mem_addr,
                    *reinterpret_cast<ADDRINT*>(found_mem_addr));
      }
    }
  }
  return;
}


static auto patch_indirect_memory (ADDRINT ins_addr, bool patch_point,
                                   UINT32 patch_indirect_reg, ADDRINT patch_indirect_addr, THREADID thread_id) -> void
{
  (void)thread_id;
  ASSERTX(REG_valid(static_cast<REG>(patch_indirect_reg)) && "the patched register is invalid");

  for (auto const& patch_indirect_mem_info : patched_indirect_memory_at_address) {
    auto patch_exec_point = std::get<0>(patch_indirect_mem_info);
    auto exec_point       = std::get<0>(patch_exec_point);
    auto exec_addr        = std::get<0>(exec_point);
    auto exec_order       = std::get<1>(exec_point);
    auto patch_pos        = std::get<1>(patch_exec_point); // before or after

    auto patch_indirect_info = std::get<1>(patch_indirect_mem_info);
    auto need_to_patch_indirect_reg   = std::get<0>(patch_indirect_info);

    if ((exec_addr == ins_addr) && (exec_order == execution_order_of_address[ins_addr]) &&
        (patch_pos == patch_point) && (need_to_patch_indirect_reg == patch_indirect_reg)) {

      auto mem_size = std::get<1>(patch_indirect_info);
      auto mem_value = std::get<2>(patch_indirect_info);

      tfm::format(std::cerr, "at 0x%x: will patch %d bytes at 0x%x by value 0x%x\n",
                  exec_addr, mem_size, patch_indirect_addr, mem_value);

      auto excp_info = EXCEPTION_INFO();
      auto patched_mem_size = PIN_SafeCopyEx(reinterpret_cast<uint8_t*>(patch_indirect_addr),
                                             reinterpret_cast<uint8_t*>(&mem_value), mem_size, &excp_info);
      if (patched_mem_size != mem_size) {
        tfm::format(std::cerr, "error: %s\n", PIN_ExceptionToString(&excp_info));
      }
      else {
        tfm::format(std::cerr, "after patching: ADDRINT value at 0x%x is 0x%x\n",
                    patch_indirect_addr, *reinterpret_cast<ADDRINT*>(patch_indirect_addr));
      }
    }
  }

  return;
}

static auto save_before_handling (INS ins) -> void
{
  static_assert(std::is_same<
                decltype(save_register<WRITE>), VOID (const CONTEXT*, UINT32)
                >::value, "invalid callback function type");

  auto ins_address = INS_Address(ins);
  ASSERTX(!cached_ins_at_addr[ins_address]->is_special);

  INS_InsertCall(ins,
                 IPOINT_BEFORE,
                 reinterpret_cast<AFUNPTR>(save_register<WRITE>),
                 IARG_CONST_CONTEXT,
                 IARG_THREAD_ID,
                 IARG_END);

  static_assert(std::is_same<
                decltype(save_memory<WRITE>), VOID (ADDRINT, UINT32, UINT32)
                >::value, "invalid callback function type");

  INS_InsertCall(ins,
                 IPOINT_BEFORE,
                 reinterpret_cast<AFUNPTR>(save_memory<WRITE>),
                 IARG_ADDRINT, 0,
                 IARG_UINT32, 0,
                 IARG_THREAD_ID,
                 IARG_END);
  return;
}


static auto update_condition_before_handling (INS ins) -> void
{
  static_assert(std::is_same<
                decltype(update_condition<ANY_TO_DISABLE>), VOID (ADDRINT, UINT32)
                >::value, "invalid callback function type");

  INS_InsertCall(ins,
                 IPOINT_BEFORE,
                 reinterpret_cast<AFUNPTR>(update_condition<ANY_TO_DISABLE>),
                 IARG_INST_PTR,
                 IARG_THREAD_ID,
                 IARG_END);

  INS_InsertCall(ins,
                 IPOINT_BEFORE,
                 reinterpret_cast<AFUNPTR>(update_condition<ANY_TO_TERMINATE>),
                 IARG_INST_PTR,
                 IARG_THREAD_ID,
                 IARG_END);

  INS_InsertCall(ins,
                 IPOINT_BEFORE,
                 reinterpret_cast<AFUNPTR>(update_condition<ENABLE_TO_SUSPEND>),
                 IARG_INST_PTR,
                 IARG_THREAD_ID,
                 IARG_END);

  INS_InsertCall(ins,
                 IPOINT_BEFORE,
                 reinterpret_cast<AFUNPTR>(update_condition<NOT_START_TO_ENABLE>),
                 IARG_INST_PTR,
                 IARG_THREAD_ID,
                 IARG_END);

  INS_InsertCall(ins,
                 IPOINT_BEFORE,
                 reinterpret_cast<AFUNPTR>(update_condition<SUSPEND_TO_ENABLE>),
                 IARG_INST_PTR,
                 IARG_THREAD_ID,
                 IARG_END);
  return;
}


static auto insert_ins_get_info_callbacks (INS ins) -> void
{
  auto ins_addr = INS_Address(ins);

  /*
   * Update the code cache if a new instruction found (be careful for self-modifying code)
   */
  if (cached_ins_at_addr.find(ins_addr) == cached_ins_at_addr.end()) {
    cached_ins_at_addr[ins_addr] = std::make_shared<instruction>(ins);
  }

  /*
   * current instruction.
   */
  auto current_ins = cached_ins_at_addr[ins_addr];


  if (some_thread_is_started) {
    /*
     *  we must always verify whether there is a new execution thread or not.
     */
    static_assert(std::is_same<
                  decltype(update_condition<NEW_THREAD>), VOID (ADDRINT, THREADID)
                  >::value, "invalid callback function type");

    INS_InsertCall(ins,
                   IPOINT_BEFORE,
                   reinterpret_cast<AFUNPTR>(update_condition<NEW_THREAD>),
                   IARG_INST_PTR,
                   IARG_THREAD_ID,
                   IARG_END);

    /*
     * The write memory addresses/registers of the PREVIOUS instruction are collected in the following callback analysis
     * functions.
     *
     * We note that these functions capture information of normal instructions. The syscalls need some more special
     * treatment because the read/write memory addresses/registers cannot be determined statically.
     */

    if (some_thread_is_not_suspended || some_thread_is_selective_suspended) {
      // update information of the PREVIOUS instruction (i.e. write registers, memory addresses)
      if (!current_ins->is_special) {
        save_before_handling(ins);
      }
    }

    /*
     * Add the PREVIOUS instruction into the trace.
     */
    if (some_thread_is_not_suspended || some_thread_is_selective_suspended) {
      static_assert(std::is_same<
                    decltype(add_to_trace), VOID (ADDRINT, UINT32)
                    >::value, "invalid callback function type");

      INS_InsertCall(ins,
                     IPOINT_BEFORE,
                     reinterpret_cast<AFUNPTR>(add_to_trace),
                     IARG_INST_PTR,
                     IARG_THREAD_ID,
                     IARG_END);

    }

    /*
     * The following state update callback functions are CALLED ALWAYS, even when all threads are suspended. These
     * functions need to detect if there is some thread goes out of the suspended state.
     */
    update_condition_before_handling(ins);

    /*
     * The following callback function is called only if there is some non-suspended state, if all states are suspended
     * then the skip addresses are not interesting.
     */

    if (some_thread_is_not_suspended) {

      auto current_ins_addr = current_ins->address;

      if ((std::find(
            std::begin(selective_skip_call_addresses), std::end(selective_skip_call_addresses), current_ins_addr
             ) != std::end(selective_skip_call_addresses))
          ||
          (std::find(
             std::begin(full_skip_call_addresses), std::end(full_skip_call_addresses), current_ins_addr
             ) != std::end(full_skip_call_addresses))
          ) {
//        ASSERTX((current_ins->is_call || current_ins->is_branch) &&
//                "the instruction at the skip address must be a call or branch");


        static_assert(std::is_same<
                      decltype(update_resume_address), VOID (ADDRINT, UINT32)
                      >::value, "invalid callback function type");

        INS_InsertCall(ins,
                       IPOINT_BEFORE,
                       reinterpret_cast<AFUNPTR>(update_resume_address),
                       IARG_ADDRINT, current_ins->next_address,
                       IARG_THREAD_ID,
                       IARG_END);
      }
    } // end of if (some_thread_is_not_suspended)

    if (some_thread_is_not_suspended || some_thread_is_selective_suspended) {
      INS_InsertCall(ins,
                     IPOINT_BEFORE,
                     reinterpret_cast<AFUNPTR>(remove_previous_instruction),
                     IARG_THREAD_ID,
                     IARG_END);
    }

    /*
     * The state is updated previously (before capturing instruction's information). Now we will verify if the state
     * leads to a reinstrumentation or not. We note that if the following callback function restarts the instrumentation,
     * then the callback functions after it may not be called. In general, the instrumentation will restart from
     * the beginning of this intrumentation function.
     *
     * We note that this callback function will change value of the identifier "some_thread_is_not_suspended", and it
     * explicitly makes callback functions after it be called or not
     */

    static_assert(std::is_same<
                  decltype(reinstrument_because_of_suspended_state), VOID (const CONTEXT*, ADDRINT)
                  >::value, "invalid callback function type");

    // ATTENTION: cette fonction pourra changer l'instrumentation!!!!
    // if (current_ins->is_special) {
      INS_InsertCall(ins,
                     IPOINT_BEFORE,
                     reinterpret_cast<AFUNPTR>(reinstrument_because_of_suspended_state),
                     IARG_CONST_CONTEXT,
                     IARG_INST_PTR,
                     IARG_END);
    // }


    /*
     * The function ABOVE will update the suspended state (which is true if there is some non-suspended thread, and
     * false if all threads are suspended), and restart the instrumentation only if the suspended state changes.
     *
     * Now if the following callback functions are called, then that means the instruction should be captured. The
     * following function will capture information of the current instruction.
     */

    if (some_thread_is_not_suspended || some_thread_is_selective_suspended) {

      // initialize and save information of the CURRENT instruction

      static_assert(std::is_same<
                    decltype(initialize_instruction), VOID (ADDRINT, UINT32)
                    >::value, "invalid callback function type");

      INS_InsertCall(ins,                                               // instrumented instruction
                     IPOINT_BEFORE,                                     // instrumentation point
                     reinterpret_cast<AFUNPTR>(initialize_instruction), // callback analysis function
                     IARG_INST_PTR,                                     // instruction address
                     IARG_THREAD_ID,                                    // thread id
                     IARG_END);

#if defined(FAST_TRACING)
#else
      if (current_ins->is_call) {

        static_assert(std::is_same<
                      decltype(save_call_concrete_info), VOID (ADDRINT, UINT32)
                      >::value, "invalid callback function type");

        INS_InsertCall(ins,
                       IPOINT_BEFORE,
                       reinterpret_cast<AFUNPTR>(save_call_concrete_info),
                       IARG_BRANCH_TARGET_ADDR,
                       IARG_THREAD_ID,
                       IARG_END);
      }

      if (!current_ins->src_registers.empty() && !current_ins->is_special) {

        static_assert(std::is_same<
                      decltype(save_register<READ>), VOID (const CONTEXT*, UINT32)
                      >::value, "invalid callback function type");

        INS_InsertCall(ins,                                             // instrumented instruction
                       IPOINT_BEFORE,                                   // instrumentation point
                       reinterpret_cast<AFUNPTR>(save_register<READ>),  // callback analysis function
                       IARG_CONST_CONTEXT,                              // context of CPU,
                       IARG_THREAD_ID,                                  // thread id
                       IARG_END);
      }

      if (current_ins->is_memory_read && !current_ins->is_special) {

        static_assert(std::is_same<
                      decltype(save_memory<READ>), VOID (ADDRINT, UINT32, UINT32)
                      >::value, "invalid callback function type");

        INS_InsertCall(ins,                                             // instrumented instruction
                       IPOINT_BEFORE,                                   // instrumentation point
                       reinterpret_cast<AFUNPTR>(save_memory<READ>),    // callback analysis function (read)
                       IARG_MEMORYREAD_EA, IARG_MEMORYREAD_SIZE,        // memory read (address, size)
                       IARG_THREAD_ID,                                  // thread id
                       IARG_END);
      }

      if (current_ins->has_memory_read_2 && !current_ins->is_special) {

        INS_InsertCall(ins,                                             // instrumented instruction
                       IPOINT_BEFORE,                                   // instrumentation point
                       reinterpret_cast<AFUNPTR>(save_memory<READ>),    // callback analysis function (read)
                       IARG_MEMORYREAD2_EA, IARG_MEMORYREAD_SIZE,       // memory read (address, size)
                       IARG_THREAD_ID,                                  // thread id
                       IARG_END);
      }

      if (current_ins->is_memory_write && !current_ins->is_special) {

        INS_InsertCall(ins,                                             // instrumented instruction
                       IPOINT_BEFORE,                                   // instrumentation point
                       reinterpret_cast<AFUNPTR>(save_memory<WRITE>),   // callback analysis function (write)
                       IARG_MEMORYWRITE_EA, IARG_MEMORYWRITE_SIZE,      // memory written (address, size)
                       IARG_THREAD_ID,                                  // thread id
                       IARG_END);
      }
#endif
    }
  }
  else { // !some_thread_is_started

    /*
     * The following callback functions will restart the instrumentation if the next executed instruction has is the
     * start instruction. They are also the only analysis functions called when the start instruction is not executed.
     *
     * We DO NOT NEED to give too much attention at these functions, because they are never re-executed. The value
     * of the identified some_thread_is_started is changed only one time.
     */

    if (!current_ins->is_special) {
      if (current_ins->is_call || current_ins->is_branch) {
        static_assert(std::is_same<
                      decltype(reinstrument_if_some_thread_started), VOID (ADDRINT, ADDRINT, const CONTEXT*)
                      >::value, "invalid callback function type");

        INS_InsertCall(ins,
                       IPOINT_BEFORE,
                       reinterpret_cast<AFUNPTR>(reinstrument_if_some_thread_started),
                       IARG_INST_PTR,
                       IARG_BRANCH_TARGET_ADDR,
                       IARG_CONST_CONTEXT,
                       IARG_END);
      }
      else {
        if (current_ins->is_ret) {
          INS_InsertCall(ins,
                         IPOINT_BEFORE,
                         reinterpret_cast<AFUNPTR>(reinstrument_if_some_thread_started),
                         IARG_INST_PTR,
                         IARG_REG_VALUE, REG_STACK_PTR,
                         IARG_CONST_CONTEXT,
                         IARG_END);
        }
        else {
          static_assert(std::is_same<
                        decltype(reinstrument_if_some_thread_started), VOID (ADDRINT, ADDRINT, const CONTEXT*)
                        >::value, "invalid callback function type");

          INS_InsertCall(ins,
                         IPOINT_BEFORE,
                         reinterpret_cast<AFUNPTR>(reinstrument_if_some_thread_started),
                         IARG_INST_PTR,
                         IARG_ADDRINT, current_ins->next_address,
                         IARG_CONST_CONTEXT,
                         IARG_END);
        }
      }
    }
  }

  return;
}


template<typename T>
auto point_is_patchable(T patch_info_at_addr) -> bool
{
  return std::any_of(std::begin(patch_info_at_addr), std::end(patch_info_at_addr), [](typename T::const_reference patch_info)
  {
    auto patch_exec_point = std::get<0>(patch_info);
    auto exec_point       = std::get<0>(patch_exec_point);
    auto exec_addr        = std::get<0>(exec_point);
    auto exec_order       = std::get<1>(exec_point);

    return (exec_order >= execution_order_of_address[exec_addr]);
  });
}

static auto insert_ins_patch_info_callbacks (INS ins) -> void
{
  static auto register_is_patchable = true;
  static auto memory_is_patchable = true;
  static auto indirect_memory_is_patchable = true;

  if (register_is_patchable || memory_is_patchable || indirect_memory_is_patchable) {
    auto ins_addr = INS_Address(ins);

    if (execution_order_of_address.find(ins_addr) != execution_order_of_address.end()) {

      static_assert(std::is_same<
                    decltype(update_execution_order), VOID (ADDRINT, UINT32)
                    >::value, "invalid callback function type");

      INS_InsertCall(ins,                                               // instrumented instruction
                     IPOINT_BEFORE,                                     // instrumentation point
                     reinterpret_cast<AFUNPTR>(update_execution_order), // callback analysis function
                     IARG_INST_PTR,                                     // instruction address
                     IARG_THREAD_ID,                                    // thread id
                     IARG_END);

      if (register_is_patchable) {
        for (auto const& patch_reg_info : patched_register_at_address) {
          auto patch_exec_point = std::get<0>(patch_reg_info);

          auto patch_reg_value_info = std::get<1>(patch_reg_info);
          auto patch_reg = std::get<0>(patch_reg_value_info);

          auto exec_point = std::get<0>(patch_exec_point);
          auto exec_addr = std::get<0>(exec_point);
          auto exec_order = std::get<1>(exec_point);

          if ((exec_addr == ins_addr) && (exec_order >= execution_order_of_address[ins_addr])) {
            auto patch_point = std::get<1>(patch_exec_point);
            auto pin_patch_point = !patch_point ? IPOINT_BEFORE : IPOINT_AFTER;

//            if (!cached_ins_at_addr[ins_addr]->has_fall_through) {
//              pin_patch_point = IPOINT_BEFORE;
//              ins = INS_Next(ins);
//            }

            auto reg_size = REG_Size(patch_reg);

            ASSERTX(((reg_size == 1) || (reg_size == 2) || (reg_size == 4) || (reg_size == 8)) &&
                   "the needed to patch register has a unsupported length");

            if (INS_Valid(ins)) {

              static_assert(std::is_same<
                            decltype(patch_register), VOID (ADDRINT, bool, UINT32, PIN_REGISTER*, UINT32)
                            >::value, "invalid callback function type");

              INS_InsertCall(ins,                                       // instrumented instruction
                             pin_patch_point,                           // instrumentation point
                             reinterpret_cast<AFUNPTR>(patch_register), // callback analysis function
                             IARG_INST_PTR,                             // instruction address
                             IARG_BOOL, patch_point,                    // patch point (before or after)
                             IARG_UINT32, patch_reg,
                             IARG_REG_REFERENCE, patch_reg,             // patched register (reference)
                             IARG_THREAD_ID,                            // thread id
                             IARG_END);
            }
          }
        }
      }

      if (memory_is_patchable) {
        for (auto const& patch_mem_info : patched_memory_at_address) {

          auto patch_exec_point = std::get<0>(patch_mem_info);
          auto exec_point       = std::get<0>(patch_exec_point);
          auto exec_addr        = std::get<0>(exec_point);
          auto exec_order       = std::get<1>(exec_point);

          if ((exec_addr == ins_addr) && (exec_order >= execution_order_of_address[ins_addr])) {
            auto patch_point = std::get<1>(patch_exec_point);
            auto pin_patch_point = !patch_point ? IPOINT_BEFORE : IPOINT_AFTER;

            auto patch_mem_val = std::get<1>(patch_mem_info);
            auto patch_mem_addr = std::get<0>(patch_mem_val);

            static_assert(std::is_same<
                          decltype(patch_memory), VOID (ADDRINT, bool, ADDRINT, UINT32)
                          >::value, "invalid callback function type");

            INS_InsertCall(ins,                                       // instrumented instruction
                           pin_patch_point,                           // instrumentation point
                           reinterpret_cast<AFUNPTR>(patch_memory),   // callback analysis function
                           IARG_INST_PTR,                             // instruction address
                           IARG_BOOL, patch_point,                    // patch point (before or after)
                           IARG_ADDRINT, patch_mem_addr,
                           IARG_THREAD_ID,                            // thread id
                           IARG_END);
          }
        }
      }

      if (indirect_memory_is_patchable) {
        for (auto const& patch_indirect_mem_info : patched_indirect_memory_at_address) {

          auto patch_exec_point = std::get<0>(patch_indirect_mem_info);
          auto exec_point       = std::get<0>(patch_exec_point);
          auto exec_addr        = std::get<0>(exec_point);
          auto exec_order       = std::get<1>(exec_point);

          if ((exec_addr == ins_addr) && (exec_order >= execution_order_of_address[ins_addr])) {
            auto patch_point = std::get<1>(patch_exec_point);
            auto pin_patch_point = !patch_point ? IPOINT_BEFORE : IPOINT_AFTER;

            auto patch_value_info = std::get<1>(patch_indirect_mem_info);
            auto patch_indirect_reg = std::get<0>(patch_value_info);

            static_assert(std::is_same<
                          decltype(patch_indirect_memory), VOID (ADDRINT, bool, UINT32, ADDRINT, ADDRINT)
                          >::value, "invalid callback function type");

            INS_InsertCall(ins,
                           pin_patch_point,
                           reinterpret_cast<AFUNPTR>(patch_indirect_memory),
                           IARG_INST_PTR,
                           IARG_BOOL, patch_point,
                           IARG_UINT32, patch_indirect_reg,
                           IARG_REG_VALUE, patch_indirect_reg,
                           IARG_THREAD_ID,
                           IARG_END);
          }
        }
      }
    }

//    register_is_patchable = std::any_of(patched_register_at_address.begin(), patched_register_at_address.end(),
//                                     [&](decltype(patched_register_at_address)::const_reference patch_reg_info)
//    {
//      auto patch_exec_point = std::get<0>(patch_reg_info);
//      auto exec_point       = std::get<0>(patch_exec_point);
//      auto exec_addr        = std::get<0>(exec_point);
//      auto exec_order       = std::get<1>(exec_point);

//      return (exec_order >= execution_order_of_address[exec_addr]);
//    });
    register_is_patchable = point_is_patchable<decltype(patched_register_at_address)>(patched_register_at_address);

//    memory_is_patchable = std::any_of(patched_memory_at_address.begin(), patched_memory_at_address.end(),
//                                   [&](decltype(patched_memory_at_address)::const_reference patch_mem_info)
//    {
//      auto patch_exec_point = std::get<0>(patch_mem_info);
//      auto exec_point       = std::get<0>(patch_exec_point);
//      auto exec_addr        = std::get<0>(exec_point);
//      auto exec_order       = std::get<1>(exec_point);

//      return (exec_order >= execution_order_of_address[exec_addr]);
//    });
    memory_is_patchable = point_is_patchable<decltype(patched_memory_at_address)>(patched_memory_at_address);

//    indirect_memory_is_patchable = std::any_of(patched_indirect_memory_at_address.begin(),
//                                               patched_indirect_memory_at_address.end(),
//                                               [&](decltype(patched_indirect_memory_at_address)::const_reference patch_indirect_mem_info)
//    {
//      auto patch_exec_point = std::get<0>(patch_indirect_mem_info);
//      auto exec_point = std::get<0>(patch_exec_point);
//      auto exec_addr = std::get<>
//    });
    indirect_memory_is_patchable = point_is_patchable<decltype(patched_indirect_memory_at_address)>(patched_indirect_memory_at_address);
  }
  return;
}


template<uint32_t sys_id>
auto update_syscall_entry_info (dyn_ins_t& instruction) -> void
{
  return;
}

template<>
auto update_syscall_entry_info<CAP_SYS_OPEN> (dyn_ins_t& instruction) -> void
{
  auto syscall_open_info = sys_open_info_t{};

  // path name
  auto pathname_c_str = reinterpret_cast<char*>(std::get<SYSCALL_ARG_0>(current_syscall_info));
  syscall_open_info.path_name = std::string(pathname_c_str);

  // flags
  auto flags = static_cast<int>(std::get<SYSCALL_ARG_1>(current_syscall_info));
  syscall_open_info.flags = flags;

  // mode
  auto mode = static_cast<int>(std::get<SYSCALL_ARG_2>(current_syscall_info));
  syscall_open_info.mode = mode;

  std::get<INS_CONCRETE_INFO>(instruction) = syscall_open_info;

  return;
}

template<>
auto update_syscall_entry_info<CAP_SYS_READ> (dyn_ins_t& instruction) -> void/*concrete_info_t*/
{
  auto syscall_read_info = sys_read_info_t{};

  // file descriptor
  auto file_desc = static_cast<int>(std::get<SYSCALL_ARG_0>(current_syscall_info));
  syscall_read_info.file_desc = file_desc;

  // read buffer address
  auto buf_addr = std::get<SYSCALL_ARG_1>(current_syscall_info);
  syscall_read_info.buffer_addr = buf_addr;

  // required read length
  auto buf_length = static_cast<size_t>(std::get<SYSCALL_ARG_2>(current_syscall_info));
  syscall_read_info.buffer_length = buf_length;

//  auto buf = std::make_shared<uint8_t>(buf_length);
//  std::copy(buf.get(), buf.get() + buf_length, reinterpret_cast<uint8_t*>(buf_addr));
//  syscall_read_info.buffer = buf;

  std::get<INS_CONCRETE_INFO>(instruction) = syscall_read_info;

  return;
}

template<>
auto update_syscall_entry_info<CAP_SYS_WRITE> (dyn_ins_t& instruction) -> void
{
  auto syscall_write_info = sys_write_info_t{};

  // file descriptor
  auto file_desc = static_cast<int>(std::get<SYSCALL_ARG_0>(current_syscall_info));
  syscall_write_info.file_desc = file_desc;

  // write buffer address
  auto buf_addr = std::get<SYSCALL_ARG_1>(current_syscall_info);
  syscall_write_info.buffer_addr = buf_addr;

  // required write length
  auto buf_length = static_cast<size_t>(std::get<SYSCALL_ARG_2>(current_syscall_info));
  syscall_write_info.buffer_length = buf_length;

  auto buf = std::shared_ptr<uint8_t>(new uint8_t[buf_length], std::default_delete<uint8_t[]>());
//  //  PIN_SafeCopy(buf.get(), reinterpret_cast<uint8_t*>(buf_addr), buf_length);
  auto excp_info = EXCEPTION_INFO();
  auto copied_buf_length = PIN_SafeCopyEx(buf.get(), reinterpret_cast<uint8_t*>(buf_addr), buf_length, &excp_info);
  if (copied_buf_length != buf_length) {
//    // auto excp_code = PIN_GetExceptionCode(&excp_info);
    tfm::format(std::cerr, "error: %s\n", PIN_ExceptionToString(&excp_info));
  }
  syscall_write_info.buffer = buf;

  std::get<INS_CONCRETE_INFO>(instruction) = syscall_write_info;

  return;
}

template<>
auto update_syscall_entry_info<CAP_SYS_OTHER> (dyn_ins_t& instruction) -> void
{
  std::get<INS_CONCRETE_INFO>(instruction) = sys_other_info_t(std::get<SYSCALL_ID>(current_syscall_info));
  return;
}


static auto save_syscall_entry_info (THREADID thread_id, CONTEXT* p_context, SYSCALL_STANDARD syscall_std, VOID* data) -> VOID
{
  if (some_thread_is_started &&
      (some_thread_is_not_suspended || some_thread_is_selective_suspended)) {

    ASSERTX(state_of_thread.find(thread_id) != state_of_thread.end());

    if ((state_of_thread[thread_id] == ENABLED) ||
        (state_of_thread[thread_id] == SELECTIVE_SUSPENDED)) {
      auto ins_addr = PIN_GetContextReg(p_context, REG_INST_PTR);

      ASSERTX(ins_addr == std::get<INS_ADDRESS>(ins_at_thread[thread_id]));
      ASSERTX(cached_ins_at_addr[ins_addr]->is_syscall);

      std::get<SYSCALL_ID>(current_syscall_info) = PIN_GetSyscallNumber(p_context, syscall_std);
      std::get<SYSCALL_ARG_0>(current_syscall_info) = PIN_GetSyscallArgument(p_context, syscall_std, 0);
      std::get<SYSCALL_ARG_1>(current_syscall_info) = PIN_GetSyscallArgument(p_context, syscall_std, 1);
      std::get<SYSCALL_ARG_2>(current_syscall_info) = PIN_GetSyscallArgument(p_context, syscall_std, 2);
      std::get<SYSCALL_ARG_3>(current_syscall_info) = PIN_GetSyscallArgument(p_context, syscall_std, 3);

      switch (std::get<SYSCALL_ID>(current_syscall_info))
      {
      case CAP_SYS_OPEN:
        update_syscall_entry_info<CAP_SYS_OPEN>(ins_at_thread[thread_id]);
        break;

      case CAP_SYS_READ:
        update_syscall_entry_info<CAP_SYS_READ>(ins_at_thread[thread_id]);
        break;

      case CAP_SYS_WRITE:
        update_syscall_entry_info<CAP_SYS_WRITE>(ins_at_thread[thread_id]);
        break;

      default:
        update_syscall_entry_info<CAP_SYS_OTHER>(ins_at_thread[thread_id]);
        break;
      }
    }
  }

  return;
}


template<uint32_t sys_id>
auto get_syscall_exit_concret_info (dyn_ins_t& instruction) -> void
{
  return;
}

template<>
auto get_syscall_exit_concret_info<CAP_SYS_OPEN> (dyn_ins_t& instruction) -> void
{
  ASSERTX(std::get<INS_CONCRETE_INFO>(instruction).which() == 0);

  auto & current_ins_sys_open = boost::get<sys_open_info_t>(std::get<INS_CONCRETE_INFO>(instruction));

  // update file descriptor
  current_ins_sys_open.file_desc = static_cast<int>(std::get<SYSCALL_RET>(current_syscall_info));

  return;
}

template<>
auto get_syscall_exit_concret_info<CAP_SYS_READ> (dyn_ins_t& instruction) -> void
{
  ASSERTX(std::get<INS_CONCRETE_INFO>(instruction).which() == 1);

  auto & current_ins_sys_read = boost::get<sys_read_info_t>(std::get<INS_CONCRETE_INFO>(instruction));

  // length and address of sys_read buffer
  auto buf_length = current_ins_sys_read.buffer_length;
  auto buf_addr = current_ins_sys_read.buffer_addr;

  // create a buffer to store the read buffer
  auto buf = std::shared_ptr<uint8_t>(new uint8_t[buf_length], std::default_delete<uint8_t[]>());

  // read data into buf
  auto excp_info = EXCEPTION_INFO();
  auto copied_buf_length = PIN_SafeCopyEx(buf.get(), reinterpret_cast<uint8_t*>(buf_addr), buf_length, &excp_info);

  if (copied_buf_length != buf_length) {
    tfm::format(std::cerr, "error: %s\n", PIN_ExceptionToString(&excp_info));
  }

  // update data buffer and effective read length
  current_ins_sys_read.buffer = buf;
  current_ins_sys_read.read_length = std::get<SYSCALL_RET>(current_syscall_info);

  return;
}

template<>
auto get_syscall_exit_concret_info<CAP_SYS_WRITE> (dyn_ins_t& instruction) -> void
{
  ASSERTX(std::get<INS_CONCRETE_INFO>(instruction).which() == 2);

  auto & current_ins_sys_write = boost::get<sys_write_info_t>(std::get<INS_CONCRETE_INFO>(instruction));

  // update effective write length
  current_ins_sys_write.write_length = std::get<SYSCALL_RET>(current_syscall_info);

  return;
}

template<>
auto get_syscall_exit_concret_info<CAP_SYS_OTHER> (dyn_ins_t& instruction) -> void
{
  ASSERTX(std::get<INS_CONCRETE_INFO>(instruction).which() == 3);
  return;
}


static auto save_syscall_exit_concret_info (THREADID thread_id,
                                            CONTEXT* p_context, SYSCALL_STANDARD syscall_std, VOID* data) -> VOID
{
  (void)data;

  if (some_thread_is_started &&
      (some_thread_is_not_suspended || some_thread_is_selective_suspended)) {

    ASSERTX(state_of_thread.find(thread_id) != state_of_thread.end());

    if ((state_of_thread[thread_id] == ENABLED) ||
        (state_of_thread[thread_id] == SELECTIVE_SUSPENDED)) {

      ASSERTX(cached_ins_at_addr[
             std::get<INS_ADDRESS>(ins_at_thread[thread_id]
                                   )]->is_syscall);

      auto type_idx = std::get<INS_CONCRETE_INFO>(ins_at_thread[thread_id]).which();
      ASSERTX((type_idx == 0) || (type_idx == 1) || (type_idx == 2) || (type_idx == 3));

      // get return value
      std::get<SYSCALL_RET>(current_syscall_info) = PIN_GetSyscallReturn(p_context, syscall_std);

      switch (type_idx)
      {
      case 0: /* SYS_OPEN */
        get_syscall_exit_concret_info<CAP_SYS_OPEN>(ins_at_thread[thread_id]);
        break;

      case 1: /* SYS_READ */
        get_syscall_exit_concret_info<CAP_SYS_READ>(ins_at_thread[thread_id]);
        break;

      case 2: /* SYS_WRITE */
        get_syscall_exit_concret_info<CAP_SYS_WRITE>(ins_at_thread[thread_id]);
        break;

      case 3: /* SYS_OTHER */
        get_syscall_exit_concret_info<CAP_SYS_OTHER>(ins_at_thread[thread_id]);
        break;
      }
    }
  }
  return;
}


/*====================================================================================================================*/
/*                                                   exported functions                                               */
/*====================================================================================================================*/


static auto ins_mode_get_ins_info (INS ins, VOID* data) -> VOID
{
  (void)data;

  insert_ins_get_info_callbacks(ins);
  return;
}


static auto trace_mode_get_ins_info (TRACE trace, VOID* data) -> VOID
{
  (void)data;

  for (auto bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    for (auto ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
      insert_ins_get_info_callbacks(ins);
    }
  }
  return;
}


static auto ins_mode_patch_ins_info (INS ins, VOID* data) -> VOID
{
  (void)data;
  insert_ins_patch_info_callbacks(ins);
  return;
}


static auto trace_mode_patch_ins_info (TRACE trace, VOID* data) -> VOID
{
  (void)data;

  for (auto bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    for (auto ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
      insert_ins_patch_info_callbacks(ins);
    }
  }
  return;
}


static auto img_mode_get_ins_info (IMG img, VOID* data) -> VOID
{
  (void)data;
//  if (!some_thread_is_started) {
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
      for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {

        RTN_Open(rtn);
        for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
          auto ins_addr = INS_Address(ins);

          // update the code cache if a new instruction found.
          if (cached_ins_at_addr.find(ins_addr) == cached_ins_at_addr.end()) {

            cached_ins_at_addr[ins_addr] = std::make_shared<instruction>(ins);

            if (INS_IsDirectCall(ins) &&
                (std::find(
                   std::begin(full_skip_call_addresses), std::end(full_skip_call_addresses), ins_addr
                   ) == std::end(full_skip_call_addresses)
                 )) {
              auto called_addr = INS_DirectBranchOrCallTargetAddress(ins);

              if (std::find(
                    std::begin(auto_skip_call_addresses), std::end(auto_skip_call_addresses), called_addr
                    ) != std::end(auto_skip_call_addresses)) {
                tfm::format(std::cerr, "add a full-skip from an auto-skip at 0x%x  %s\n",
                            ins_addr, cached_ins_at_addr[ins_addr]->disassemble);
                full_skip_call_addresses.push_back(ins_addr);
              }
            }
          }
        }
        RTN_Close(rtn);
      }
    }
//  }
    tfm::format(std::cerr, "code cache size: %7d instructions processed\n", cached_ins_at_addr.size());

  return;
}

auto cap_initialize () -> void
{
  cached_ins_at_addr.clear();
  resume_address_of_thread.clear();
  trace.clear();

  start_address = 0x0; stop_address = 0x0;
  return;
}

auto cap_initialize_state () -> void
{
//  some_thread_is_started = false;
  some_thread_is_started = (start_address == 0x0);

  some_thread_is_not_suspended = true;
  some_thread_is_selective_suspended = false;

  return;
}


auto cap_set_start_address (ADDRINT address) -> void
{
  start_address = address;
  return;
}


auto cap_set_stop_address (ADDRINT address) -> void
{
  stop_address = address;
  return;
}


auto cap_add_full_skip_call_address (ADDRINT address) -> void
{
  full_skip_call_addresses.push_back(address);
  return;
}


auto cap_add_selective_skip_address (ADDRINT address) -> void
{
  selective_skip_call_addresses.push_back(address);
  return;
}


auto cap_add_auto_skip_call_addresses (ADDRINT address) -> void
{
  auto_skip_call_addresses.push_back(address);
  return;
}


auto cap_set_loop_count (uint32_t count) -> void
{
  loop_count = count;
  return;
}


auto cap_set_trace_length (uint32_t trace_length) -> void
{
  max_trace_length = trace_length;
  return;
}


auto cap_add_patched_memory_value (ADDRINT ins_address, UINT32 exec_order, bool be_or_af,
                                   ADDRINT mem_address, UINT8 mem_size, ADDRINT mem_value) -> void
{
  auto exec_point         = exec_point_t(ins_address, exec_order);
  auto patched_exec_point = patch_point_t(exec_point, be_or_af);
  auto memory_value       = memory_patch_value_t(mem_address, mem_size, mem_value);
  
  patched_memory_at_address.push_back(std::make_pair(patched_exec_point, memory_value));
  execution_order_of_address[ins_address] = 0;

  return;
}


auto cap_add_patched_register_value (ADDRINT ins_address, UINT32 exec_order, bool be_or_af,
                                     REG reg, UINT8 lo_pos, UINT8 hi_pos, ADDRINT reg_value) -> void
{
  auto exec_point             = exec_point_t(ins_address, exec_order);
  auto patched_exec_point     = patch_point_t(exec_point, be_or_af);
  auto patched_register_value = register_patch_value_t(reg, lo_pos, hi_pos, reg_value);

  patched_register_at_address.push_back(std::make_pair(patched_exec_point, patched_register_value));
  execution_order_of_address[ins_address] = 0;

  return;
}

auto cap_add_patched_indirect_memory_value (ADDRINT ins_address, UINT32 exec_order, bool be_or_af,
                                            REG reg, UINT8 mem_size, ADDRINT mem_value) -> void
{
  auto exec_point = exec_point_t(ins_address, exec_order);
  auto patched_exec_point = patch_point_t(exec_point, be_or_af);
  auto patched_indirect_memory_value = indirect_memory_patch_value_t(reg, mem_size, mem_value);

  patched_indirect_memory_at_address.push_back(std::make_pair(patched_exec_point, patched_indirect_memory_value));
  execution_order_of_address[ins_address] = 0;

  return;
}

auto cap_verify_parameters () -> void
{
  tfm::format(std::cerr, "start address 0x%x\n", start_address);
  tfm::format(std::cerr, "stop address 0x%x\n", stop_address);
  return;
}


ins_instrumentation_t cap_ins_mode_get_ins_info          = ins_mode_get_ins_info;
ins_instrumentation_t cap_patch_instrunction_information = ins_mode_patch_ins_info;
trace_instrumentation_t cap_trace_mode_get_ins_info      = trace_mode_get_ins_info;
trace_instrumentation_t cap_trace_mode_patch_ins_info    = trace_mode_patch_ins_info;
img_instrumentation_t cap_img_mode_get_ins_info          = img_mode_get_ins_info;
SYSCALL_ENTRY_CALLBACK cap_get_syscall_entry_info        = save_syscall_entry_info;
SYSCALL_EXIT_CALLBACK cap_get_syscall_exit_info          = save_syscall_exit_concret_info;
