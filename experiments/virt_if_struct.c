/* Generated by CIL v. 1.7.0 */
/* print_CIL_Input is false */
#include <stdlib.h>

union cmd$node
{
  void *_void_star;
  int _int;
};


enum cmd$op
  {
    cmd_constant_int$result_STA_0$value_LIT_0                  = 84,
    cmd_MinusA_int_int2int$right_STA_0$result_STA_0$left_STA_1 = 83,
    cmd_store_int$left_STA_0$right_STA_1                       = 33,
    cmd_branchIfTrue$expr_STA_0$label_LAB_0                    = 172,
    cmd_load_int$left_STA_0$result_STA_0                       = 162,
    cmd_label$label_LAB_0                                      = 96,
    cmd_Div_int_int2int$right_STA_0$result_STA_0$left_STA_1    = 80,
    cmd_PlusA_int_int2int$right_STA_0$result_STA_0$left_STA_1  = 17,
    cmd_Eq_int_int2int$left_STA_0$result_STA_0$right_STA_1     = 187,
    cmd_goto$label_LAB_0                                       = 2,
    cmd_local$result_STA_0$value_LIT_0                         = 59,
    cmd_return_int$expr_STA_0                                  = 39,
    cmd_formal$result_STA_0$value_LIT_0                        = 141
  };

