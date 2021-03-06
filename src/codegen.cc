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

#include "codegen.h"
#include "macro-assembler.h"


namespace rejit {
namespace internal {


void RegisterMatch(vector<Match>* matches, Match new_match) {
  // The current codegen ensures that the match registered is valid. We can just
  // push it.
  matches->push_back(new_match);

  if (FLAG_trace_match_all) {
    printf("Found match: (start %p - end %p) ", new_match.begin, new_match.end);
    const char* c = new_match.begin;
    while (c < new_match.end) {
      printf("%c", *c++);
    }
    printf("\n");
  }
}


// RegexpIndexer ---------------------------------------------------------------

void RegexpIndexer::Index(Regexp* root) {
  IndexSub(root);
  rinfo_->set_entry_state(0);
  rinfo_->set_output_state(this->entry_state());
}


void RegexpIndexer::IndexSub(Regexp* root, int entry, int output) {
  Visit(root);
  root->SetEntryState(entry);
  if (output != -1) {
    root->SetOutputState(output);
  }
  rinfo_->set_last_state(last_state());
}


// TODO(rames): If the definitions stay so small and don't diverge they could be
// changed to inline functions.
void RegexpIndexer::VisitRegexp(Regexp* re) {
  re->SetEntryState(entry_state_);
  re->SetOutputState(++last_state_);
  entry_state_ = re->output_state();
}


void RegexpIndexer::VisitAlternation(Alternation* alternation) {
  int original_entry = entry_state_;
  vector<Regexp*>::iterator it;
  for (it = alternation->sub_regexps()->begin();
       it < alternation->sub_regexps()->end();
       it++) {
    Visit(*it);
    last_state_--;
  }
  last_state_++;
  alternation->SetEntryState(original_entry);
  alternation->SetOutputState(last_state_);
  entry_state_ = alternation->output_state();
}


void RegexpIndexer::VisitConcatenation(Concatenation* concatenation) {
  int original_entry = entry_state_;
  vector<Regexp*>::iterator it;
  for (it = concatenation->sub_regexps()->begin();
       it < concatenation->sub_regexps()->end();
       it++) {
    Visit(*it);
  }
  concatenation->SetEntryState(original_entry);
  concatenation->SetOutputState(last_state_);
  entry_state_ = concatenation->output_state();
}


void RegexpIndexer::VisitRepetition(Repetition* re) {
  // The actual work will be done by the RegexpLister.
  VisitRegexp(re);
}


// RegexpIndexer ---------------------------------------------------------------

void RegexpLister::VisitAlternation(Alternation* regexp) {
  vector<Regexp*>::iterator it;
  for (it = regexp->sub_regexps()->begin();
       it < regexp->sub_regexps()->end();
       it++ ) {
    Visit(*it);
  }
}


void RegexpLister::VisitConcatenation(Concatenation* regexp) {
  vector<Regexp*>::iterator it;
  for (it = regexp->sub_regexps()->begin();
       it < regexp->sub_regexps()->end();
       it++) {
    Visit(*it);
  }
}


void RegexpLister::VisitRepetition(Repetition* repetition) {
  // TODO: HIGH_PRIORITY Better handling of repetitions.
  // TODO: HIGH_PRIORITY Don't leak the newly allocated regexps.
  // TODO: Check we don't leak base.
  // TODO: Fix tracing of repetitions.
  // TODO: Clean.
  // Base may have been referenced by other mechanisms. It must be indexed.
  Regexp* base = repetition->sub_regexp();
  unsigned min_rep = repetition->min_rep();
  unsigned max_rep = repetition->max_rep();
  Regexp* created = NULL;
  Epsilon* epsilon = NULL;

  if (!repetition->IsLimited()) {
    if (min_rep == 0) {
      rinfo()->set_last_state(rinfo()->last_state() + 1);
      RegexpIndexer indexer(rinfo(),
                               rinfo()->last_state(),
                               rinfo()->last_state());
      indexer.IndexSub(base, rinfo()->last_state());

      Epsilon* eps_null =
        new Epsilon(repetition->entry_state(), repetition->output_state());
      ListNew(eps_null);

      Epsilon* eps_in =
        new Epsilon(repetition->entry_state(), base->entry_state());
      ListNew(eps_in);

      Visit(base);

      Epsilon* eps_out =
        new Epsilon(base->output_state(), repetition->output_state());
      ListNew(eps_out);

      Epsilon* eps_rep =
        new Epsilon(base->output_state(), base->entry_state());
      ListNew(eps_rep);

      if (FLAG_trace_repetitions) {
        cout << "Repetion ----------" << endl;
        cout << *eps_in << endl;
        cout << *eps_out << endl;
        cout << *eps_rep << endl;
        cout << *eps_null << endl;
        cout << *base << endl;
        cout << "---------- End of repetition" << endl;
      }

    } else {
      Concatenation* concat = new Concatenation();
      concat->Append(base);
      for (unsigned i = 1; i < min_rep; i++) {
        concat->Append(base->DeepCopy());
      }

      created = concat;

      // Index the created regexp.
      RegexpIndexer indexer(rinfo(),
                               repetition->entry_state(),
                               rinfo()->last_state());
      indexer.IndexSub(created, repetition->entry_state());

      Visit(created);

      Epsilon* eps_rep = new Epsilon(
          concat->output_state(), concat->sub_regexps()->back()->entry_state());
      ListNew(eps_rep);

      epsilon = new Epsilon(concat->output_state(), repetition->output_state());
      ListNew(epsilon);

      if (FLAG_trace_repetitions) {
        cout << "Repetion ----------" << endl;
        if (epsilon) cout << *epsilon << endl;
        cout << *created << endl;
        cout << "---------- End of repetition" << endl;
      }
    }

  } else {
    if (min_rep == 0) {
      epsilon =
        new Epsilon(repetition->entry_state(), repetition->output_state());
      ListNew(epsilon);
      min_rep = 1;
    }

    Concatenation* concat = new Concatenation();
      concat->Append(base);
    for (unsigned i = 1; i < max_rep; i++) {
      concat->Append(base->DeepCopy());
    }
    created = concat;
    // Index and list the created regexp.
    RegexpIndexer indexer(rinfo(),
                             repetition->entry_state(),
                             rinfo()->last_state());
    indexer.IndexSub(created,
                     repetition->entry_state(), repetition->output_state());

    Visit(created);

    // Now add epsilon transitions to the output state.
    vector<Regexp*>::const_iterator it;
    for (it = concat->sub_regexps()->begin() + min_rep - 1;
         it < concat->sub_regexps()->end() - 1;
         it++) {
      ListNew(
          new Epsilon((*it)->output_state(), concat->output_state()));
    }

    if (FLAG_trace_repetitions) {
      cout << "Repetion ----------" << endl;
      if (epsilon) cout << *epsilon << endl;
      cout << *created << endl;
      cout << "---------- End of repetition" << endl;
    }
  }
}


bool FF_finder::VisitAlternation(Alternation* alt) {
  // TODO(rames): Try to find common substrings to reduce
  bool res = true;
  vector<Regexp*>::iterator it;
  for (it = alt->sub_regexps()->begin();
       it < alt->sub_regexps()->end();
       it++) {
    res &= Visit(*it);
  }
  return res;
}


// TODO: Clean this.
bool FF_finder::VisitConcatenation(Concatenation* concat) {
  bool res = false;
  bool cur_res;
  vector<Regexp*>::iterator it;
  size_t cur_start = regexp_list_->size();
  size_t cur_end = regexp_list_->size();

  for (it = concat->sub_regexps()->begin();
       it < concat->sub_regexps()->end();
       it++) {
    cur_res = Visit(*it);
    if (!cur_res) {
      regexp_list_->erase(regexp_list_->begin() + cur_end, regexp_list_->end());
    }
    res |= cur_res;
    if (cur_start == cur_end) {
      // No sub-regexp has been visited successfully yet.
      cur_end = regexp_list_->size();
    }
    if (ff_cmp(cur_start, cur_end, regexp_list_->size()) >= 0) {
      regexp_list_->erase(regexp_list_->begin() + cur_end, regexp_list_->end());
    } else {
      regexp_list_->erase(regexp_list_->begin() + cur_start,
                          regexp_list_->begin() + cur_end);
    }
    cur_end = regexp_list_->size();
  }

  return res;
}


bool FF_finder::VisitRepetition(Repetition* rep) {
  if (rep->min_rep() > 0) {
    Visit(rep->sub_regexp());
  } else {
    return false;
  }
  return true;
}


// A positive return value means that i1:i2 is better than i2:i3 for
// fast forwarding.
int FF_finder::ff_cmp(size_t i1,
                      size_t i2,
                      size_t i3) {
  size_t s1 = i2 - i1;
  size_t s2 = i3 - i2;
  int score_1 = 0, score_2 = 0;

  // We need the lowest score, but we need some regexps to look for !
  if (s1 == 0) return -1;
  if (s2 == 0) return  1;

  for (size_t i = i1; i < i2; i++) {
    score_1 += regexp_list_->at(i)->ff_score();
  }
  for (size_t i = i2; i < i3; i++) {
    score_2 += regexp_list_->at(i)->ff_score();
  }

  return score_2 - score_1;
}


// Codegen ---------------------------------------------------------------------

VirtualMemory* Codegen::Compile(RegexpInfo* rinfo, MatchType match_type) {
  Regexp* root = rinfo->regexp();
  RegexpIndexer indexer(rinfo);
  indexer.Index(root);
  if (FLAG_print_re_tree) {
    cout << "Regexp tree --------------------------------{{{" << endl;
    cout << *root << endl;
    cout << "}}}------------------------- End of regexp tree" << endl;
  }
  RegexpLister lister(rinfo, rinfo->gen_list());
  lister.Visit(root);

  int n_states = rinfo->last_state() + 1;

  // Align size with cache line size?
  state_ring_time_size_ = kPointerSize * n_states;

  state_ring_times_ = 1 + min(rinfo->regexp_max_length(), kMaxNodeLength);

  state_ring_size_ = state_ring_time_size_ * state_ring_times_;

  // TODO(rames): Investigate if rounding the state ring size to a power of 2
  // could speedup state related operations.

  // Bit <n> set in the time summary indicates that there is at least one state
  // set for time <n>.
  // A bit clear indicates that no states are set.
  time_summary_size_ = kPointerSize * ((state_ring_times_ / kBitsPerPointer) +
    ((state_ring_times_ % kBitsPerPointer) != 0));
  
  ring_base_ = Operand(rbp, StateRingBaseOffsetFromFrame());

  if (FLAG_print_state_ring_info) {
    cout << "State ring info ----------------------------{{{" << endl;
    cout << "n_states : " << n_states << endl;
    cout << "state_ring_time_size_ : " << state_ring_time_size_ << endl;
    cout << "state_ring_times_ : " << state_ring_times_ << endl;
    cout << "state_ring_size_ : " << state_ring_size_ << endl;
    cout << "time_summary_size_ : " << time_summary_size_ << endl;
    cout << "}}}--------------------- End of state ring info" << endl;
  }


  Generate(rinfo, match_type);

  return masm_->GetCode();
}


} }  // namespace rejit::internal

