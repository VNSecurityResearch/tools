open Dba
open Formula
open Concsymb_policy
open CommonAnalysis
open Trace_type
open TraceAnalysis
open AnalysisEnv
open Formula
open Smtlib2
open Solver
open Smtlib2print
open Basic_structs
open Concsymb_policy
open Random
open Extlib

type examination_t =
    Visited          (* visited *)
  | Covered          (* visited and completely covered *)
  | PartiallyCovered (* visited but partially covered only *)
  | JustCovered      (* visited, covered by solving the predicate at some previous instruction (for dynamic jump) *)
  | Uncoverable      (* visited, tried to cover, but cannot *)

type state_indentifier_t = Memory of int | Register of string

type continuation_t =
  {
    next_location : int64;
    input_value   : int list;
  }

type control_t = ConJump | DynJump

type exe_control_point_t =
  {
    location         : int64;
    history          : int64 list;
    continuations    : continuation_t DynArray.t;
    mutable explored : examination_t;
    control_type     : control_t
  }

module IntSet = Set.Make(
  struct
    let compare = Pervasives.compare
    type t = int
  end
  )

exception NotVisitedContinuationIndex of int
exception AllContinuationVisited

exception ControlPointIndex of int

(* ================================================================================ *)

class exp_policy_c = object(self)

  method concsymb_store (tr_index:int) (addr_occ:int) (e_addr:dbaExpr) (e:dbaExpr) (size:int) (nth_dba:int) (inst:trace_inst): concsymb_action * concsymb_action =
    (* if (Int64.compare (Int64.of_int 0x8048619) inst.location = 0) *)
    (* then (KeepOrSymb(false), Conc) *)
    (* else (KeepOrSymb(false), KeepOrSymb(false)) *)

    (KeepOrSymb(false), KeepOrSymb(false))


  method concsymb_load (tr_index:int) (add_occ:int) (e:dbaExpr) (size:int) (nth_dba:int) (inst:trace_inst) =
    (KeepOrSymb(false), KeepOrSymb(false))

  method concsymb_var (tr_index:int) (addr_occ:int) (name:string) (low:int) (hight:int) (nth_dba:int) (inst:trace_inst) =
    if (name = "ebp") || (name = "esp") (* make concrete action records of all functions *)
    then Conc
    else KeepOrSymb(false)

  method intended_concsymb (tr_index:int) (addr_occ:int) (inst:trace_inst) (nth_dba:int) (env:analysis_env): unit = ()
end;;

(* ============================================================================= *)

