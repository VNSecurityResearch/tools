
#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "../parsing_helper.h"
#include <pin.H>
extern "C" {
#include "xed-interface.h"
}

#include <string>
#include <memory>
#include <vector>
#include <map>

class instruction
{
 public:
  ADDRINT     address;
  ADDRINT     next_address;
//  std::string opcode;
//  xed_decoded_inst_t* decoded_opcode;
  uint8_t opcode_size;
  std::string disassemble;
  std::string including_image;
  std::string including_routine_name;

  bool has_fall_through;

  bool is_call;
  bool is_branch;
  bool is_syscall;
  bool is_ret;
  bool is_special;

  xed_category_enum_t category;
  xed_iclass_enum_t iclass;

  std::vector<REG> src_registers;
  std::vector<REG> dst_registers;

  bool is_memory_read;
  bool is_memory_write;
  bool has_memory_read_2;

 public:
  instruction(ADDRINT ins_addr, const char* opcode_buffer, int opcode_buffer_size);
};

//typedef std::shared_ptr<instruction> p_instruction_t;
//typedef std::vector<p_instruction_t> p_instructions_t;
//typedef std::map<ADDRINT, p_instruction_t> map_address_instruction_t;
//typedef std::shared_ptr<map_address_instruction_t> p_map_address_instruction_t;
using p_instruction_t             = std::shared_ptr<instruction>;
using p_instructions_t            = std::vector<p_instruction_t>;
using map_address_instruction_t   = std::map<ADDRINT, p_instruction_t>;
using p_map_address_instruction_t = std::shared_ptr<map_address_instruction_t>;

#endif
















