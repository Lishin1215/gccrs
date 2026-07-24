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
#include "rust-hir-pattern.h"

namespace Rust {
namespace BIR {

DropAnalysis &
DropAnalysis::get ()
{
  static DropAnalysis instance;
  return instance;
}

void
DropAnalysis::clear ()
{
  drop_states.clear ();
}

void
DropAnalysis::analyze (Function &function)
{
  std::vector<bool> initialized (function.place_db.size (), false);
  std::map<HirId, DropState> function_states;
  std::set<BasicBlockId> visited;
  BasicBlockId current = ENTRY_BASIC_BLOCK;

  while (current != INVALID_BB)
    {
      if (!visited.insert (current).second)
	return;

      const BasicBlock &block = function.basic_blocks[current];
      for (const Statement &statement : block.statements)
	{
	  PlaceId lhs = statement.get_place ();
	  switch (statement.get_kind ())
	    {
	    case Statement::Kind::STORAGE_LIVE:
	      initialized[lhs.value] = false;
	      break;

	    case Statement::Kind::ASSIGNMENT:
	      {
		const AbstractExpr &expr = statement.get_expr ();
		if (expr.get_kind () == ExprKind::ASSIGNMENT)
		  {
		    PlaceId rhs = static_cast<const Assignment &> (expr).get_rhs ();
		    const Place &rhs_place = function.place_db[rhs];
		    if (rhs_place.kind == Place::VARIABLE
			&& rhs_place.should_be_moved ())
		      initialized[rhs.value] = false;
		  }

		initialized[lhs.value] = true;
		break;
	      }

	    case Statement::Kind::STORAGE_DEAD:
	      {
		const Place &place = function.place_db[lhs];
		if (place.kind == Place::VARIABLE)
		  {
		    auto hirid = Analysis::Mappings::get ().lookup_node_to_hir (
		      static_cast<NodeId> (place.variable_or_field_index));
		    if (hirid.has_value ())
		      function_states[hirid.value ()]
			= initialized[lhs.value] ? DropState::STATIC
						 : DropState::DEAD;
		  }
		initialized[lhs.value] = false;
		break;
	      }

	    case Statement::Kind::SWITCH:
	    case Statement::Kind::RETURN:
	    case Statement::Kind::GOTO:
	    case Statement::Kind::USER_TYPE_ASCRIPTION:
	    case Statement::Kind::FAKE_READ:
	      break;
	    }
	}

      if (block.successors.empty ())
	break;

      if (block.successors.size () != 1)
	return;

      current = block.successors.front ();
    }

  drop_states.insert (function_states.begin (), function_states.end ());
}

bool
DropAnalysis::lookup (HirId id, DropState *state) const
{
  auto it = drop_states.find (id);
  if (it == drop_states.end ())
    return false;

  *state = it->second;
  return true;
}

void
DropAnalysis::dump (std::ostream &stream, Function &function) const
{
  auto &mappings = Analysis::Mappings::get ();

  for (PlaceId id = FIRST_VARIABLE_PLACE; id.value < function.place_db.size ();
       ++id.value)
    {
      const Place &place = function.place_db[id];
      if (place.kind != Place::VARIABLE)
	continue;

      auto hirid = mappings.lookup_node_to_hir (
	static_cast<NodeId> (place.variable_or_field_index));
      if (!hirid.has_value ())
	continue;

      DropState state;
      if (!lookup (hirid.value (), &state))
	continue;

      std::string name = "_";
      auto pattern = mappings.lookup_hir_pattern (hirid.value ());
      if (pattern.has_value ()
	  && pattern.value ()->get_pattern_type () == HIR::Pattern::IDENTIFIER)
	name = static_cast<HIR::IdentifierPattern *> (pattern.value ())
		 ->get_identifier ()
		 .as_string ();

      stream << "drop-state " << name << ": "
	     << (state == DropState::STATIC ? "static" : "dead") << "\n";
    }
}

} // namespace BIR
} // namespace Rust