let construct_memory_state (base_address:int) (state_entries:int list) initial_state =
  let
    accum_state (state, addr_offset) state_entry =
    let next_state = Addr64Map.add (Int64.add (Int64.of_int base_address) addr_offset) state_entry state
    and next_addr_offset = Int64.add addr_offset 1L
    in (next_state, next_addr_offset)
  in
  (* fold_left : ('a -> 'b -> 'a) -> 'a -> 'b list -> 'a *)
  fst (List.fold_left accum_state (initial_state, 0L) state_entries)

(* ============================================================================= *)

let construct_memory_state_from_file base_address state_entries_filename initial_state =
  let entries =
    try
      let entries_as_string = Std.input_file ~bin:false state_entries_filename in
      let entries_as_strings = Str.split (Str.regexp "[;]") entries_as_string in
      List.map (fun str -> int_of_string str) entries_as_strings
    with
    | Sys_error _ -> []
  in construct_memory_state base_address entries initial_state

(* ============================================================================= *)

(* let find_not_visited_continuation_index control_point = *)
(*   try *)
(*     let idx = ref 0 in *)
(*     DynArray.iter (fun continuation -> *)
(*         if (Int64.to_int continuation.next_location = 0) then raise (NotVisitedContinuationIndex !idx) *)
(*         else idx := !idx + 1) control_point.continuations; *)
(*     None *)
(*   with *)
(*   | NotVisitedContinuationIndex i -> Some i *)

(* ============================================================================= *)

(* let find_last_visited_continuation_index control_point = *)
(*   try *)
(*     let idx = ref 0 in *)
(*     DynArray.iter (fun continuation -> *)
(*         if (Int64.to_int continuation.next_location <> 0) *)
(*         then idx := !idx + 1 *)
(*         else raise (NotVisitedContinuationIndex !idx)) control_point.continuations; *)
(*       Some (!idx - 1) *)
(*   with *)
(*   | NotVisitedContinuationIndex i -> Some i *)

(* ============================================================================= *)

(* let find_visiting_continuation_index control_point = *)
(*   try *)
(*     let idx = ref 0 in *)
(*     DynArray.iter (fun continuation -> *)
(*       if (Int64.to_int continuation.next_location <> 0) then idx := !idx + 1 *)
(*       else raise (NotVisitedContinuationIndex !idx)) control_point.continuations; *)
(*     Some (!idx - 1) *)
(*   with *)
(*   | NotVisitedContinuationIndex i -> if (i = 0) then Some i else Some (i - 1) *)

(* ============================================================================= *)

(* moteur d'execution spécial *)
class explorer_c (trace_filename:string) concolic_policy (input_positions:(int * state_indentifier_t) list) initial_memory_state = object(self)
  inherit trace_analysis trace_filename concolic_policy as super

  val initial_state = initial_memory_state

  val accumulated_ins_locs : (int64 DynArray.t) = DynArray.create ()
  val mutable current_inst_idx = 0

  val visited_control_points : (exe_control_point_t DynArray.t) = DynArray.create ()
  method set_visited_control_points cpoints =
    (
      (DynArray.clear visited_control_points);
      (DynArray.append cpoints visited_control_points)
    )
  method get_visited_control_points = visited_control_points

  val input_points = input_positions
  method get_input_points = input_points

  val mutable input_vars = []
  method get_input_vars = input_vars

  val mutable current_inputs = []
  method set_current_inputs inputs = current_inputs <- inputs
  method get_current_inputs = current_inputs

  (* ================================== visiting methods ================================== *)

  method visit_instr_before (key:int) (inst:trace_inst) (env:analysis_env) = (* of type trace_visit_action *)
    DoExec

  (* ============================================================================= *)

  method visit_instr_after (key:int) (inst:trace_inst) (env:analysis_env) =
    (current_inst_idx <- current_inst_idx + 1);
    (DynArray.add accumulated_ins_locs inst.location);
    DoExec

  (* ============================================================================= *)

  method private find_index_of_current_instruction_in_visited_control_points inst =
    let inst_history = DynArray.to_list accumulated_ins_locs in
    let is_the_same_history hist =
      try
        let history_pair = List.combine hist inst_history in
        not (List.exists (fun pair_elem -> snd pair_elem <> fst pair_elem) history_pair)
      with
      | _ -> false
    in
    let idx = ref 0 in
    try
      DynArray.iter (
        fun cpoint -> if is_the_same_history cpoint.history then raise (ControlPointIndex !idx) else idx := !idx + 1
      ) visited_control_points;
      None
    with
    | ControlPointIndex i -> Some i

  (* ============================================================================= *)

  method private find_index_of_next_instruction_in_visited_control_points (inst:trace_inst) addr_size =
    let inst_history = DynArray.to_list accumulated_ins_locs in
  (* and next_inst_location = Big_int.int64_of_big_int (fst (get_next_address inst.concrete_infos addr_size)) in *)
    let next_inst_history = List.append inst_history [inst.location] in
    let is_the_same_history hist =
      try
        let history_pair = List.combine hist next_inst_history in
        not (List.exists (fun pair_elem -> snd pair_elem <> fst pair_elem) history_pair)
      with
      | _ -> false
    in
    let idx = ref 0 in
    try
      DynArray.iter (
        fun cpoint ->
          if is_the_same_history cpoint.history
          then raise (ControlPointIndex !idx) else idx := !idx + 1
      ) visited_control_points;
      None
    with
    | ControlPointIndex i -> Some i

  (* ============================================================================= *)

  method private get_conditional_jump_new_input_values target_cond target_addr inst (env:analysis_env) =
    let formula_file = "formula_if.smt2"
    and trace_pred = self#build_cond_predicate target_cond env in
    (
      let cond_prop = Big_int.eq_big_int target_addr (fst (get_next_address inst.concrete_infos env.addr_size)) in
      (build_formula env.formula trace_pred ~negate:cond_prop ~initial_state:initial_state ~inline_fun:false formula_file);

      try
        let result, model = solve_z3_model formula_file in
        (
          match result with
          | SAT -> List.map (fun input_var_name -> Big_int.int_of_big_int (fst (get_bitvector_value model input_var_name))) input_vars
          | UNSAT -> []
        )
      with
      | _ -> failwith "parsing smt formula error.\n"
    )

  (* ============================================================================= *)

  method private calculate_conditional_jump_new_continuations target_cond target_addr inst (env:analysis_env) =
    let new_input_values = self#get_conditional_jump_new_input_values target_cond target_addr inst env in
    (
      match new_input_values with
      | [] -> []
      | values -> [{next_location = Int64.of_int 0; input_value = values}]
    )

  (* ============================================================================= *)

  method private add_new_control_point_for_conditional_jump (inst:trace_inst) (dbainst:dbainstr) (env:analysis_env) =
    match snd dbainst with
    | DbaIkIf (cond, NonLocal((address, _), _), offset) ->
      (
        let next_inst_address = Big_int.int64_of_big_int (fst (get_next_address inst.concrete_infos env.addr_size)) in
        let current_continuation = { next_location = next_inst_address; input_value = current_inputs }
        and other_continuations =
          if self#is_symbolic_condition cond env
          then self#calculate_conditional_jump_new_continuations cond address inst env
          else []
        in
        let expl_status = if List.length other_continuations = 0 then Uncoverable else PartiallyCovered in
        let new_cpoint =
          {
            location      = inst.location;
            history       = DynArray.to_list accumulated_ins_locs;
            continuations = DynArray.of_list (current_continuation::other_continuations);
            explored      = expl_status;
            control_type  = ConJump
          }
        in
        (
          let status_str =
            match expl_status with
            | Uncoverable -> "UNCOVERABLE";
            | PartiallyCovered -> "PARTIALLY COVERED"
          in Printf.printf "add new conditional jump at 0x%x with history %d, status %s\n" (Int64.to_int new_cpoint.location) (List.length new_cpoint.history) status_str;
          flush stdout;

          DynArray.add visited_control_points new_cpoint
        )
      )
    | _ -> ()

  (* ============================================================================= *)

  method private get_dynamic_jump_new_input_values target_expr current_target_address_input_value_pair (env:analysis_env) =
    let target_var_name = "jump_address" in
    (
      (self#add_witness_variable target_var_name target_expr env);

      let formula_file = "formula_djump.smt2" in
      let rec get_possible_targets collected_target_addr_input_values_pairs =
        let collected_addrs = fst (List.split collected_target_addr_input_values_pairs) in
        let target_var_diff_from_addr_pred addr =
          SmtNot(self#build_witness_bitvector_comparison_predicate target_var_name env.addr_size addr)
        in
        let local_pred = List.fold_left (* fold_left : ('a -> 'b -> 'a) -> 'a -> 'b list -> 'a *)
            (fun current_pred addr -> (target_var_diff_from_addr_pred addr)::current_pred)
            [] collected_addrs
        in
        let trace_pred = self#build_multiple_condition_predicate local_pred in
        (
          (* Printf.printf "blah\n"; flush stdout; *)
          (build_formula env.formula trace_pred ~negate:false ~initial_state:initial_state ~inline_fun:false formula_file);
          let result, model = solve_z3_model formula_file in
          try
            match result with
            | SAT ->
              (
                let input_values = List.map (fun input_var -> Big_int.int_of_big_int (fst (get_bitvector_value model input_var)))  input_vars
                and target_addr = get_bitvector_value model target_var_name in
                get_possible_targets ((target_addr, input_values)::collected_target_addr_input_values_pairs)
              )
            | UNSAT -> collected_target_addr_input_values_pairs
          with
          | _ ->
            (
              Printf.printf "parsing smt result error.\n"; flush stdout;
              assert false
            )
        )
      in
      (
        (* copied from http://langref.org/fantom+ocaml+java/lists/modification/remove-last *)
        let remove_last lls =
          match (List.rev lls) with
          | h::t -> List.rev t
          | [] -> []
        in
        let all_targets = get_possible_targets [current_target_address_input_value_pair] in
        snd (List.split (remove_last all_targets))
      )
    )

  (* ============================================================================= *)

  method private calculate_dynamic_jump_new_continuations expr inst dbainst (env:analysis_env) =
    if self#is_symbolic_expression expr env then
      let current_target_addr = get_regwrite_value_bv "ecx" inst.concrete_infos env.addr_size in
      let new_input_values = self#get_dynamic_jump_new_input_values expr (current_target_addr, current_inputs) env in
      if List.length new_input_values = 0 then []
      else List.map (
          fun inval -> { next_location = Int64.of_int 0; input_value = inval }
        ) new_input_values
    else []

  (* ============================================================================= *)

  method private add_new_control_point_for_dynamic_jump inst dbainst (env:analysis_env) =
    match snd dbainst with
    | DbaIkDJump _ ->
      (
        let next_inst_address = Big_int.int64_of_big_int (fst (get_next_address inst.concrete_infos env.addr_size)) in
        let current_continuation =
          {
            next_location = next_inst_address;
            input_value   = current_inputs
          }
        in
        let new_cpoint =
          {
            location      = inst.location;
            history       = DynArray.to_list accumulated_ins_locs;
            continuations = DynArray.init 1 (fun _ -> current_continuation);
            explored      = Visited;
            control_type  = DynJump
          }
        in
        (
          Printf.printf "add new dynamic jump at 0x%x with history %d\n" (Int64.to_int new_cpoint.location) (List.length new_cpoint.history);
          flush stdout;

          DynArray.add visited_control_points new_cpoint
        )
      )
    | _ -> ()

  (* ============================================================================= *)

  method private update_continuation_of_control_point_at_index idx inst addr_size =
    let current_cpoint = DynArray.get visited_control_points idx in
    match current_cpoint.explored with
    | Uncoverable | Covered -> ()
    | _ ->
      (
        let continuation_is_updated = ref false in
        let current_continuations = current_cpoint.continuations in
        let new_continuations =
          DynArray.map (
            fun cont ->
              let input_pairs = List.combine cont.input_value current_inputs in
              if not (List.exists (fun elem -> fst elem <> snd elem) input_pairs) then
                if Int64.to_int cont.next_location = 0 then
                  let next_inst_address = Big_int.int64_of_big_int (fst (get_next_address inst.concrete_infos addr_size)) in
                  { next_location = next_inst_address; input_value = current_inputs }
                else cont
              else cont)
            current_continuations
        in
        let explored_status =
          if not (List.exists (fun cont -> Int64.to_int cont.next_location = 0) (DynArray.to_list new_continuations))
          then Covered else PartiallyCovered
        in
        let new_cpoint = { current_cpoint with continuations = new_continuations; explored = explored_status }
        in
        (
          let status_str =
            match new_cpoint.explored with
            | Covered -> "COMPLETELY COVERED"
            | PartiallyCovered -> "PARTIALLY COVERED"
          in Printf.printf "control point at 0x%x with history %d is %s\n" (Int64.to_int new_cpoint.location) (List.length new_cpoint.history) status_str;

          DynArray.set visited_control_points idx new_cpoint
        )
      )

  (* ============================================================================= *)

  method private setup_continuations_of_dynamic_jump_at_index idx expr inst dbainst (env:analysis_env) =
    let current_cpoint = DynArray.get visited_control_points idx in
    match current_cpoint.explored with
    | Visited ->
      (
        let current_continuation = DynArray.get current_cpoint.continuations 0
        and other_continuations = self#calculate_dynamic_jump_new_continuations expr inst dbainst env in
        let all_continuations = DynArray.of_list (current_continuation::other_continuations) in
        let new_cpoint =
          if List.length other_continuations = 0
          then { current_cpoint
                 with continuations = all_continuations; explored = Uncoverable }
          else { current_cpoint
                 with continuations = all_continuations; explored = PartiallyCovered }
        in
        (
          let status_str =
            match new_cpoint.explored with
            | Uncoverable -> "UNCOVERABLE"
            | PartiallyCovered -> "PARTIALLY COVERED"
          in Printf.printf "dynamic jump at 0x%x with history %d is %s\n" (Int64.to_int new_cpoint.location) (List.length new_cpoint.history) status_str;

          DynArray.set visited_control_points idx new_cpoint
        )
      )
    | _ -> ()

  (* ============================================================================= *)

  method visit_dbainstr_before (key:int) (inst:trace_inst) (dbainst:dbainstr) (env:analysis_env) =
    (
      match snd dbainst with
      | DbaIkIf (cond, NonLocal((address, _), _), offset) ->
        (
          match self#find_index_of_current_instruction_in_visited_control_points inst with
          | None ->
            (
              self#add_new_control_point_for_conditional_jump inst dbainst env
            )
          | Some idx ->
            (
              self#update_continuation_of_control_point_at_index idx inst env.addr_size
            )
        )
      | DbaIkDJump _ ->
        (
          match self#find_index_of_current_instruction_in_visited_control_points inst with
          | None ->
            (
              self#add_new_control_point_for_dynamic_jump inst dbainst env
            )
          | Some idx ->
            (
              self#update_continuation_of_control_point_at_index idx inst env.addr_size
            )
        )
      | DbaIkAssign (DbaLhsVar(var, size, tags), expr, offset) ->
        (
          match self#find_index_of_next_instruction_in_visited_control_points inst env.addr_size with
          | None -> ()
          | Some idx ->
            (
              self#setup_continuations_of_dynamic_jump_at_index idx expr inst dbainst env
            )
        )

      | _ -> ()
    );
    DoExec

  (* ============================================================================= *)
  (* mark the input *)
  method visit_dbainstr_after (key:int) (inst:trace_inst) (dbainst:dbainstr) (env:analysis_env) =
    let ins_locs = fst (List.split input_points) in
    if (List.exists (fun loc -> Int64.compare (Int64.of_int loc) inst.location = 0) ins_locs)
    then
      (
        let add_var_char_constraints_into_env var_name upper_bound lower_bound =
          let lt = SmtBvBinary(SmtBvSlt,
                           SmtBvVar(var_name, 32),
                           SmtBvCst(Big_int.big_int_of_int upper_bound, 32))
          and ge = SmtBvBinary(SmtBvSge,
                           SmtBvVar(var_name, 32),
                           SmtBvCst(Big_int.big_int_of_int lower_bound, 32))
          in
          (
            env.formula <- add_constraint env.formula ~comment:(Some "upper bound constraint") (SmtBvExpr(lt));
            env.formula <- add_constraint env.formula ~comment:(Some "lower bound constraint") (SmtBvExpr(ge));
          )
        in
        match (snd dbainst) with
        | DbaIkAssign(lhs, expr, offset) ->
          (
            let var_name = "x_"^(Printf.sprintf "0x%x" (Int64.to_int inst.location)) in
            (
              (* Printf.printf "add input variable %s\n" var_name; flush stdout; *)
              self#add_witness_variable var_name expr env;
              add_var_char_constraints_into_env var_name 127 0;
              input_vars <- input_vars@[var_name];
            )
          )
        | _ -> ()
      )
    else ();
    DoExec
end;;

(* ============================================================================= *)

let generate_option_file exe_filename start_addr stop_addr =
  let option_info = Printf.sprintf "start,0x%x\nstop,0x%x" start_addr stop_addr
  and option_filename = (Filename.basename exe_filename) ^ ".opt"
  in
  (
    (Std.output_file option_filename option_info);
    ignore (Printf.printf "option file %s\n", option_filename);
    option_filename
  )

(* ============================================================================= *)

let generate_config_file exe_filename ins_address before_or_after reg_name reg_value =
  let config_info = (Printf.sprintf "0x%x" ins_address) ^ "," ^ (* location of instruction *)
                    "1," ^                                      (* execution order (always 1 if instruction is outside any loop) *)
                    (Printf.sprintf "%s:0:31" reg_name) ^ "," ^ (* register name *)
                    (Printf.sprintf "0x%x" reg_value) ^ "," ^   (* register value *)
                    (Printf.sprintf "%d" before_or_after)       (* patching point (0 = before, 1 = after) *)
  and config_filename = (Filename.basename exe_filename) ^ ".conf"
  in
  (
    (Std.output_file config_filename config_info);
    config_filename
  )

(* ============================================================================= *)

let generate_config_file exe_filename inputs =
  let config_of_input =
    match inputs with
    | ((ins_addr, Memory mem_addr), mem_value)::_ ->
      fun ((ins_addr, Memory mem_addr), mem_value) -> (Printf.sprintf "0x%x" ins_addr) ^ "," ^      (* location of instruction *)
                                                      "1," ^                                        (* execution order (always 1 if instruction is outside any loop) *)
                                                      (Printf.sprintf "0x%x:%d" mem_addr 1) ^ "," ^ (* memory address *)
                                                      (Printf.sprintf "0x%x" mem_value) ^ "," ^     (* memory value  *)
                                                      "1\n"                                         (* patching point (0 = before, 1 = after) *)
    | ((ins_addr, Register reg_name), reg_value)::_ ->
      fun ((ins_addr, Register reg_name), reg_value) -> (Printf.sprintf "0x%x" ins_addr) ^ "," ^    (* location of instruction *)
                                                        "1," ^                                      (* execution order (always 1 if instruction is outside any loop) *)
                                                        (Printf.sprintf "%s:0:31" reg_name) ^ "," ^ (* register name *)
                                                        (Printf.sprintf "0x%x" reg_value) ^ "," ^   (* register value *)
                                                        "1\n"                                       (* patching point (0 = before, 1 = after) *)
    | _ ->
      (
        Printf.printf "malformed input information\n"; flush stdout;
        assert false
      )
  in
  let config_info = List.fold_left (fun acc_conf input -> acc_conf ^ (config_of_input input)) "" inputs
  and config_filename = (Filename.basename exe_filename) ^ ".conf"
  in
  (
    (* Printf.printf "generate new configuration file\n"; flush stdout; *)
    (Std.output_file config_filename config_info);
    config_filename
  )

(* ============================================================================= *)

let instrument_exe exe_filename option_filename config_filename =
  let trace_filename = (Filename.basename exe_filename ^ ".trace") in
  let instrument_cmd = "./pin67257/ia32/bin/pinbin -t ./pintools/trace-pin/build/vtrace.pin" ^ " -opt " ^ option_filename ^
                       " -conf " ^ config_filename ^ " -out " ^ trace_filename ^ " -- " ^ exe_filename ^ " 57" in
  (
    let inout_channels = Unix.open_process instrument_cmd in
    let exit_status    = Unix.close_process inout_channels in
    match exit_status with
    | Unix.WEXITED exit_code -> Some trace_filename
    | _ -> None
  )

(* ============================================================================= *)

(* exploration strategy *)
let find_next_unexplored_control_point visited_control_points =
  try
    Some
      (
        List.find(fun cpoint -> match cpoint.explored with
            | Visited | PartiallyCovered -> true
            | _ -> false)
          (DynArray.to_list visited_control_points)
      )
  with
  | Not_found -> ( None )

(* ============================================================================= *)

let find_control_point_index cpoint cpoints =
  let i = ref 0
  and found = ref false
  and cpoints_length = (DynArray.length cpoints) in
  (
    while ((not !found) && (!i < cpoints_length)) do
      if ((DynArray.get cpoints !i).location = cpoint.location) &&
         ((List.length (DynArray.get cpoints !i).history) = (List.length cpoint.history))
      then found := true
      else i := !i + 1
    done;
    if !found then Some !i else None
  )

(* ============================================================================= *)

let get_exploration_input visited_cpoints input_number first_time_exploration =
  if (first_time_exploration)
  then
    let i = ref 0 and random_input = ref [] in
    (
      while (!i < input_number) do
        random_input := (Random.int 127)::!random_input;
        i := !i + 1
      done;
      Some (!random_input)
    )
  else
    (
      let not_completely_covered cpoint =
        match cpoint.explored with
        | Uncoverable | Covered -> false
        | _ -> true
      in
      try
        let idx = ref 0 in
        DynArray.iter (
          fun cpoint -> if not_completely_covered cpoint then raise (ControlPointIndex !idx) else idx := !idx + 1
        ) visited_cpoints;
        None
      with
      | ControlPointIndex idx ->
        (
          let cpoint = DynArray.get visited_cpoints idx in
          let cpoint_ctns = DynArray.to_list cpoint.continuations in
          match cpoint.control_type with
          | ConJump ->
            (
              let ctn = List.find (fun ctn -> Int64.to_int ctn.next_location = 0) cpoint_ctns in
              Some ctn.input_value
            )
          | DynJump ->
            (
              match cpoint.explored with
              | Visited -> let ctn = List.hd cpoint_ctns in Some ctn.input_value;
              | _ -> let ctn = List.find (fun ctn -> Int64.to_int ctn.next_location = 0) cpoint_ctns in Some ctn.input_value
            )
        )
    )

(* ============================================================================= *)

let print_exploration_result visited_cpoints =
  DynArray.iter (fun cpoint ->
      match cpoint.explored with
      | Covered ->
        (
          (
            match cpoint.control_type with
            | ConJump -> Printf.printf "conditional jump at 0x%x with history %d is covered by:\n" (Int64.to_int cpoint.location) (List.length cpoint.history)
            | DynJump -> Printf.printf "dynamic jump at 0x%x with history %d is covered by:\n" (Int64.to_int cpoint.location) (List.length cpoint.history)
          );

          DynArray.iter (fun continuation ->
              Printf.printf "next address: 0x%x; " (Int64.to_int continuation.next_location);
              Printf.printf "input value(s): ";
              List.iter (fun value -> Printf.printf "0x%x " value) continuation.input_value;
              Printf.printf "\n"
            ) cpoint.continuations
        )
      | _ -> ()
    ) visited_cpoints

(* ============================================================================= *)

let save_exploration_result_to_file visited_cpoints result_file =
  let list_equal a b =
    try
      let ab = List.combine a b in
      not (List.exists (fun xy -> fst xy <> snd xy) ab)
    with
    | Invalid_argument _ -> false
  in
  let explored_values = ref [] in
  (
    DynArray.iter (fun cpoint ->
      match cpoint.explored with
      | Covered ->
        (
          DynArray.iter (fun continuation ->
              if (List.exists (fun elem -> list_equal elem continuation.input_value) !explored_values)
              then ()
              else explored_values := continuation.input_value::!explored_values) cpoint.continuations
        )
      | _ -> ()
      ) visited_cpoints;

    let string_of_input input = List.fold_left (fun accum_str elem -> accum_str ^ (Printf.sprintf "0x%x;" elem)) "" input in
    let result_string = List.fold_left (fun accum input -> accum ^ string_of_input input ^ "\n") "" !explored_values in
    Std.output_file result_file result_string
  )

(* ============================================================================= *)

let print_current_exploration_result visited_cpoints =
  let covered_cpoint_num = ref 0
  and notcovered_cpoint_num = ref 0
  and uncoverable_cpoint_num = ref 0
  and partially_covered_cpoint_num = ref 0
  in
  (
    DynArray.iter (fun cpoint ->
        match cpoint.explored with
        | Covered -> covered_cpoint_num := !covered_cpoint_num + 1
        | Visited -> notcovered_cpoint_num := !notcovered_cpoint_num + 1
        | PartiallyCovered -> partially_covered_cpoint_num := !partially_covered_cpoint_num + 1
        | Uncoverable -> uncoverable_cpoint_num := !uncoverable_cpoint_num + 1
        | _ -> ()
      ) visited_cpoints;

    Printf.printf "not covered %d, partially covered %d, covered %d, uncoverable %d\n"
      !notcovered_cpoint_num !partially_covered_cpoint_num !covered_cpoint_num !uncoverable_cpoint_num;
    flush stdout
  )

(* ============================================================================= *)

let direct_explore_exe (exe_filename:string) (start_addr:int) (stop_addr:int) (input_points:(int * state_indentifier_t) list) (memory_base_addr:int) (memory_dump_file:string) =
  let visited_cpoints = ref (DynArray.create ())
  and all_explored    = ref false
  and first_time_exploration = ref true
  and option_filename = generate_option_file exe_filename start_addr stop_addr
  and cs_policy       = new exp_policy_c
  and initial_state   = construct_memory_state_from_file memory_base_addr memory_dump_file Addr64Map.empty
  in
  while not !all_explored do
    print_current_exploration_result !visited_cpoints;
    let expl_input = get_exploration_input !visited_cpoints (List.length input_points) !first_time_exploration in
    (
      match expl_input with
      | None ->
        (
          all_explored := true;

          Printf.printf "all control points are covered, stop exploration.\n";
          Printf.printf "===================================================\nexploration results:\n";
          print_exploration_result !visited_cpoints;
          let exploration_result_file = (Filename.basename exe_filename) ^ ".exp" in
          (
            Printf.printf "===================================================\nsave results to: %s\n" exploration_result_file;
            save_exploration_result_to_file !visited_cpoints exploration_result_file;
          );
          flush stdout;
        )

      | Some input_value ->
        (
          let config_filename = generate_config_file exe_filename (List.combine input_points input_value) in
          match (instrument_exe exe_filename option_filename config_filename) with
          | None ->
            (
              failwith "instrumentation error, stop exploration.\n";
            )
          | Some trace_filename ->
            let exp_instance = new explorer_c trace_filename cs_policy input_points initial_state in
            (
              first_time_exploration := false;

              exp_instance#set_visited_control_points !visited_cpoints;
              exp_instance#set_current_inputs input_value;

              exp_instance#compute;

              visited_cpoints := exp_instance#get_visited_control_points
            )
        )
    )
  done