const unsigned char vcodes[1][348]  =
  {
    {
      cmd_formal$result_STA_0$value_LIT_0,        0,        0,        0,
      0,        cmd_load_int$left_STA_0$result_STA_0,        cmd_local$result_STA_0$value_LIT_0,        4,
      0,        0,        0,        cmd_store_int$left_STA_0$right_STA_1,
      cmd_goto$label_LAB_0,        4,        0,        0,
      0,        cmd_constant_int$result_STA_0$value_LIT_0,        0,        0,
      0,        0,        cmd_local$result_STA_0$value_LIT_0,        4,
      0,        0,        0,        cmd_load_int$left_STA_0$result_STA_0,
      cmd_Eq_int_int2int$left_STA_0$result_STA_0$right_STA_1,        cmd_branchIfTrue$expr_STA_0$label_LAB_0,        9,        0,
      0,        0,        cmd_goto$label_LAB_0,        27,
      0,        0,        0,        cmd_local$result_STA_0$value_LIT_0,
      4,        0,        0,        0,
      cmd_load_int$left_STA_0$result_STA_0,        cmd_constant_int$result_STA_0$value_LIT_0,        1,        0,
      0,        0,        cmd_PlusA_int_int2int$right_STA_0$result_STA_0$left_STA_1,        cmd_local$result_STA_0$value_LIT_0,
      4,        0,        0,        0,
      cmd_store_int$left_STA_0$right_STA_1,        cmd_goto$label_LAB_0,        20,        1,
      0,        0,        cmd_constant_int$result_STA_0$value_LIT_0,        1,
      0,        0,        0,        cmd_local$result_STA_0$value_LIT_0,
      4,        0,        0,        0,
      cmd_load_int$left_STA_0$result_STA_0,        cmd_Eq_int_int2int$left_STA_0$result_STA_0$right_STA_1,        cmd_branchIfTrue$expr_STA_0$label_LAB_0,        9,
      0,        0,        0,        cmd_goto$label_LAB_0,
      27,        0,        0,        0,
      cmd_local$result_STA_0$value_LIT_0,        4,        0,        0,
      0,        cmd_load_int$left_STA_0$result_STA_0,        cmd_constant_int$result_STA_0$value_LIT_0,        3,
      0,        0,        0,        cmd_PlusA_int_int2int$right_STA_0$result_STA_0$left_STA_1,
      cmd_local$result_STA_0$value_LIT_0,        4,        0,        0,
      0,        cmd_store_int$left_STA_0$right_STA_1,        cmd_goto$label_LAB_0,        231,
      0,        0,        0,        cmd_constant_int$result_STA_0$value_LIT_0,
      2,        0,        0,        0,
      cmd_local$result_STA_0$value_LIT_0,        4,        0,        0,
      0,        cmd_load_int$left_STA_0$result_STA_0,        cmd_Eq_int_int2int$left_STA_0$result_STA_0$right_STA_1,        cmd_branchIfTrue$expr_STA_0$label_LAB_0,
      9,        0,        0,        0,
      cmd_goto$label_LAB_0,        27,        0,        0,
      0,        cmd_local$result_STA_0$value_LIT_0,        4,        0,
      0,        0,        cmd_load_int$left_STA_0$result_STA_0,        cmd_constant_int$result_STA_0$value_LIT_0,
      1,        0,        0,        0,
      cmd_MinusA_int_int2int$right_STA_0$result_STA_0$left_STA_1,        cmd_local$result_STA_0$value_LIT_0,        4,        0,
      0,        0,        cmd_store_int$left_STA_0$right_STA_1,        cmd_goto$label_LAB_0,
      186,        0,        0,        0,
      cmd_constant_int$result_STA_0$value_LIT_0,        3,        0,        0,
      0,        cmd_local$result_STA_0$value_LIT_0,        4,        0,
      0,        0,        cmd_load_int$left_STA_0$result_STA_0,        cmd_Eq_int_int2int$left_STA_0$result_STA_0$right_STA_1,
      cmd_branchIfTrue$expr_STA_0$label_LAB_0,        9,        0,        0,
      0,        cmd_goto$label_LAB_0,        27,        0,
      0,        0,        cmd_local$result_STA_0$value_LIT_0,        4,
      0,        0,        0,        cmd_load_int$left_STA_0$result_STA_0,
      cmd_constant_int$result_STA_0$value_LIT_0,        2,        0,        0,
      0,        cmd_Div_int_int2int$right_STA_0$result_STA_0$left_STA_1,        cmd_local$result_STA_0$value_LIT_0,        4,
      0,        0,        0,        cmd_store_int$left_STA_0$right_STA_1,
      cmd_goto$label_LAB_0,        141,        0,        0,
      0,        cmd_constant_int$result_STA_0$value_LIT_0,        4,        0,
      0,        0,        cmd_local$result_STA_0$value_LIT_0,        4,
      0,        0,        0,        cmd_load_int$left_STA_0$result_STA_0,
      cmd_Eq_int_int2int$left_STA_0$result_STA_0$right_STA_1,        cmd_branchIfTrue$expr_STA_0$label_LAB_0,        9,        0,
      0,        0,        cmd_goto$label_LAB_0,        27,
      0,        0,        0,        cmd_local$result_STA_0$value_LIT_0,
      4,        0,        0,        0,
      cmd_load_int$left_STA_0$result_STA_0,        cmd_constant_int$result_STA_0$value_LIT_0,        2,        0,
      0,        0,        cmd_MinusA_int_int2int$right_STA_0$result_STA_0$left_STA_1,        cmd_local$result_STA_0$value_LIT_0,
      4,        0,        0,        0,
      cmd_store_int$left_STA_0$right_STA_1,        cmd_goto$label_LAB_0,        96,        0,
      0,        0,        cmd_constant_int$result_STA_0$value_LIT_0,        5,
      0,        0,        0,        cmd_local$result_STA_0$value_LIT_0,
      4,        0,        0,        0,
      cmd_load_int$left_STA_0$result_STA_0,        cmd_Eq_int_int2int$left_STA_0$result_STA_0$right_STA_1,        cmd_branchIfTrue$expr_STA_0$label_LAB_0,        9,
      0,        0,        0,        cmd_goto$label_LAB_0,
      20,        0,        0,        0,
      cmd_constant_int$result_STA_0$value_LIT_0,        10,        0,        0,
      0,        cmd_local$result_STA_0$value_LIT_0,        4,        0,
      0,        0,        cmd_store_int$left_STA_0$right_STA_1,        cmd_goto$label_LAB_0,
      58,        0,        0,        0,
      cmd_constant_int$result_STA_0$value_LIT_0,        6,        0,        0,
      0,        cmd_local$result_STA_0$value_LIT_0,        4,        0,
      0,        0,        cmd_load_int$left_STA_0$result_STA_0,        cmd_Eq_int_int2int$left_STA_0$result_STA_0$right_STA_1,
      cmd_branchIfTrue$expr_STA_0$label_LAB_0,        9,        0,        0,
      0,        cmd_goto$label_LAB_0,        20,        0,
      0,        0,        cmd_constant_int$result_STA_0$value_LIT_0,        17,
      0,        0,        0,        cmd_local$result_STA_0$value_LIT_0,
      4,        0,        0,        0,
      cmd_store_int$left_STA_0$right_STA_1,        cmd_goto$label_LAB_0,        20,        0,
      0,        0,        cmd_constant_int$result_STA_0$value_LIT_0,        10,
      0,        0,        0,        cmd_local$result_STA_0$value_LIT_0,
      4,        0,        0,        0,
      cmd_store_int$left_STA_0$right_STA_1,        cmd_goto$label_LAB_0,        4,        0,
      0,        0,        cmd_formal$result_STA_0$value_LIT_0,        0,
      0,        0,        0,        cmd_load_int$left_STA_0$result_STA_0,
      cmd_local$result_STA_0$value_LIT_0,        4,        0,        0,
      0,        cmd_load_int$left_STA_0$result_STA_0,        cmd_PlusA_int_int2int$right_STA_0$result_STA_0$left_STA_1,        cmd_return_int$expr_STA_0
    }
  };

