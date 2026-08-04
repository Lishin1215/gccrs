// Copyright (C) 2026 Free Software Foundation, Inc.

// This file is part of GCC.

// GCC is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3, or (at your option) any later
// version.

// GCC is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.

// You should have received a copy of the GNU General Public License
// along with GCC; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

#include "rust-bir-drop-analysis.h"
#include "rust-bir.h"
#include "rust-hir-map.h"

namespace Rust {
namespace BIR {

namespace {

struct InitializationState
{
  explicit InitializationState (size_t place_count)
    : maybe_initialized (place_count, false),
      maybe_uninitialized (place_count, false), reachable (false)
  {}

  std::vector<bool> maybe_initialized;
  std::vector<bool> maybe_uninitialized;
  bool reachable;
};

static bool
is_straight_line (const Function &function)
{
  std::set<BasicBlockId> visited;
  BasicBlockId current = ENTRY_BASIC_BLOCK;

  while (current != INVALID_BB)
    {
      if (!visited.insert (current).second)
	return false;

      const BasicBlock &block = function.basic_blocks[current];

      if (block.successors.empty ())
	return true;

      if (block.successors.size () != 1)
	return false;

      current = block.successors.front ();
    }

  return true;
}

static bool
merge_state (InitializationState &into, const InitializationState &from)
{
  if (!from.reachable)
    return false;

  if (!into.reachable)
    {
      into = from;
      return true;
    }

  bool changed = false;
  for (size_t i = 0; i < into.maybe_initialized.size (); ++i)
    {
      bool maybe_initialized
	= into.maybe_initialized[i] || from.maybe_initialized[i];
      bool maybe_uninitialized
	= into.maybe_uninitialized[i] || from.maybe_uninitialized[i];

      changed |= maybe_initialized != into.maybe_initialized[i];
      changed |= maybe_uninitialized != into.maybe_uninitialized[i];

      into.maybe_initialized[i] = maybe_initialized;
      into.maybe_uninitialized[i] = maybe_uninitialized;
    }

  return changed;
}

static void
set_initialized (InitializationState &state, PlaceId place)
{
  state.maybe_initialized[place.value] = true;
  state.maybe_uninitialized[place.value] = false;
}

static void
set_uninitialized (InitializationState &state, PlaceId place)
{
  state.maybe_initialized[place.value] = false;
  state.maybe_uninitialized[place.value] = true;
}

static void
apply_statement (Function &function, Statement &statement,
		 InitializationState &state)
{
  PlaceId place = statement.get_place ();

  switch (statement.get_kind ())
    {
    case Statement::Kind::STORAGE_LIVE:
      set_uninitialized (state, place);
      break;

    case Statement::Kind::ASSIGNMENT:
      {
	PlaceId lhs = place;
	AbstractExpr &expr = statement.get_expr ();

	if (expr.get_kind () == ExprKind::ASSIGNMENT)
	  {
	    PlaceId rhs = static_cast<Assignment &> (expr).get_rhs ();
	    const Place &rhs_place = function.place_db[rhs];

	    if (rhs_place.kind == Place::VARIABLE
		&& rhs_place.should_be_moved ())
	      set_uninitialized (state, rhs);
	  }

	set_initialized (state, lhs);
	break;
      }

    case Statement::Kind::DROP:
    case Statement::Kind::STORAGE_DEAD:
      set_uninitialized (state, place);
      break;

    case Statement::Kind::SWITCH:
    case Statement::Kind::RETURN:
    case Statement::Kind::GOTO:
    case Statement::Kind::USER_TYPE_ASCRIPTION:
    case Statement::Kind::FAKE_READ:
      break;
    }
}

static Statement::DropKind
classify_drop (const InitializationState &state, PlaceId place)
{
  bool maybe_initialized = state.maybe_initialized[place.value];
  bool maybe_uninitialized = state.maybe_uninitialized[place.value];

  if (maybe_initialized && maybe_uninitialized)
    return Statement::DropKind::CONDITIONAL;

  if (maybe_initialized)
    return Statement::DropKind::STATIC;

  if (maybe_uninitialized)
    return Statement::DropKind::DEAD;

  return Statement::DropKind::UNCLASSIFIED;
}

} // namespace

DropAnalysis &
DropAnalysis::get ()
{
  static DropAnalysis instance;
  return instance;
}

void
DropAnalysis::clear ()
{
  definitely_dead.clear ();
  conditionally_dropped.clear ();
  move_sources.clear ();
}

void
DropAnalysis::analyze (Function &function)
{
  size_t place_count = function.place_db.size ();
  size_t block_count = function.basic_blocks.size ();

  std::vector<InitializationState> entry_states;
  entry_states.reserve (block_count);
  for (size_t i = 0; i < block_count; ++i)
    entry_states.emplace_back (place_count);

  InitializationState &entry_state = entry_states[ENTRY_BASIC_BLOCK.value];
  entry_state.reachable = true;
  for (size_t i = 0; i < place_count; ++i)
    entry_state.maybe_uninitialized[i] = true;

  for (PlaceId argument : function.arguments)
    set_initialized (entry_state, argument);

  std::vector<BasicBlockId> worklist;
  std::vector<bool> queued (block_count, false);
  worklist.push_back (ENTRY_BASIC_BLOCK);
  queued[ENTRY_BASIC_BLOCK.value] = true;

  while (!worklist.empty ())
    {
      BasicBlockId block_id = worklist.back ();
      worklist.pop_back ();
      queued[block_id.value] = false;

      InitializationState state = entry_states[block_id.value];
      BasicBlock &block = function.basic_blocks[block_id];
      for (Statement &statement : block.statements)
	apply_statement (function, statement, state);

      for (BasicBlockId successor : block.successors)
	if (merge_state (entry_states[successor.value], state)
	    && !queued[successor.value])
	  {
	    worklist.push_back (successor);
	    queued[successor.value] = true;
	  }
    }

  bool export_backend_dead = is_straight_line (function);
  std::set<HirId> dead_drop_locals;
  std::set<HirId> non_dead_drop_locals;

  for (size_t i = 0; i < block_count; ++i)
    {
      InitializationState state = entry_states[i];
      if (!state.reachable)
	continue;

      BasicBlockId block_id = {static_cast<uint32_t> (i)};
      BasicBlock &block = function.basic_blocks[block_id];

      for (Statement &statement : block.statements)
	{
	  if (statement.get_kind () == Statement::Kind::ASSIGNMENT
	      && statement.get_move_site () != UNKNOWN_HIRID)
	    {
	      AbstractExpr &expr = statement.get_expr ();
	      if (expr.get_kind () == ExprKind::ASSIGNMENT)
		{
		  PlaceId rhs = static_cast<Assignment &> (expr).get_rhs ();
		  const Place &rhs_place = function.place_db[rhs];
		  if (rhs_place.kind == Place::VARIABLE
		      && rhs_place.should_be_moved ())
		    {
		      auto hirid
			= Analysis::Mappings::get ().lookup_node_to_hir (
			  static_cast<NodeId> (
			    rhs_place.variable_or_field_index));
		      if (hirid.has_value ())
			move_sources[statement.get_move_site ()] = hirid.value ();
		    }
		}
	    }

	  if (statement.get_kind () == Statement::Kind::DROP)
	    {
	      PlaceId place = statement.get_place ();
	      Statement::DropKind drop_kind = classify_drop (state, place);
	      statement.set_drop_kind (drop_kind);

	      if (drop_kind == Statement::DropKind::CONDITIONAL)
		{
		  const Place &dropped_place = function.place_db[place];
		  if (dropped_place.kind == Place::VARIABLE)
		    {
		      auto hirid
			= Analysis::Mappings::get ().lookup_node_to_hir (
			  static_cast<NodeId> (
			    dropped_place.variable_or_field_index));
		      if (hirid.has_value ())
			conditionally_dropped.insert (hirid.value ());
		    }
		}

	      if (export_backend_dead)
		{
		  const Place &dropped_place = function.place_db[place];
		  if (dropped_place.kind == Place::VARIABLE)
		    {
		      auto hirid
			= Analysis::Mappings::get ().lookup_node_to_hir (
			  static_cast<NodeId> (
			    dropped_place.variable_or_field_index));
		      if (hirid.has_value ())
			{
			  if (drop_kind == Statement::DropKind::DEAD)
			    dead_drop_locals.insert (hirid.value ());
			  else
			    non_dead_drop_locals.insert (hirid.value ());
			}
		    }
		}
	    }

	  apply_statement (function, statement, state);
	}
    }

  for (HirId hirid : dead_drop_locals)
    if (non_dead_drop_locals.find (hirid) == non_dead_drop_locals.end ())
	definitely_dead.insert (hirid);
}

bool
DropAnalysis::is_definitely_dead (HirId id) const
{
  return definitely_dead.find (id) != definitely_dead.end ();
}

bool
DropAnalysis::needs_drop_flag (HirId id) const
{
  return conditionally_dropped.find (id) != conditionally_dropped.end ();
}

bool
DropAnalysis::lookup_move_source (HirId move_site, HirId *source) const
{
  auto it = move_sources.find (move_site);
  if (it == move_sources.end ())
    return false;

  *source = it->second;
  return true;
}

} // namespace BIR
} // namespace Rust