int if_struct_obfuscated(int input)
{
  char locals[8];
  union cmd$node vstack[1][32];
  union cmd$node *vsp[1];
  const unsigned char *vpc[1];

  vsp[0] = vstack[0];
  vpc[0] = vcodes[0];

  while (1) {
    switch (*(vpc[0])) {
    case cmd_load_int$left_STA_0$result_STA_0:
      (vpc[0]) ++;
      (vsp[0] + 0)->_int = *((int *)(vsp[0] + 0)->_void_star);
      break;

    case cmd_return_int$expr_STA_0:
      (vpc[0]) ++;
      return ((vsp[0] + 0)->_int);
      break;

    case cmd_label$label_LAB_0:
      (vpc[0]) ++;
      break;

    case cmd_Div_int_int2int$right_STA_0$result_STA_0$left_STA_1:
      (vpc[0]) ++;
      (vsp[0] + -1)->_int = (vsp[0] + -1)->_int / (vsp[0] + 0)->_int;
      (vsp[0]) --;
      break;

    case cmd_MinusA_int_int2int$right_STA_0$result_STA_0$left_STA_1:
      (vpc[0]) ++;
      (vsp[0] + -1)->_int = (vsp[0] + -1)->_int - (vsp[0] + 0)->_int;
      (vsp[0]) --;
      break;

    case cmd_Eq_int_int2int$left_STA_0$result_STA_0$right_STA_1:
      (vpc[0]) ++;
      (vsp[0] + -1)->_int = (vsp[0] + 0)->_int == (vsp[0] + -1)->_int;
      (vsp[0]) --;
      break;

    case cmd_local$result_STA_0$value_LIT_0:
      (vpc[0]) ++;
      (vsp[0] + 1)->_void_star = (void *)(locals + *((int *)vpc[0]));
      (vsp[0]) ++;
      vpc[0] += 4;
      break;

    case cmd_goto$label_LAB_0:
      (vpc[0]) ++;
      vpc[0] += *((int *)vpc[0]);
      break;

    case cmd_store_int$left_STA_0$right_STA_1:
      (vpc[0]) ++;
      *((int *)(vsp[0] + 0)->_void_star) = (vsp[0] + -1)->_int;
      vsp[0] += -2;
      break;

    case cmd_PlusA_int_int2int$right_STA_0$result_STA_0$left_STA_1:
      (vpc[0]) ++;
      (vsp[0] + -1)->_int = (vsp[0] + -1)->_int + (vsp[0] + 0)->_int;
      (vsp[0]) --;
      break;

    case cmd_constant_int$result_STA_0$value_LIT_0:
      (vpc[0]) ++;
      (vsp[0] + 1)->_int = *((int *)vpc[0]);
      (vsp[0]) ++;
      vpc[0] += 4;
      break;

    case cmd_formal$result_STA_0$value_LIT_0:
      (vpc[0]) ++;
      switch (*((int *)vpc[0])) {
      case 0:
        (vsp[0] + 1)->_void_star = (void *)(& input);
        break;
      }
      (vsp[0]) ++;
      vpc[0] += 4;
      break;

    case cmd_branchIfTrue$expr_STA_0$label_LAB_0:
      (vpc[0]) ++;
      if ((vsp[0] + 0)->_int) {
        vpc[0] += *((int *)vpc[0]);
      } else {
        vpc[0] += 4;
      }
      (vsp[0]) --;
      break;
    }
  }
}


int main(int argc , char *argv[])
{
  int input ;

  input = strtoul(argv[1], 0, 0);
  return if_struct_obfuscated(input);
}